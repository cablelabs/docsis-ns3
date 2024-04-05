/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
 * Copyright (c) 2017-2020 Cable Television Laboratories, Inc. (DOCSIS changes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Shravya K.S. <shravya.ks0@gmail.com>
 *          Tom Henderson <tomh@tomh.org> (Extensions by CableLabs LLD project)
 */

#include "dual-queue-coupled-aqm.h"

#include "docsis-queue-disc-item.h"
#include "math.h"
#include "queue-protection.h"

#include "ns3/abort.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/enum.h"
#include "ns3/ipv4-header.h"
#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3
{
namespace docsis
{

// Constants in use for the DOCSIS mode
static const uint32_t MIN_PKTSIZE = 64;
static const Time LATENCY_LOW = MilliSeconds(5);
// The below constant represents Default Upstream Target Buffer ('D')
// from section C.1.2.17, and applies herein to both upstream and downstream
static const Time DEFAULT_UPSTREAM_TARGET_BUFFER_CONFIGURATION = MilliSeconds(100);
// The below constant represents the 10ms in equation (3) of C.2.2.9.11.4
static const Time DEFAULT_LOW_LATENCY_TARGET_BUFFER = MilliSeconds(10);

// Constants for improved code readability
static bool INCLUDE_MAC_HEADERS = true;
static bool EXCLUDE_MAC_HEADERS = false;

NS_LOG_COMPONENT_DEFINE("DualQueueCoupledAqm");

NS_OBJECT_ENSURE_REGISTERED(DualQueueCoupledAqm);

TypeId
DualQueueCoupledAqm::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::docsis::DualQueueCoupledAqm")
            .SetParent<QueueDisc>()
            .SetGroupName("Docsis")
            .AddConstructor<DualQueueCoupledAqm>()
            .AddAttribute("IaqmOn",
                          "Indicates whether IAQM is enabled",
                          BooleanValue(true),
                          MakeBooleanAccessor(&DualQueueCoupledAqm::m_iaqmOn),
                          MakeBooleanChecker())
            .AddAttribute("A",
                          "Value of alpha (Hz^2)",
                          DoubleValue(0.25),
                          MakeDoubleAccessor(&DualQueueCoupledAqm::m_alpha),
                          MakeDoubleChecker<double>())
            .AddAttribute("B",
                          "Value of beta (Hz^2)",
                          DoubleValue(2.5),
                          MakeDoubleAccessor(&DualQueueCoupledAqm::m_beta),
                          MakeDoubleChecker<double>())
            .AddAttribute("Interval",
                          "Sample interval in the Classic queue control path",
                          TimeValue(MilliSeconds(16)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_interval),
                          MakeTimeChecker())
            .AddAttribute("Supdate",
                          "Simulation start time of the Classic queue update process",
                          TimeValue(Seconds(0.0)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_sUpdate),
                          MakeTimeChecker())
            .AddAttribute(
                "MaxSize",
                "The maximum number of bytes accepted by the overall queue (both flows). "
                "This value is not in the specification but is an ns-3 overall queue limit value. "
                "Queue limits in the specification are enforced on the individual flow queues.",
                QueueSizeValue(QueueSize("200MB")), // 250 ms @ 640 Gb/s
                MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                MakeQueueSizeChecker())
            // ClassicBufferSize corresponds to BUFFER_SIZE in Annex M PIE AQM.
            // This value is usually set by the TargetBuffer parameter in
            // the service flow definition.  The value must be configured before
            // simulation runtime; if the value is set after the simulation starts,
            // it will have no effect.  The order of operations and precedence is
            // as follows:  1) if the TargetBuffer is set in the service flow
            // configuration, that value will be configured, 2) else if this
            // attribute is configured differently than the default of "0B", then
            // that value will be configured, or 3) else the default of "0B" will
            // cause the specification default of Default Upstream Target Buffer
            // Configuration * min (MSR, Peak Rate) to be configured.
            .AddAttribute("ClassicBufferSize",
                          "The maximum number of bytes accepted by the classic queue",
                          QueueSizeValue(QueueSize("0B")), // default to 100ms @ AMSR
                          MakeQueueSizeAccessor(&DualQueueCoupledAqm::m_classicBufferSize),
                          MakeQueueSizeChecker())
            // LowLatencyConfigBufferSize corresponds to CONFIG_BUFFER_SIZE in Annex
            // N.3.  This value is usually set by the TargetBuffer parameter in
            // the service flow definition.  The value must be configured before
            // simulation runtime; if the value is set after the simulation starts,
            // it will have no effect.  The order of operations and precedence is
            // as follows:  1) if the TargetBuffer is set in the service flow
            // configuration, that value will be configured, 2) else if this
            // attribute is configured differently than the default of "0B", then
            // that value will be configured, or 3) else the default of "0B" will
            // cause the specification default of 10 ms * MAX_RATE to be configured.
            .AddAttribute("LowLatencyConfigBufferSize",
                          "The maximum number of bytes accepted by the low latency queue",
                          QueueSizeValue(QueueSize("0B")), // default to 10ms @ AMSR
                          MakeQueueSizeAccessor(&DualQueueCoupledAqm::m_lowLatencyConfigBufferSize),
                          MakeQueueSizeChecker())
            .AddAttribute("ClassicAqmLatencyTarget",
                          "Target queue delay of Classic traffic (C.2.2.7.15.2)",
                          TimeValue(MilliSeconds(10)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_latencyTarget),
                          MakeTimeChecker())
            .AddAttribute("LgRange",
                          "Log2(range) of the range of IAQM ramp",
                          UintegerValue(19),
                          MakeUintegerAccessor(&DualQueueCoupledAqm::m_lgRange),
                          MakeUintegerChecker<uint16_t>(0, 25))
            .AddAttribute(
                "MinTh",
                "MINTH threshold for immediate AQM ramp function",
                TypeId::ATTR_GET,
                TimeValue(Seconds(
                    0)), // this value is ignored because there is no setter (i.e. read-only)
                MakeTimeAccessor(&DualQueueCoupledAqm::GetMinTh),
                MakeTimeChecker())
            .AddAttribute("MaxTh",
                          "MAXTH threshold for immediate AQM ramp function",
                          TimeValue(MicroSeconds(1000)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_maxTh),
                          MakeTimeChecker())
            .AddAttribute("Coupled",
                          "Indicates whether the PIE AQM is part of a Coupled DualQ",
                          BooleanValue(true),
                          MakeBooleanAccessor(&DualQueueCoupledAqm::m_coupled),
                          MakeBooleanChecker())
            .AddAttribute("CouplingFactor",
                          "Coupling factor (1/10 of the integer AQM Coupling Factor)",
                          DoubleValue(2),
                          MakeDoubleAccessor(&DualQueueCoupledAqm::m_couplingFactor),
                          MakeDoubleChecker<double>(0, 25.5))
            .AddAttribute("SchedulingWeight",
                          "Default value of scheduling weight if not present in ASF configuration; "
                          "if value is changed here, align with CmtsUpstreamScheduler",
                          UintegerValue(230),
                          MakeUintegerAccessor(&DualQueueCoupledAqm::m_schedulingWeight),
                          MakeUintegerChecker<uint32_t>(1, 255))
            .AddAttribute("DrrQuantum",
                          "Quantum used in weighted DRR policy (bytes)",
                          UintegerValue(1500),
                          MakeUintegerAccessor(&DualQueueCoupledAqm::m_drrQuantum),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MaxFrameSize",
                          "MAX_FRAME_SIZE constant used to set FLOOR (bytes)",
                          UintegerValue(2000),
                          MakeUintegerAccessor(&DualQueueCoupledAqm::m_maxFrameSize),
                          MakeUintegerChecker<uint32_t>())
            // DOCSIS PIE data path
            .AddAttribute("MeanPktSize",
                          "Average of packet size",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&DualQueueCoupledAqm::m_meanPktSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MaxBurstAllowance",
                          "Current max burst allowance in seconds before random drop",
                          TimeValue(Seconds(0.142)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_maxBurst),
                          MakeTimeChecker())
            .AddAttribute("BurstResetTimeout",
                          "Time to wait before transitioning to INACTIVE",
                          TimeValue(Seconds(1)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_burstResetTimeout),
                          MakeTimeChecker())
            .AddAttribute("ProbLow",
                          "PROB_LOW from DOCSIS PIE specification",
                          DoubleValue(0.85),
                          MakeDoubleAccessor(&DualQueueCoupledAqm::m_probLow),
                          MakeDoubleChecker<double>())
            .AddAttribute("ProbHigh",
                          "PROB_HIGH from DOCSIS PIE specification",
                          DoubleValue(8.5),
                          MakeDoubleAccessor(&DualQueueCoupledAqm::m_probHigh),
                          MakeDoubleChecker<double>())
            .AddAttribute("LlEstimator",
                          "Determines LL-queue latency estimate policy in use",
                          EnumValue(QDELAY_COUPLED_L),
                          MakeEnumAccessor<LlEstimatorPolicy>(&DualQueueCoupledAqm::m_llEstimator),
                          MakeEnumChecker(QDELAY_COUPLED_L,
                                          "QDelayCoupledL",
                                          QDELAY_COUPLED_V,
                                          "QDelayCoupledV"))
            .AddAttribute("VqInterval",
                          "Interval to check whether virtual queue needs to be "
                          "adjusted (typically half of MAP interval)",
                          TimeValue(MicroSeconds(500)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_vqInterval),
                          MakeTimeChecker())
            .AddAttribute("LgVqEwmaAlph",
                          "Log base 2 of the reciprocal of the exponentially-weighted moving "
                          "average weight for the LL virtual queue.",
                          DoubleValue(7.0),
                          MakeDoubleAccessor(&DualQueueCoupledAqm::m_lgVqEwmaAlpha),
                          MakeDoubleChecker<double>())
            .AddAttribute("AllowedAqConstant",
                          "Constant factor to multiply with the GGR*GGI term",
                          DoubleValue(1),
                          MakeDoubleAccessor(&DualQueueCoupledAqm::m_aqConstant),
                          MakeDoubleChecker<double>(0, 2))
            // The following attributes are intended to be set by the CmNetDevice only
            .AddAttribute("VqMapInterval",
                          "MAP interval used for VQ calculations",
                          TimeValue(Seconds(0)),
                          MakeTimeAccessor(&DualQueueCoupledAqm::m_vqMapInterval),
                          MakeTimeChecker())
            .AddAttribute("VqFramesPerMap",
                          "Value used for VQ calculations",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DualQueueCoupledAqm::m_vqFramesPerMap),
                          MakeUintegerChecker<uint16_t>())
            // Trace sources
            .AddTraceSource("ClassicBytes",
                            "Bytes in Classic queue, including DOCSIS MAC Header bytes",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_traceClassicBytes),
                            "ns3::TracedValueCallback::Uint32")
            .AddTraceSource("LowLatencyBytes",
                            "Bytes in Low Latency queue, including DOCSIS MAC Header bytes",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_traceLlBytes),
                            "ns3::TracedValueCallback::Uint32")
            .AddTraceSource("PieQueueBytes",
                            "Current PIE queue_.byte_length() including all MAC "
                            "PDU bytes without DOCSIS MAC overhead",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_tracePieQueueBytes),
                            "ns3::TracedValueCallback::Uint32")
            .AddTraceSource("ClassicSojournTime",
                            "Sojourn time of the last packet dequeued from the Classic queue",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_traceClassicSojourn),
                            "ns3::Time::TracedCallback")
            .AddTraceSource("LowLatencySojournTime",
                            "Sojourn time of the last packet dequeued from the LL queue",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_traceLlSojourn),
                            "ns3::Time::TracedCallback")
            .AddTraceSource("ClassicDropProbability",
                            "Classic drop probability",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_classicDropProb),
                            "ns3::TracedValueCallback::Double")
            .AddTraceSource("ProbCL",
                            "Coupled marking probability from Classic queue",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_probCL),
                            "ns3::TracedValueCallback::Double")
            .AddTraceSource("ProbNative",
                            "Current native marking probability of LL queue",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_probNative),
                            "ns3::TracedValueCallback::Double")
            .AddTraceSource("BaseProbability",
                            "Base probability",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_baseProb),
                            "ns3::TracedValueCallback::Double")
            .AddTraceSource("EnqueueState",
                            "State of queue at packet enqueue time",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_enqueueStateTrace),
                            "ns3::TracedValueCallback::DualQEnqueueStateTracedCallback")
            .AddTraceSource("CalculatePState",
                            "State of variables at CalculateP events",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_calculatePStateTrace),
                            "ns3::TracedValueCallback::CalculatePStateTracedCallback")
            .AddTraceSource("LowLatencyQueueDelay",
                            "Low latency queue delay estimate upon entering Iaqm ()",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_llQueueDelayTrace),
                            "ns3::TracedValueCallback::DualQLlQueueDelayTracedCallback")
            .AddTraceSource("LowLatencyDequeue",
                            "Notify the dequeue of a packet classified to the L queue",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_llDequeueTrace),
                            "ns3::TracedValueCallback::DualQLlDequeueTracedCallback")
            .AddTraceSource("LowLatencyArrival",
                            "Notify the arrival of a packet classified to the L queue",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_llArrivalTrace),
                            "ns3::TracedValue::Uint32Callback")
            .AddTraceSource("ClassicArrival",
                            "Notify the arrival of a packet classified to the C queue",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_classicArrivalTrace),
                            "ns3::TracedValue::Uint32Callback")
            .AddTraceSource("AlignVq",
                            "After alignment, return length of actual and virtual "
                            "queue, average queue, and allowed AQ, in bytes",
                            MakeTraceSourceAccessor(&DualQueueCoupledAqm::m_alignVqTrace),
                            "ns3::DualQueueCoupledAqm::AlignVqTracedCallback");

    return tid;
}

DualQueueCoupledAqm::DualQueueCoupledAqm()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES),
      m_classicDeficit(0),
      m_llDeficit(0),
      m_intervalBitsL(0),
      m_cqEstimateAtUpdate(Seconds(0)),
      m_llDataPduBytes(0),
      m_vq(Seconds(0)),
      m_tLast(Seconds(0))
{
    NS_LOG_FUNCTION(this);
    m_uv = CreateObject<UniformRandomVariable>();
    m_qDelaySingleCallback = MakeNullCallback<Time>();
}

DualQueueCoupledAqm::~DualQueueCoupledAqm()
{
    NS_LOG_FUNCTION(this);
    if (m_alignVqTimer.IsRunning())
    {
        m_alignVqTimer.Cancel();
    }
}

void
DualQueueCoupledAqm::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_uv = nullptr;
    if (m_queueProtection)
    {
        m_queueProtection->Dispose();
        m_queueProtection = nullptr;
    }
    Simulator::Remove(m_updateEvent);
    m_qDelaySingleCallback = MakeNullCallback<Time>();
    if (m_asf)
    {
        m_asf->SetLowLatencyServiceFlow(nullptr);
        m_asf->SetClassicServiceFlow(nullptr);
    }
    m_asf = nullptr;
    m_sf = nullptr;
    QueueDisc::DoDispose();
}

void
DualQueueCoupledAqm::SetQueueProtection(Ptr<QueueProtection> qp)
{
    NS_LOG_FUNCTION(this << qp);
    m_queueProtection = qp;
}

Ptr<QueueProtection>
DualQueueCoupledAqm::GetQueueProtection() const
{
    return m_queueProtection;
}

void
DualQueueCoupledAqm::SetQDelaySingleCallback(Callback<Time> qDelaySingleCallback)
{
    NS_LOG_FUNCTION(this);
    m_qDelaySingleCallback = qDelaySingleCallback;
}

void
DualQueueCoupledAqm::SetLoopDelayCallback(Callback<std::pair<Time, uint32_t>> loopDelayCallback)
{
    NS_LOG_FUNCTION(this);
    m_loopDelayCallback = loopDelayCallback;
}

void
DualQueueCoupledAqm::SetAsf(Ptr<AggregateServiceFlow> asf)
{
    NS_LOG_FUNCTION(this << asf);
    NS_ABORT_MSG_IF(IsInitialized(), "Must call before device is initialized");
    // Note:  If this model is changed in the future to allow asf/sf to be
    // changed after initialization, then the code to set MAX_RATE and FLOOR
    // will need to be called also to update those dependent parameters
    NS_ABORT_MSG_IF(m_sf, "Cannot set an ASF if a single SF was already set");
    if (m_asf)
    {
        NS_LOG_WARN("Overwriting previously set ASF: " << m_asf);
    }
    m_asf = asf;
}

void
DualQueueCoupledAqm::SetSf(Ptr<ServiceFlow> sf)
{
    NS_LOG_FUNCTION(this << sf);
    NS_ABORT_MSG_IF(IsInitialized(), "Must call before device is initialized");
    NS_ABORT_MSG_IF(m_asf, "Cannot set a single SF if an ASF was already set");
    NS_ABORT_MSG_IF(sf->m_sfid == LOW_LATENCY_SFID, "Single service flows should be classic SF");
    if (m_sf)
    {
        NS_LOG_WARN("Overwriting previously set SF: " << m_sf);
    }
    m_sf = sf;
}

uint32_t
DualQueueCoupledAqm::GetLowLatencyQueueSize(bool includeMacHeaders) const
{
    if (includeMacHeaders)
    {
        return GetInternalQueue(LL)->GetCurrentSize().GetValue();
    }
    else
    {
        return m_llDataPduBytes;
    }
}

uint32_t
DualQueueCoupledAqm::GetClassicQueueSize(bool includeMacHeaders) const
{
    if (includeMacHeaders)
    {
        return GetInternalQueue(CLASSIC)->GetCurrentSize().GetValue();
    }
    else
    {
        return m_tracePieQueueBytes.Get();
    }
}

Time
DualQueueCoupledAqm::GetClassicQueuingDelay(uint32_t size) const
{
    Ptr<const QueueDiscItem> item;
    return m_cqEstimateAtUpdate;
}

Time
DualQueueCoupledAqm::GetLowLatencyQueuingDelay()
{
    Time delay;
    if (m_llEstimator == QDELAY_COUPLED_V)
    {
        delay = QDelayCoupledV(0);
        NS_LOG_LOGIC("Virtual queue delay of " << delay.As(Time::NS));
    }
    else if (m_llEstimator == QDELAY_COUPLED_L)
    {
        delay = QDelayCoupledL(GetLowLatencyQueueSize(EXCLUDE_MAC_HEADERS));
        NS_LOG_LOGIC("Standard queue delay of " << delay.As(Time::MS) << " from queue of "
                                                << GetLowLatencyQueueSize(EXCLUDE_MAC_HEADERS)
                                                << " bytes");
    }
    return delay;
}

Time
DualQueueCoupledAqm::QDelayCoupledV(uint32_t pSize)
{
    if (m_maxRate.GetBitRate() == 0)
    {
        return Time::FromInteger(0, Time::S);
    }
    else
    {
        m_vq -= (Simulator::Now() - m_tLast);
        if (m_vq < Seconds(0))
        {
            m_vq = Seconds(0);
        }
        m_vq += Time::FromInteger(pSize * 8 * 1e9 / m_maxRate.GetBitRate(), Time::NS);
        m_tLast = Simulator::Now();
        return m_vq;
    }
}

Time
DualQueueCoupledAqm::QDelayCoupledL(uint32_t byteLength) const
{
    NS_LOG_FUNCTION(this << byteLength);
    if (m_maxRate.GetBitRate() == 0)
    {
        return Time::FromInteger(0, Time::S);
    }
    else
    {
        // LL queue delay uses ns units in the spec, but ns-3 uses a Time object
        return Time::FromInteger(8 * byteLength * 1e9 / m_maxRate.GetBitRate(), Time::NS);
    }
}

double
DualQueueCoupledAqm::GetClassicDropProbability() const
{
    return m_classicDropProb;
}

// This method is called either upon low latency dequeue or upon a VQ
// interval timeout.  Drain the queue.  If the difference between avg.
// queue length and avg. queue allowed exceeds the VQ length, set the
// VQ to that difference value.
void
DualQueueCoupledAqm::AlignVq()
{
    NS_LOG_FUNCTION(this);
    bool aligned = false;
    //  smooth the actual queue (AQ)
    double alpha = std::pow(2, -1 * m_lgVqEwmaAlpha);
    // The current queue length is provided via a callback function that is
    // hooked to the CmNetDevice to return a std::pair<Time, uint32_t>.  The
    // first value is the loop delay estimate; the second is the current LL
    // queue length in bytes, including both bytes stored in this AQM and
    // any partially sent LL packet bytes or staged data bytes within the
    // CmNetDevice.  The m_loopDelayCallback ().second value below accesses
    // the LL queue length value.  The CmtsNetDevice will return zero for
    // both values because these variables are relevant only to the CM side.
    // The following prevents integer overflow on the specification formula
    int32_t averageAqBytes = m_averageAqBytes;
    averageAqBytes += alpha * static_cast<int32_t>(m_loopDelayCallback().second - m_averageAqBytes);
    m_averageAqBytes = (averageAqBytes > 0) ? static_cast<uint32_t>(averageAqBytes) : 0;
    if (m_averageAqBytes > 0)
    {
        NS_LOG_LOGIC("EWMA alpha = " << alpha);
        NS_LOG_LOGIC("Update EWMA-filtered avg LL queue length to " << m_averageAqBytes
                                                                    << " bytes");
    }
    uint32_t allowedAqBytes = 0;
    // The following implements calcAllowedAQ() from Annex 0
    {
        uint64_t ggr = 0;
        if (m_asf && m_asf->GetLowLatencyServiceFlow() &&
            m_asf->GetLowLatencyServiceFlow()->m_guaranteedGrantRate.GetBitRate())
        {
            ggr = m_asf->GetLowLatencyServiceFlow()->m_guaranteedGrantRate.GetBitRate();
            // The below assert is to catch a possible configuration error
            NS_ASSERT_MSG(ggr <= m_maxRate.GetBitRate(), "Error:  GGR > MAX_RATE");
        }
        if (!m_loopDelayCallback.IsNull())
        {
            // Callback is checked for validity before entering this branch because
            // some test suites do not set it
            allowedAqBytes =
                m_loopDelayCallback().first.GetSeconds() * (m_maxRate.GetBitRate() - ggr) / 8 +
                m_aqConstant * (ggr * m_pgsGrantInterval.GetSeconds() / 8);
            NS_LOG_LOGIC("allowedAqBytes = " << allowedAqBytes);
        }
    }
    // Virtual Queue alignment if conditions are met
    if (m_averageAqBytes > allowedAqBytes)
    {
        uint32_t excessAqBytes = m_averageAqBytes - allowedAqBytes;
        Time excessAqDelay =
            Time::FromInteger(excessAqBytes * 8 * 1e9 / m_maxRate.GetBitRate(), Time::NS);
        if (excessAqDelay > m_vq)
        {
            NS_LOG_INFO("Align VQ from " << m_vq.As(Time::NS) << " to "
                                         << excessAqDelay.As(Time::NS));
            m_vq = excessAqDelay;
            aligned = true;
        }
    }
    m_alignVqTrace(m_loopDelayCallback().second, m_vq, m_averageAqBytes, allowedAqBytes, aligned);
    m_alignVqTimer.Schedule();
}

double
DualQueueCoupledAqm::CalcProbNative(Time qDelay) const
{
    NS_LOG_FUNCTION(this << qDelay);
    double probNative = 0;
    if (qDelay >= GetMaxTh())
    {
        probNative = 1;
    }
    else if (qDelay > GetMinTh())
    {
        // ramp function from MINTH to (MINTH + RANGE)
        probNative = (qDelay - GetMinTh()).GetSeconds() / (GetMaxTh() - GetMinTh()).GetSeconds();
    }
    NS_LOG_LOGIC("LL mark probability due to internal AQM: " << probNative);
    NS_ABORT_MSG_IF(probNative > 1 || probNative < 0, "Check for an invalid value");
    return probNative;
}

double
DualQueueCoupledAqm::GetProbNative() const
{
    return m_probNative;
}

double
DualQueueCoupledAqm::GetProbCL() const
{
    return m_probCL;
}

Time
DualQueueCoupledAqm::GetMinTh() const
{
    return m_minTh;
}

Time
DualQueueCoupledAqm::GetMaxTh() const
{
    return m_maxTh;
}

int64_t
DualQueueCoupledAqm::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_uv->SetStream(stream);
    return 1;
}

bool
DualQueueCoupledAqm::Iaqm(Ptr<QueueDiscItem> item, double probNative)
{
    NS_LOG_FUNCTION(this << probNative);
    // Combine Native and Coupled probabilities into ECN marking probL
    double probL = std::max<double>(probNative, std::min<double>(m_probCL, 1));
    bool exitCe = Recur(probL);
    NS_LOG_LOGIC("ProbNative " << probNative << " EXIT_CE " << exitCe);
    return exitCe;
}

bool
DualQueueCoupledAqm::ClassicEnqueue(Ptr<DocsisQueueDiscItem> docsisItem)
{
    NS_LOG_FUNCTION(this << docsisItem);
    Time qDelay = GetClassicQueuingDelay();
    m_enqueueStateTrace(qDelay,
                        GetCurrentSize().GetValue(),
                        m_classicDropProb,
                        m_burstReset,
                        m_burstState);
    if (DropEarly(docsisItem))
    {
        // Early probability drop: proactive
        NS_LOG_INFO("EarlyDrop (DOCSISmode) classic queue; C_Delay " << qDelay);
        DropBeforeEnqueue(docsisItem, UNFORCED_CLASSIC_DROP);
        return false;
    }
    // DSCP overwrite
    uint8_t dscp;
    if (docsisItem->GetUint8Value(QueueItem::IP_DSFIELD, dscp))
    {
        uint8_t initialDscp = dscp;
        dscp &= m_classicDscpOverwrite.m_tosAndMask;
        dscp |= m_classicDscpOverwrite.m_tosOrMask;
        if (dscp != initialDscp)
        {
            NS_LOG_LOGIC("DSCP overwrite classic packet from 0x" << std::hex << +(initialDscp >> 2)
                                                                 << " to 0x" << std::hex
                                                                 << +(dscp >> 2));
            bool retVal [[maybe_unused]] = docsisItem->SetUint8Value(QueueItem::IP_DSFIELD, dscp);
            NS_ASSERT_MSG(retVal, "Did not set DSCP value");
        }
    }
    if (GetInternalQueue(CLASSIC)->Enqueue(docsisItem))
    {
        NS_LOG_LOGIC("Classic queue enqueue successful; C_Delay " << qDelay);
        m_traceClassicBytes += docsisItem->GetSize();
        // PIE queue bytes should not count the DOCSIS MAC header
        m_tracePieQueueBytes += (docsisItem->GetSize() - docsisItem->GetMacHeaderLength());
        NS_LOG_LOGIC("Current size in classic queue "
                     << GetInternalQueue(CLASSIC)->GetCurrentSize().GetValue());
        NS_LOG_LOGIC("Current size in both queues: " << GetCurrentSize().GetValue());
        return true;
    }
    else
    {
        NS_LOG_INFO("Drop ClassicQueue enqueue not succeeding; C_Delay " << qDelay);
        // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called
        // by the internal queue because QueueDisc::AddInternalQueue
        // sets the trace callback, so no need to call it here
        return false;
    }
}

bool
DualQueueCoupledAqm::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);
    int32_t queueNumber;
    Ptr<DocsisQueueDiscItem> docsisItem = DynamicCast<DocsisQueueDiscItem>(item);
    NS_ASSERT_MSG(docsisItem, "DocsisQueueDiscItem not found");

    // This check is not in the specification but pertains to exceeding the
    // overall queue limit of the ns-3 queue.  The specification defines
    // additional per-SF limits, which are implemented further below.
    if (GetCurrentSize() + item > GetMaxSize())
    {
        // Drops due to queue limit
        NS_LOG_INFO("Drop due to overall queue limit " << GetMaxSize() << " exceeded");
        DropBeforeEnqueue(item, FORCED_DROP);
        return false;
    }
    // For further study; insert checks for starvation here
    queueNumber = Classify(item);
    if (queueNumber == LL)
    {
        NS_LOG_INFO("LL packet");
        m_llArrivalTrace(item->GetSize());
    }
    else if (queueNumber == CLASSIC)
    {
        NS_LOG_INFO("CLASSIC packet");
        m_classicArrivalTrace(item->GetSize());
    }
    else if (queueNumber == PacketFilter::PF_NO_MATCH)
    {
        // Typically for non-IP packets (e.g. ARP)
        NS_LOG_INFO("Packet arrival, no filter match-- place into CLASSIC queue");
        queueNumber = CLASSIC;
        m_classicArrivalTrace(item->GetSize());
    }
    else
    {
        NS_FATAL_ERROR("Configuration error on queue classification " << queueNumber);
    }

    if (queueNumber == CLASSIC)
    {
        return ClassicEnqueue(docsisItem);
    }

    // Derive qdelay of qL using qdelayCoupledV() (see Annex O)
    // Note1: for downstream use by the CMTS,
    // qdelay = qdelayCoupledL(q_byte_length + packet.size);
    // should be used instead
    Time delay; // Delay to use in the algorithm
    if (m_llEstimator == QDELAY_COUPLED_V)
    {
        delay = QDelayCoupledV(item->GetSize() - docsisItem->GetMacHeaderLength());
        NS_LOG_LOGIC("Using virtual queue delay estimate of "
                     << delay.As(Time::MS) << " for packet size " << item->GetSize() << " bytes");
    }
    else if (m_llEstimator == QDELAY_COUPLED_L)
    {
        delay = QDelayCoupledL(GetLowLatencyQueueSize(INCLUDE_MAC_HEADERS) + item->GetSize());
        NS_LOG_LOGIC("Using standard queue delay estimate of "
                     << delay << " from queue of " << GetLowLatencyQueueSize(INCLUDE_MAC_HEADERS)
                     << " and " << item->GetSize() << " bytes");
    }
    // Store delay estimate in the packet, for tracing upon dequeue
    docsisItem->SetQueueDelayEstimate(delay);
    // Trace the queue delay estimate being used by the model
    m_llQueueDelayTrace(delay, item->GetSize());
    if (queueNumber == LL)
    {
        m_probNative = CalcProbNative(delay);
    }
    if (m_iaqmOn)
    {
        // Run Immediate AQM for the LL SF
        if (Iaqm(item, m_probNative))
        {
            bool marked [[maybe_unused]] = Mark(item, UNFORCED_LL_MARK);
            NS_LOG_INFO("LL packet is " << (marked ? "marked CE" : "not marked"));
        }
    }
    bool qProtectOn = false;
    if (m_queueProtection)
    {
        BooleanValue value;
        m_queueProtection->GetAttribute("QProtectOn", value);
        qProtectOn = value.Get();
    }
    QueueProtectionOutcome queueProtectionOutcome;
    if (qProtectOn)
    {
        queueProtectionOutcome = m_queueProtection->QueueProtect(item, delay, m_probNative);
    }
    if (qProtectOn && queueProtectionOutcome == QueueProtectionOutcome::SANCTION)
    {
        NS_LOG_INFO("Queue Protection has sanctioned packet to the CLASSIC queue");
        // DSCP overwrite for testing support (detection of a sanction)
        uint8_t dscp;
        if (docsisItem->GetUint8Value(QueueItem::IP_DSFIELD, dscp))
        {
            uint8_t initialDscp = dscp;
            dscp &= m_llDscpOverwrite.m_tosAndMask;
            dscp |= m_llDscpOverwrite.m_tosOrMask;
            if (dscp != initialDscp)
            {
                NS_LOG_INFO("DSCP overwrite LL packet from 0x" << std::hex << +(initialDscp >> 2)
                                                               << " to 0x" << std::hex
                                                               << +(dscp >> 2));
                bool retVal = docsisItem->SetUint8Value(QueueItem::IP_DSFIELD, dscp);
                NS_ASSERT_MSG(retVal, "Did not set DSCP value");
            }
        }
        bool classicEnqueueReturnValue = ClassicEnqueue(docsisItem);
        DecreaseVq(item->GetSize() - docsisItem->GetMacHeaderLength());
        return classicEnqueueReturnValue;
    }
    else
    {
        // Check buffer space is not exhausted
        bool isSpace = false;
        if (m_llEstimator == QDELAY_COUPLED_L)
        {
            NS_ASSERT_MSG(GetInternalQueue(LL)->GetMaxSize().GetValue() >= m_maxFrameSize,
                          "Prevent integer overflow");
            // if ( qL.byte_length() < CONFIG_BUFFER_SIZE – MAX_FRAME_SIZE ) {
            isSpace = (GetLowLatencyQueueSize(EXCLUDE_MAC_HEADERS) <
                       GetInternalQueue(LL)->GetMaxSize().GetValue() - m_maxFrameSize);
        }
        else if (m_llEstimator == QDELAY_COUPLED_V)
        {
            // if ( qdelay < (CONFIG_BUFFER_SIZE – MAX_FRAME_SIZE) * 8 / MAX_RATE) {
            int64_t delayLimitNs =
                (GetInternalQueue(LL)->GetMaxSize().GetValue() - m_maxFrameSize) * 8 * 1e9 /
                m_maxRate.GetBitRate();
            isSpace = (GetLowLatencyQueuingDelay().GetNanoSeconds() < delayLimitNs);
            NS_LOG_LOGIC(" LL enqueue isSpace " << isSpace << " qdelay "
                                                << GetLowLatencyQueuingDelay().GetNanoSeconds()
                                                << " limit " << delayLimitNs);
        }
        // qL.enqueue(packet);
        if (isSpace && GetInternalQueue(LL)->Enqueue(item))
        {
            m_llDataPduBytes += (item->GetSize() - docsisItem->GetMacHeaderLength());
            NS_LOG_INFO("LL queue enqueue successful, size " << item->GetSize() << " VQ "
                                                             << m_vq.As(Time::NS));
            m_traceLlBytes += item->GetSize();
            NS_LOG_LOGIC("Current size in LL queue "
                         << GetInternalQueue(LL)->GetCurrentSize().GetValue());
            NS_LOG_LOGIC("Current size in both queues: " << GetCurrentSize().GetValue());
            return true;
        }
        else
        {
            NS_LOG_LOGIC("isSpace " << isSpace);
            if (isSpace)
            {
                NS_LOG_LOGIC("LL queue enqueue failed; will lead to drop");
                // This branch is reached if the virtual queue estimate predicted that there
                // was space, but the actual internal LL queue was out of space.  In this case,
                // ns-3's QueueDisc::DropBeforeEnqueue is called by the internal queue because
                // QueueDisc::AddInternalQueue sets the trace callback, so there is no need
                // to call it here
            }
            else
            {
                // In this branch of code, there was no attempt to enqueue in the internal LL
                // queue, so to update the statistics, call DropBeforeEnqueue() explicitly
                NS_LOG_LOGIC("VQ estimate indicated no space; drop");
                DropBeforeEnqueue(item, "Dropped by internal queue");
            }
            DecreaseVq(item->GetSize() - docsisItem->GetMacHeaderLength());
            return false;
        }
    }
    NS_FATAL_ERROR("End of method should be unreachable");
    return false; // silence compiler warning
}

bool
DualQueueCoupledAqm::Recur(double likelihood)
{
    NS_LOG_FUNCTION(this << likelihood);
    NS_ASSERT_MSG(likelihood >= 0 && likelihood <= 1, "Failed bounds checking: " << likelihood);
    m_count += likelihood;
    if (m_count > 1)
    {
        m_count -= 1;
        return true;
    }
    else
    {
        return false;
    }
}

void
DualQueueCoupledAqm::DecreaseVq(uint32_t pSize)
{
    m_vq -= Time::FromInteger(pSize * 8 * 1e9 / m_maxRate.GetBitRate(), Time::NS);
    if (m_vq < Seconds(0))
    {
        m_vq = Seconds(0);
    }
}

void
DualQueueCoupledAqm::InitializeParams()
{
    NS_ABORT_MSG_UNLESS(m_asf || m_sf, "Service flow not configured");
    NS_ABORT_MSG_IF(m_asf && m_sf,
                    "Conflicting service flow (both ASF and single SF) configuration");
    NS_ABORT_MSG_IF(m_qDelaySingleCallback.IsNull(), "Must set qDelaySingleCallback");
    m_baseProb = 0.0;
    m_classicDropProb = 0.0;
    m_probCL = 0.0;
    m_probNative = 0.0;
    m_prevq = Time(Seconds(0));
    m_count = 0.0;
    m_updateEvent = Simulator::Schedule(m_sUpdate, &DualQueueCoupledAqm::CalculateDropProb, this);
    // DOCSIS PIE data plane
    m_burstState = NO_BURST;
    NS_ABORT_MSG_UNLESS(GetMaxSize().GetUnit() == QueueSizeUnit::BYTES,
                        "DualQ only supports byte mode");

    // Set MAX_RATE
    DataRate AMSR; // Defaults to zero
    if (m_asf)
    {
        AMSR = m_asf->m_maxSustainedRate;
    }
    DataRate MSR_L; // Defaults to zero
    if (m_asf && m_asf->GetLowLatencyServiceFlow())
    {
        MSR_L = m_asf->GetLowLatencyServiceFlow()->m_maxSustainedRate;
    }
    if ((AMSR.GetBitRate() == 0) && (MSR_L.GetBitRate() == 0))
    {
        m_maxRate = DataRate(0);
    }
    else if ((AMSR.GetBitRate() == 0) && (MSR_L.GetBitRate() != 0))
    {
        m_maxRate = MSR_L;
    }
    else if ((AMSR.GetBitRate() != 0) && (MSR_L.GetBitRate() == 0))
    {
        m_maxRate = AMSR;
    }
    else if ((AMSR.GetBitRate() != 0) && (MSR_L.GetBitRate() != 0))
    {
        m_maxRate = (AMSR < MSR_L ? AMSR : MSR_L); // min (AMSR, MSR_L)
    }

    // Adjust IAQM thresholds based on "FLOOR"
    uint32_t floorNs = 0;
    if (m_maxRate != 0)
    {
        floorNs = static_cast<uint32_t>(m_maxFrameSize * 2 * 8 * 1e9 / m_maxRate.GetBitRate());
        // Minimum marking threshold of 2 MTU for slow links
        floorNs = std::min<uint32_t>(65535000, floorNs);
    }
    NS_LOG_LOGIC("Initialize floorNs to " << floorNs << "ns");
    Time range = NanoSeconds(1 << m_lgRange);
    m_minTh = std::max<Time>((m_maxTh - range), NanoSeconds(floorNs));
    m_maxTh = m_minTh + range;
    if (m_maxTh.GetNanoSeconds() > 65535000)
    {
        m_maxTh = NanoSeconds(65535000);
    }
    if (m_llEstimator == QDELAY_COUPLED_V)
    {
        // Start a timer to schedule virtual queue drain events, if necessary
        NS_ABORT_MSG_UNLESS(m_vqInterval > Seconds(0), "VQ interval must be non-zero");
        m_alignVqTimer = Timer(Timer::CANCEL_ON_DESTROY);
        m_alignVqTimer.SetFunction(&DualQueueCoupledAqm::AlignVq, this);
        m_alignVqTimer.SetDelay(m_vqInterval);
        m_alignVqTimer.Schedule();
        // Calculations to later support VQ average allowed queue
        if (m_asf && m_asf->GetLowLatencyServiceFlow() && m_vqFramesPerMap &&
            m_vqMapInterval > Seconds(0))
        {
            uint16_t ggi = m_asf->GetLowLatencyServiceFlow()->m_guaranteedGrantInterval;
            if (ggi == 0)
            {
                ggi = m_vqMapInterval.GetMicroSeconds();
            }
            uint16_t frameIntervalUs = m_vqMapInterval.GetMicroSeconds() / m_vqFramesPerMap;
            m_pgsGrantInterval = MicroSeconds((ggi / frameIntervalUs) * frameIntervalUs);
        }
    }
}

double
DualQueueCoupledAqm::GetWeight() const
{
    NS_ASSERT_MSG(m_schedulingWeight < 256, "Scheduling weight too high");
    return static_cast<double>(m_schedulingWeight) / 256;
}

Time
DualQueueCoupledAqm::QDelayCoupledC(uint32_t byteLength)
{
    NS_LOG_FUNCTION(this << byteLength);
    // The calculation in Annex O, qdelayCoupledC produces units of s
    // In ns-3, a Time object is returned
    uint64_t AMSR = m_asf->m_maxSustainedRate.GetBitRate();
    uint64_t MSR_C = 0;
    uint64_t MSR_L = 0;
    NS_LOG_LOGIC("ASF MSR = " << m_asf->m_maxSustainedRate.GetBitRate());
    if (m_asf->GetClassicServiceFlow())
    {
        MSR_C = m_asf->GetClassicServiceFlow()->m_maxSustainedRate.GetBitRate();
    }
    if (m_asf->GetLowLatencyServiceFlow())
    {
        MSR_L = m_asf->GetLowLatencyServiceFlow()->m_maxSustainedRate.GetBitRate();
    }
    if (AMSR == 0)
    {
        return Seconds(0);
    }
    else
    {
        double r_L = GetWeight() * AMSR;
        if (MSR_L != 0)
        {
            r_L = std::min<double>(r_L, MSR_L);
        }
        r_L =
            std::min<double>((1000.0 * m_intervalBitsL) / m_interval.GetMilliSeconds(), r_L); // b/s
        double r_C = AMSR - r_L;
        if (MSR_C != 0)
        {
            r_C = std::min<double>(r_C, MSR_C);
        }
        NS_LOG_LOGIC("r_L=" << r_L << "b/s; r_C=" << r_C << "b/s; delay=" << byteLength * 8 / r_C
                            << "s");
        NS_ASSERT_MSG(r_C != 0, "Error: divide by zero");
        return Seconds(byteLength * 8 / r_C);
    }
}

// Background update, occurs every INTERVAL
void
DualQueueCoupledAqm::CalculateDropProb()
{
    NS_LOG_FUNCTION(this);
    // Derive queue delay using qdelay functions defined in Annex O.1.
    if (m_coupled)
    {
        // interval_BitsL is adjusted upon each enqueue and dequeue; no need
        // to perform the arr_byte_counter operations in the spec in this model.
        // qC.byte_length corresponds to GetClassicQueueSize (false) (i.e.
        // without taking DOCSIS MAC header bytes into consideration).
        // m_cqEstimateAtUpdate corresponds to 'qdelay' in the spec
        m_cqEstimateAtUpdate = QDelayCoupledC(GetClassicQueueSize(EXCLUDE_MAC_HEADERS));
        m_intervalBitsL = 0; // Zero counter for next interval
    }
    else
    {
        m_cqEstimateAtUpdate = m_qDelaySingleCallback();
    }

    double dropProb = m_classicDropProb; // Perform calculations on non-traced variable
    double baseProb = m_baseProb;        // Perform calculations on non-traced variable
    Time qDelay = GetClassicQueuingDelay();
    Time qDelayOld = m_prevq;
    Time target = m_latencyTarget;

    double pFormula = 0; // Variable to store p value unmodified for tracing
    if (m_burstAllowance.GetSeconds() > 0)
    {
        dropProb = 0;
        baseProb = 0;
    }
    else
    {
        NS_LOG_LOGIC("Queuing time of first-in classic packet: "
                     << qDelay.GetSeconds() << "; target " << target.GetSeconds());
        double p = m_alpha * (qDelay.GetSeconds() - target.GetSeconds()) +
                   m_beta * (qDelay.GetSeconds() - qDelayOld.GetSeconds());
        pFormula = p;
        if (m_classicDropProb < 0.000001)
        {
            // Cover extremely low drop prob scenarios
            p /= 2048;
        }
        else if (m_classicDropProb < 0.00001)
        {
            p /= 512;
        }
        else if (m_classicDropProb < 0.0001)
        {
            p /= 128;
        }
        else if (m_classicDropProb < 0.001)
        {
            p /= 32;
        }
        else if (m_classicDropProb < 0.01)
        {
            p /= 8;
        }
        else if (m_classicDropProb < 0.1)
        {
            p /= 2;
        }
        else if (m_classicDropProb < 1)
        {
            p /= 0.5;
        }
        else if (m_classicDropProb < 10)
        {
            p /= 0.125;
        }
        else
        {
            p /= 0.03125;
        }
        if ((m_classicDropProb >= 0.1) && (p > 0.02))
        {
            p = 0.02;
        }
        dropProb += p;

        if (qDelay < LATENCY_LOW && qDelayOld < LATENCY_LOW)
        {
            dropProb *= 0.98;
        }
        else if (qDelay.GetSeconds() > 0.2)
        {
            dropProb += 0.02;
        }
        dropProb = (dropProb > 0) ? dropProb : 0;
        dropProb = std::min(dropProb, m_probLow * m_meanPktSize / MIN_PKTSIZE);
        baseProb = sqrt(dropProb);
    }

    // Assign new baseProb and dropProb to traced state variables, and assign
    // any other derived values
    m_classicDropProb = dropProb;
    m_baseProb = baseProb;
    if (m_coupled)
    {
        m_probCL = std::min<double>(m_baseProb * m_couplingFactor, 1.0);
    }
    NS_LOG_LOGIC("probCL " << m_probCL << " classic drop_prob " << m_classicDropProb);

    // Handle burst allowance updates
    if (m_burstAllowance < m_interval)
    {
        m_burstAllowance = Seconds(0);
    }
    else
    {
        m_burstAllowance -= m_interval;
    }

    uint32_t burstResetLimit = m_burstResetTimeout.GetSeconds() / m_interval.GetSeconds();
    if ((qDelay.GetSeconds() < 0.5 * target.GetSeconds()) &&
        (m_prevq.GetSeconds() < (0.5 * target.GetSeconds())) && (m_classicDropProb == 0) &&
        (m_burstAllowance.GetSeconds() == 0))
    {
        if (m_burstState == IN_BURST_PROTECTING)
        {
            m_burstState = IN_BURST;
            m_burstReset = 0;
        }
        else if (m_burstState == IN_BURST)
        {
            m_burstReset++;
            if (m_burstReset > burstResetLimit)
            {
                m_burstReset = 0;
                m_burstState = NO_BURST;
            }
        }
    }
    else if (m_burstState == IN_BURST)
    {
        m_burstReset = 0;
    }

    m_prevq = qDelay;
    m_updateEvent = Simulator::Schedule(m_interval, &DualQueueCoupledAqm::CalculateDropProb, this);
    m_calculatePStateTrace(qDelay, qDelayOld, pFormula, m_classicDropProb, m_baseProb, m_probCL);
}

bool
DualQueueCoupledAqm::SelectLlQueue()
{
    NS_LOG_FUNCTION(this);
    while (GetCurrentSize().GetValue() > 0)
    {
        if (m_drrQueues.none())
        {
            NS_LOG_LOGIC("Start new round; LL deficit: " << m_llDeficit << " classic deficit: "
                                                         << m_classicDeficit);
            m_drrQueues.set(LL);
            m_drrQueues.set(CLASSIC);
            m_llDeficit += (m_drrQuantum * GetWeight());
            m_classicDeficit += m_drrQuantum;
            NS_LOG_LOGIC("Starting state: LL front, LL deficit "
                         << m_llDeficit << " classic deficit " << m_classicDeficit);
        }
        if (m_drrQueues.test(LL))
        {
            if (GetInternalQueue(LL)->Peek())
            {
                uint32_t size = GetInternalQueue(LL)->Peek()->GetSize();
                if (size <= m_llDeficit)
                {
                    NS_LOG_LOGIC("Selecting LL queue");
                    m_llDeficit -= size;
                    NS_LOG_LOGIC("State after LL selection: LL deficit << "
                                 << m_llDeficit << " classic deficit " << m_classicDeficit);
                    return true;
                }
                else
                {
                    NS_LOG_LOGIC("Not enough deficit to send LL packet, LL deficit "
                                 << m_llDeficit << " classic deficit " << m_classicDeficit);
                    m_drrQueues.reset(LL);
                    if (!GetInternalQueue(CLASSIC)->Peek())
                    {
                        NS_LOG_LOGIC("Send LL packet due to no CLASSIC packet");
                        m_llDeficit = 0;
                        return true;
                    }
                }
            }
            else
            {
                NS_LOG_LOGIC("LL has no packet, fall through to consider CLASSIC");
            }
        }
        else
        {
            if (!GetInternalQueue(CLASSIC)->Peek())
            {
                NS_LOG_LOGIC("Send LL packet due to no CLASSIC packet");
                m_llDeficit = 0;
                return true;
            }
        }
        if (m_drrQueues.test(CLASSIC) || GetInternalQueue(CLASSIC)->Peek())
        {
            if (GetInternalQueue(CLASSIC)->Peek())
            {
                uint32_t size = GetInternalQueue(CLASSIC)->Peek()->GetSize();
                if (size <= m_classicDeficit)
                {
                    NS_LOG_LOGIC("Selecting Classic queue");
                    m_classicDeficit -= size;
                    NS_LOG_LOGIC("State after Classic selection: LL deficit << "
                                 << m_llDeficit << " classic deficit " << m_classicDeficit);
                    return false;
                }
                else if (size)
                {
                    NS_LOG_LOGIC("Not enough deficit to send Classic packet");
                    m_drrQueues.reset(CLASSIC);
                    if (!GetInternalQueue(LL)->Peek())
                    {
                        // Send anyway since there is no LL packet
                        NS_LOG_LOGIC("Send CLASSIC packet due to no LL packet");
                        m_classicDeficit = 0;
                        return false;
                    }
                    // Fall through to return to top of loop and reset
                }
            }
            else
            {
                // no classic packet either; will fall through and reset above
            }
        }
    }
    return false; // Unreachable
}

Ptr<QueueDiscItem>
DualQueueCoupledAqm::LowLatencyDequeue()
{
    NS_LOG_FUNCTION(this);
    Ptr<QueueDiscItem> item = GetInternalQueue(LL)->Dequeue();
    Ptr<DocsisQueueDiscItem> docsisItem = DynamicCast<DocsisQueueDiscItem>(item);
    if (!item)
    {
        NS_LOG_LOGIC("Failed to dequeue from LL queue");
        return nullptr;
    }
    NS_LOG_INFO("Dequeue from LL queue, packet with sojourn time "
                << (Simulator::Now() - item->GetTimeStamp()).GetSeconds() * 1000 << " ms");
    if (m_traceLlBytes >= item->GetSize())
    {
        m_traceLlBytes -= item->GetSize();
        m_intervalBitsL += 8 * (item->GetSize() - docsisItem->GetMacHeaderLength());
    }
    else
    {
        m_traceLlBytes = 0;
        NS_ASSERT_MSG(GetInternalQueue(LL)->GetNPackets() == 0, "Queue accounting error");
    }
    if (m_llDataPduBytes >= (item->GetSize() - docsisItem->GetMacHeaderLength()))
    {
        m_llDataPduBytes -= (item->GetSize() - docsisItem->GetMacHeaderLength());
    }
    else
    {
        m_llDataPduBytes = 0;
        NS_ASSERT_MSG(GetInternalQueue(LL)->GetNPackets() == 0, "Queue accounting error");
    }

    m_traceLlSojourn(Simulator::Now() - item->GetTimeStamp());
    // Trace the sojourn time, original standard and virtual queue delay
    // estimates, current classic queueing delay, whether marked, and packet size
    m_llDequeueTrace(Simulator::Now() - item->GetTimeStamp(),
                     docsisItem->GetQueueDelayEstimate(),
                     GetClassicQueuingDelay(),
                     docsisItem->IsMarked(),
                     item->GetSize() - docsisItem->GetMacHeaderLength());
    return item;
}

Ptr<QueueDiscItem>
DualQueueCoupledAqm::ClassicDequeue()
{
    NS_LOG_FUNCTION(this);
    // while loop accounts for possibility of a drop in the below
    while (GetCurrentSize().GetValue() > 0)
    {
        NS_LOG_INFO("Dequeue from Classic queue, classic drop prob: " << m_classicDropProb);
        Ptr<QueueDiscItem> item = GetInternalQueue(CLASSIC)->Dequeue();
        Ptr<DocsisQueueDiscItem> docsisItem = DynamicCast<DocsisQueueDiscItem>(item);
        if (!item)
        {
            NS_LOG_WARN("Failed to dequeue from classic queue");
            return nullptr;
        }
        if (m_traceClassicBytes >= item->GetSize())
        {
            m_traceClassicBytes -= item->GetSize();
            // To access GetMacHeaderLength (), we must use docsisItem
            m_tracePieQueueBytes -= (item->GetSize() - docsisItem->GetMacHeaderLength());
        }
        else
        {
            m_traceClassicBytes = 0;
            m_tracePieQueueBytes = 0;
        }
        m_traceClassicSojourn(Simulator::Now() - item->GetTimeStamp());
        return item;
    }
    return nullptr;
}

Ptr<QueueDiscItem>
DualQueueCoupledAqm::DoDequeue()
{
    NS_LOG_FUNCTION(this);
    if (SelectLlQueue())
    {
        return LowLatencyDequeue();
    }
    else
    {
        return ClassicDequeue();
    }
}

Ptr<const QueueDiscItem>
DualQueueCoupledAqm::DoPeek()
{
    // The QueueDisc::Peek() operation causes the DualQueueCoupledAqm() to
    // dequeue a packet from either the internal classic or ll queue.  This
    // packet then sits on the private m_requeue pointer internally.  Because
    // all dequeue operations by the CmNetDevice (when using the
    // CmtsUpstreamScheduler) are done directly on the internal queues, the
    // packet that may be sitting on the m_requeue pointer would be bypassed.
    // The below statement ensures that users do not try to Peek() on this
    // queue model.
    NS_FATAL_ERROR("DualQueueCoupledAqm not designed to support Peek ()");
    return nullptr;
}

bool
DualQueueCoupledAqm::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_FATAL_ERROR("DualQueueCoupledAqm cannot have classes");
        return false;
    }

    if (GetNPacketFilters() == 0)
    {
        NS_FATAL_ERROR("DualQueueCoupledAqm requires installation of at least one PacketFilter");
        return false;
    }

    // It is possible to configure the internal queues prior to this
    // initialization method, but we disallow it so that we can ensure that
    // the queues are properly sized based on the service flow configuration.
    if (GetNInternalQueues() > 0)
    {
        NS_FATAL_ERROR(
            "Internal queue configuration should be deferred until CheckConfig() is called");
        return false;
    }

    NS_LOG_LOGIC("Creating internal queues");
    // The following is used to determine the rate 'D' in C.1.2.17 equation 4
    // for the creation of the classic service flow queue
    uint64_t dBps = 0; // D in Bytes per sec
    if (m_asf)
    {
        Ptr<const ServiceFlow> sf = m_asf->GetClassicServiceFlow();
        if (!sf || (sf->m_maxSustainedRate.GetBitRate() == 0 && sf->m_peakRate.GetBitRate() == 0))
        {
            dBps = m_asf->m_maxSustainedRate.GetBitRate() / 8;
        }
        else if (sf->m_peakRate > sf->m_maxSustainedRate)
        {
            dBps = sf->m_peakRate.GetBitRate() / 8;
        }
        else
        {
            dBps = sf->m_maxSustainedRate.GetBitRate() / 8;
        }
    }
    else if (m_sf)
    {
        if (m_sf->m_peakRate > m_sf->m_maxSustainedRate)
        {
            dBps = m_sf->m_peakRate.GetBitRate() / 8;
        }
        else
        {
            dBps = m_sf->m_maxSustainedRate.GetBitRate() / 8;
        }
    }
    // For simplicity, two service flow queues are always created, regardless
    // of the number of active service flows (one or two).
    if (m_classicBufferSize == QueueSize("0B"))
    {
        // See C.1.2.17 Default Upstream Target Buffer, equation (4)
        uint32_t classicSfBytes = DEFAULT_UPSTREAM_TARGET_BUFFER_CONFIGURATION.GetSeconds() * dBps;
        NS_LOG_LOGIC("Setting classic queue size to default value of " << classicSfBytes
                                                                       << " bytes");
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize",
            QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, classicSfBytes))));
    }
    else
    {
        NS_LOG_LOGIC("Setting classic queue size to configured value of " << m_classicBufferSize
                                                                          << " bytes");
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize",
            QueueSizeValue(m_classicBufferSize)));
    }
    if (m_lowLatencyConfigBufferSize == QueueSize("0B"))
    {
        uint64_t amsrBps = 0;
        if (m_asf)
        {
            // See C.2.2.7.11.4 Target Buffer equation (3).
            amsrBps = m_asf->m_maxSustainedRate.GetBitRate() / 8; // bytes/s
        }
        uint32_t llSfBytes =
            std::max<uint32_t>((DEFAULT_LOW_LATENCY_TARGET_BUFFER.GetSeconds() * amsrBps),
                               (20 * m_maxFrameSize));
        NS_LOG_LOGIC("Setting low latency queue size to default value of " << llSfBytes
                                                                           << " bytes");
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize",
            QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, llSfBytes))));
    }
    else
    {
        NS_LOG_LOGIC("Setting low latency queue size to configured value of "
                     << m_lowLatencyConfigBufferSize << " bytes");
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize",
            QueueSizeValue(m_lowLatencyConfigBufferSize)));
    }

    return true;
}

bool
DualQueueCoupledAqm::DropEarly(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    if (m_burstAllowance.GetSeconds() > 0)
    {
        // If there is still burst_allowance left, skip random early drop.
        return false;
    }

    if (m_classicDropProb == 0)
    {
        m_accuProb = 0;
    }

    if (m_burstState == NO_BURST)
    {
        if (GetClassicQueueSize(EXCLUDE_MAC_HEADERS) <
            GetInternalQueue(CLASSIC)->GetMaxSize().GetValue() / 3)
        {
            return false;
        }
        else
        {
            m_burstState = IN_BURST;
        }
    }

    double p = m_classicDropProb;

    uint32_t packetSize = item->GetSize();

    p = p * packetSize / m_meanPktSize;
    p = std::min(p, m_probLow);
    m_accuProb += p;

    bool earlyDrop = true;
    double u = m_uv->GetValue();

    if ((m_prevq.GetSeconds() < (0.5 * m_latencyTarget.GetSeconds())) && (m_classicDropProb < 0.2))
    {
        return false;
    }
    else if (GetClassicQueueSize(EXCLUDE_MAC_HEADERS) <= 2 * m_meanPktSize)
    {
        return false;
    }

    if (m_accuProb < m_probLow)
    {
        // Avoid dropping too fast due to bad luck of coin tosses
        earlyDrop = false;
    }
    else if (m_accuProb >= m_probHigh)
    {
        // Avoid dropping too slow due to bad luck of coin tosses
        earlyDrop = true;
    }
    else
    {
        if (u > p)
        {
            earlyDrop = false;
        }
    }

    if (!earlyDrop)
    {
        return false;
    }
    m_accuProb = 0;
    if (m_burstState == IN_BURST)
    {
        m_burstState = IN_BURST_PROTECTING;
        m_burstAllowance = m_maxBurst;
    }
    return true;
}

void
DualQueueCoupledAqm::SetLowLatencyDscpOverwrite(DscpOverwrite overwrite)
{
    NS_LOG_FUNCTION(this << +overwrite.m_tosAndMask << +overwrite.m_tosOrMask);
    NS_ABORT_MSG_IF((overwrite.m_tosAndMask & 0x03) != 0x03,
                    "tos-and-mask should not have either of two LS bits set to zero");
    NS_ABORT_MSG_IF(overwrite.m_tosOrMask & 0x03,
                    "tos-or-mask should not have either of two LS bits set to one");
    m_llDscpOverwrite = overwrite;
}

DscpOverwrite
DualQueueCoupledAqm::GetLowLatencyDscpOverwrite() const
{
    return m_llDscpOverwrite;
}

void
DualQueueCoupledAqm::SetClassicDscpOverwrite(DscpOverwrite overwrite)
{
    NS_LOG_FUNCTION(this << +overwrite.m_tosAndMask << +overwrite.m_tosOrMask);
    NS_ABORT_MSG_IF((overwrite.m_tosAndMask & 0x0003) != 0x0003,
                    "tos-and-mask should not have either of two LS bits set to zero");
    NS_ABORT_MSG_IF(overwrite.m_tosOrMask & 0x0003,
                    "tos-or-mask should not have either of two LS bits set to one");
    m_classicDscpOverwrite = overwrite;
}

DscpOverwrite
DualQueueCoupledAqm::GetClassicDscpOverwrite() const
{
    return m_classicDscpOverwrite;
}

} // namespace docsis
} // namespace ns3
