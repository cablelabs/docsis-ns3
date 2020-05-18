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

#include <algorithm>
#include "math.h"
#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "ns3/object-factory.h"
#include "ns3/string.h"
#include "ns3/ipv4-header.h"
#include "ns3/data-rate.h"
#include "ns3/drop-tail-queue.h"
#include "dual-queue-coupled-aqm.h"
#include "docsis-queue-disc-item.h"
#include "queue-protection.h"

namespace ns3 {
namespace docsis {

// Constants in use for the DOCSIS mode
static const uint32_t MIN_PKTSIZE = 64;
static const Time LATENCY_LOW = MilliSeconds (5);
static const Time CLASSIC_TARGET_BUFFER = MilliSeconds (100);

// Spec says impl dependent between min and max...
// must be at least 50ms
Time classicQueueTarget = MilliSeconds (100);

NS_LOG_COMPONENT_DEFINE ("DualQueueCoupledAqm");

NS_OBJECT_ENSURE_REGISTERED (DualQueueCoupledAqm);

TypeId DualQueueCoupledAqm::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::DualQueueCoupledAqm")
    .SetParent<QueueDisc> ()
    .SetGroupName ("Docsis")
    .AddConstructor<DualQueueCoupledAqm> ()
    .AddAttribute ("A",
                   "Value of alpha (Hz^2)",
                   DoubleValue (0.25),
                   MakeDoubleAccessor (&DualQueueCoupledAqm::m_alpha),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("B",
                   "Value of beta (Hz^2)",
                   DoubleValue (2.5),
                   MakeDoubleAccessor (&DualQueueCoupledAqm::m_beta),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Interval",
                   "Sample interval in the Classic queue control path",
                   TimeValue (MilliSeconds (16)),
                   MakeTimeAccessor (&DualQueueCoupledAqm::m_interval),
                   MakeTimeChecker ())
    .AddAttribute ("Supdate",
                   "Simulation start time of the Classic queue update process",
                   TimeValue (Seconds (0.0)),
                   MakeTimeAccessor (&DualQueueCoupledAqm::m_sUpdate),
                   MakeTimeChecker ())
    .AddAttribute ("MaxSize",
                   "The maximum number of bytes accepted by this queue",
                   QueueSizeValue (QueueSize ("200MB")), // 250 ms @ 640 Gb/s
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
    .AddAttribute ("ClassicBufferSize",
                   "The maximum number of bytes accepted by the classic queue",
                   QueueSizeValue (QueueSize ("0B")), // default to 100ms @ AMSR
                   MakeQueueSizeAccessor (&DualQueueCoupledAqm::m_classicBufferSize),
                   MakeQueueSizeChecker ())
    .AddAttribute ("LowLatencyBufferSize",
                   "The maximum number of bytes accepted by the low latency queue",
                   QueueSizeValue (QueueSize ("0B")), // default to 10ms @ AMSR
                   MakeQueueSizeAccessor (&DualQueueCoupledAqm::m_lowLatencyBufferSize),
                   MakeQueueSizeChecker ())
    .AddAttribute ("ClassicAqmLatencyTarget",
                   "Target queue delay of Classic traffic (C.2.2.7.15.2)",
                   TimeValue (MilliSeconds (10)),
                   MakeTimeAccessor (&DualQueueCoupledAqm::m_latencyTarget),
                   MakeTimeChecker ())
    .AddAttribute ("LgRange",
                   "Log2(range) of the range of IAQM ramp",
                   UintegerValue (19),
                   MakeUintegerAccessor (&DualQueueCoupledAqm::m_lgRange),
                   MakeUintegerChecker<uint16_t> (0,25))
    .AddAttribute ("MinTh",
                   "MINTH threshold for immediate AQM ramp function",
                   TypeId::ATTR_GET,
                   TimeValue (Seconds (0)), //this value is ignored because there is no setter (i.e. read-only)
                   MakeTimeAccessor (&DualQueueCoupledAqm::GetMinTh),
                   MakeTimeChecker ())
    .AddAttribute ("MaxTh",
                   "MAXTH threshold for immediate AQM ramp function",
                   TimeValue (MicroSeconds (1000)),
                   MakeTimeAccessor (&DualQueueCoupledAqm::m_maxTh),
                   MakeTimeChecker ())
    .AddAttribute ("Coupled",
                   "Indicates whether the PIE AQM is part of a Coupled DualQ",
                   BooleanValue (true),
                   MakeBooleanAccessor (&DualQueueCoupledAqm::m_coupled),
                   MakeBooleanChecker ())
    .AddAttribute ("CouplingFactor",
                   "Coupling factor (1/10 of the integer AQM Coupling Factor)",
                   DoubleValue (2),
                   MakeDoubleAccessor (&DualQueueCoupledAqm::m_couplingFactor),
                   MakeDoubleChecker<double> (0, 25.5))
    .AddAttribute ("SchedulingWeight",
                   "Weight (maximum of 255) used for DRR LL queue scheduling",
                   UintegerValue (230),
                   MakeUintegerAccessor (&DualQueueCoupledAqm::m_schedulingWeight),
                   MakeUintegerChecker<uint32_t> (1,255))
    .AddAttribute ("DrrQuantum",
                   "Quantum used in weighted DRR policy (bytes)",
                   UintegerValue (1500),
                   MakeUintegerAccessor (&DualQueueCoupledAqm::m_drrQuantum),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Amsr", "Maximum sustained rate of the aggregate SF",
                   DataRateValue (DataRate ("50Mbps")),
                   MakeDataRateAccessor (&DualQueueCoupledAqm::m_amsr),
                   MakeDataRateChecker ())
    .AddAttribute ("MaxFrameSize",
                   "MAX_FRAME_SIZE constant used to set FLOOR (bytes)",
                   UintegerValue (2000),
                   MakeUintegerAccessor (&DualQueueCoupledAqm::m_maxFrameSize),
                   MakeUintegerChecker<uint32_t> ())
    // DOCSIS PIE data path
    .AddAttribute ("MeanPktSize",
                   "Average of packet size",
                   UintegerValue (1024),
                   MakeUintegerAccessor (&DualQueueCoupledAqm::m_meanPktSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxBurstAllowance",
                   "Current max burst allowance in seconds before random drop",
                   TimeValue (Seconds (0.142)),
                   MakeTimeAccessor (&DualQueueCoupledAqm::m_maxBurst),
                   MakeTimeChecker ())
    .AddAttribute ("BurstResetTimeout",
                   "Time to wait before transitioning to INACTIVE",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&DualQueueCoupledAqm::m_burstResetTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("ProbLow",
                   "PROB_LOW from DOCSIS PIE specification",
                   DoubleValue (0.85),
                   MakeDoubleAccessor (&DualQueueCoupledAqm::m_probLow),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("ProbHigh",
                   "PROB_HIGH from DOCSIS PIE specification",
                   DoubleValue (8.5),
                   MakeDoubleAccessor (&DualQueueCoupledAqm::m_probHigh),
                   MakeDoubleChecker<double> ())
    .AddTraceSource ("ClassicBytes",
                     "Bytes in Classic queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_traceClassicBytes),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("LowLatencyBytes",
                     "Bytes in Low Latency queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_traceLlBytes),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("ClassicSojournTime",
                     "Sojourn time of the last packet dequeued from the Classic queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_traceClassicSojourn),
                     "ns3::Time::TracedCallback")
    .AddTraceSource ("LowLatencySojournTime",
                     "Sojourn time of the last packet dequeued from the LL queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_traceLlSojourn),
                     "ns3::Time::TracedCallback")
    .AddTraceSource ("ClassicDropProbability",
                     "Classic drop probability",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_classicDropProb),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("ProbCL",
                     "Coupled marking probability from Classic queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_probCL),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("ProbNative",
                     "Current native marking probability of LL queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_probNative),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("BaseProbability",
                     "Base probability",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_baseProb),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("EnqueueState",
                     "State of queue at packet enqueue time",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_enqueueStateTrace),
                     "ns3::TracedValueCallback::DualQEnqueueStateTracedCallback")
    .AddTraceSource ("CalculatePState",
                     "State of variables at CalculateP events",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_calculatePStateTrace),
                     "ns3::TracedValueCallback::CalculatePStateTracedCallback")
    .AddTraceSource ("LowLatencyQueueDelay",
                     "State of LL queue delay estimate when queried",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_llQueueDelayTrace),
                     "ns3::TracedValueCallback::DualQLlQueueDelayTracedCallback")
    .AddTraceSource ("LowLatencyArrival",
                     "Notify the arrival of a packet classified to the L queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_llArrivalTrace),
                     "ns3::TracedValue::Uint32Callback")
    .AddTraceSource ("ClassicArrival",
                     "Notify the arrival of a packet classified to the C queue",
                     MakeTraceSourceAccessor (&DualQueueCoupledAqm::m_classicArrivalTrace),
                     "ns3::TracedValue::Uint32Callback")
  ;

  return tid;
}

DualQueueCoupledAqm::DualQueueCoupledAqm ()
  : QueueDisc (QueueDiscSizePolicy::MULTIPLE_QUEUES),
    m_classicDeficit (0),
    m_llDeficit (0),
    m_bytesArrivingAtL (0),
    m_cqEstimateAtUpdate (Seconds (0))
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
  m_qDelaySingleCallback = MakeNullCallback<Time> ();
}

DualQueueCoupledAqm::~DualQueueCoupledAqm ()
{
  NS_LOG_FUNCTION (this);
}

void
DualQueueCoupledAqm::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_uv = 0;
  if (m_queueProtection)
    {
      m_queueProtection->Dispose ();
      m_queueProtection = 0;
    }
  Simulator::Remove (m_updateEvent);
  m_qDelaySingleCallback = MakeNullCallback<Time> ();
  QueueDisc::DoDispose ();
}

void
DualQueueCoupledAqm::SetQueueProtection (Ptr<QueueProtection> qp)
{
  NS_LOG_FUNCTION (this << qp);
  m_queueProtection = qp;
}

Ptr<QueueProtection>
DualQueueCoupledAqm::GetQueueProtection (void) const
{
  return m_queueProtection;
}

void
DualQueueCoupledAqm::SetQDelaySingleCallback (Callback<Time> qDelaySingleCallback)
{
  NS_LOG_FUNCTION (this);
  m_qDelaySingleCallback = qDelaySingleCallback;
}

uint32_t
DualQueueCoupledAqm::GetLowLatencyQueueSize (void) const
{
  NS_LOG_FUNCTION (this);
  return GetInternalQueue (LL)->GetCurrentSize ().GetValue ();
}

uint32_t
DualQueueCoupledAqm::GetClassicQueueSize (void) const
{
  NS_LOG_FUNCTION (this);
  return GetInternalQueue (CLASSIC)->GetCurrentSize ().GetValue ();
}

Time
DualQueueCoupledAqm::GetClassicQueuingDelay (uint32_t size) const
{
  Ptr<const QueueDiscItem> item;
  return m_cqEstimateAtUpdate;
}

Time
DualQueueCoupledAqm::GetLowLatencyQueuingDelay (void) const
{
  return QDelayCoupledL (GetLowLatencyQueueSize ());
}

Time
DualQueueCoupledAqm::QDelayCoupledL (uint32_t byteLength) const
{
  NS_LOG_FUNCTION (this << byteLength);
  return Seconds (static_cast<double> (byteLength * 8) / m_amsr.GetBitRate ());
}

double
DualQueueCoupledAqm::GetClassicDropProbability (void) const
{
  NS_LOG_FUNCTION (this);
  return m_classicDropProb;
}

double
DualQueueCoupledAqm::CalcProbNative (void) const
{
  NS_LOG_FUNCTION (this);
  Time qDelay = QDelayCoupledL (GetLowLatencyQueueSize ());
  return CalcProbNative (qDelay);
}

double
DualQueueCoupledAqm::CalcProbNative (Time qDelay) const
{
  NS_LOG_FUNCTION (this << qDelay);
  // Set LL mark probability to the max of the internal AQM probability
  // (defined as a ramp from MinTh to MinTh + Range)
  // and the product of the coupling factor and the base probability
  double probNative = 0;
  if (qDelay >= GetMaxTh ())
    {
      probNative = 1;
    }
  else if (qDelay > GetMinTh ())
    {
      probNative = (qDelay - GetMinTh ()).GetSeconds () /
                 (GetMaxTh () - GetMinTh ()).GetSeconds ();
    }
  NS_LOG_DEBUG ("LL mark probability due to internal AQM: " << probNative);
  NS_ABORT_MSG_IF (probNative > 1, "Check for an invalid value");
  return probNative;
}

double
DualQueueCoupledAqm::GetProbCL (void) const
{
  NS_LOG_FUNCTION (this);
  return m_probCL;
}

Time
DualQueueCoupledAqm::GetMinTh (void) const
{
  return m_minTh;
}

Time
DualQueueCoupledAqm::GetMaxTh (void) const
{
  return m_maxTh;
}

int64_t
DualQueueCoupledAqm::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uv->SetStream (stream);
  return 1;
}

bool
DualQueueCoupledAqm::Iaqm (Ptr<QueueDiscItem> item, uint32_t byteLength)
{
  NS_LOG_FUNCTION (this);
  bool retval = false;
  Ptr<DocsisQueueDiscItem> docsisItem = DynamicCast<DocsisQueueDiscItem> (item);
  NS_ASSERT_MSG (docsisItem, "DocsisQueueDiscItem not found");
  Time delay = QDelayCoupledL (GetLowLatencyQueueSize () + item->GetSize ());
  m_llQueueDelayTrace (delay, item->GetSize ());
  if (item->GetProtocol () == 0x0800)
    {
      Ipv4Header ipv4Header;
      Ptr<Packet> pkt = item->GetPacket ();
      pkt->PeekHeader (ipv4Header);
      // TODO:  Rewrite below as (ecn_ & L4S_MASK)
      if (ipv4Header.GetEcn () == Ipv4Header::ECN_CE
          || ipv4Header.GetEcn () == Ipv4Header::ECN_ECT1)
        {
          double probNative = CalcProbNative (delay);
          if (m_probNative != probNative)
            {
              m_probNative = probNative; // Update traced variable
            }
          // Combine Native and Coupled probabilities into ECN marking probL
          double probL = std::max<double> (probNative, std::min<double> (m_probCL, 1));
          NS_LOG_DEBUG ("Delay " << delay << " probNative " << probNative);
          if (Recur (probL))
            {
              bool markRetval = Mark (item, UNFORCED_LL_MARK);
              NS_LOG_DEBUG ("Marking outcome in LL queue " << markRetval);
              NS_UNUSED (markRetval);  // Avoid warning of unused variable
            }
        }
      else
        {
          ; // NO OP for ECN_NotECT and ECN_ECT0
        }
    }
  uint8_t initialDscp, dscp;
  bool retVal = docsisItem->GetUint8Value (QueueItem::IP_DSFIELD, dscp);
  if (retVal)
    {
      initialDscp = dscp;
      dscp &= m_llDscpOverwrite.m_tosAndMask;
      dscp |= m_llDscpOverwrite.m_tosOrMask;
      if (dscp != initialDscp)
        {
          NS_LOG_DEBUG ("DSCP overwrite LL packet from 0x" << std::hex << +(initialDscp >> 2) << " to 0x" << std::hex << +(dscp >> 2));
          retVal = docsisItem->SetUint8Value (QueueItem::IP_DSFIELD, dscp);
          NS_ASSERT_MSG (retVal, "Did not set DSCP value");
        }
    }
  retval = GetInternalQueue (LL)->Enqueue (item);
  if (retval)
    {
      NS_LOG_DEBUG ("LL queue enqueue successful, size " << item->GetSize () << " IAQM_L_Delay " << delay );
      m_bytesArrivingAtL += item->GetSize ();
      m_traceLlBytes += item->GetSize ();
    }
  else
    {
      NS_LOG_DEBUG ("Drop due to LL queue enqueue not succeeding, Delay " << delay );
      // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called
      // by the internal queue because QueueDisc::AddInternalQueue
      // sets the trace callback, so no need to call it here
      return false;
    }
  return true;
}

bool
DualQueueCoupledAqm::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  int32_t queueNumber;
  Ptr<DocsisQueueDiscItem> docsisItem = DynamicCast<DocsisQueueDiscItem> (item);
  NS_ASSERT_MSG (docsisItem, "DocsisQueueDiscItem not found");
  
  QueueSize nQueued = GetCurrentSize ();
  Time qDelay = GetClassicQueuingDelay();
  m_enqueueStateTrace (qDelay, nQueued.GetValue (), m_classicDropProb, m_burstReset, m_burstState);
  if (nQueued + item > GetMaxSize ())
    {
      // Drops due to queue limit
      NS_LOG_DEBUG ("Drop due to overall queue limit exceeded");
      DropBeforeEnqueue (item, FORCED_DROP);
      return false;
    }
  else
    {
      // For further study; insert checks for starvation here
      queueNumber = Classify (item); 
      if (queueNumber == LL)
        {
          m_llArrivalTrace (item->GetSize ());
        }
      else if (queueNumber == CLASSIC)
        {
          m_classicArrivalTrace (item->GetSize ());
        }
      if (queueNumber == LL && m_queueProtection)
        {
          QueueProtectionOutcome queueProtectionOutcome = m_queueProtection->QueueProtect (item);
          if (queueProtectionOutcome == QueueProtectionOutcome::SANCTION)
            {
              NS_LOG_DEBUG ("Queue Protection has sanctioned packet");
              queueNumber = CLASSIC;
            }
           else
            {
               NS_LOG_DEBUG ("Queue Protection has not sanctioned packet");
            }
        }
      if (queueNumber == PacketFilter::PF_NO_MATCH)
        {
          NS_LOG_DEBUG ("No filter has been able to classify this packet; place into CLASSIC queue");
          queueNumber = CLASSIC;
        }
      NS_ABORT_MSG_UNLESS (queueNumber == CLASSIC || queueNumber == LL, "Configuration error on queue classification " << queueNumber);
    }

  bool retval = false;
  if (queueNumber == CLASSIC)
    {
      if (DropEarly (item))
        {
          // Early probability drop: proactive
          NS_LOG_DEBUG ("EarlyDrop (DOCSISmode) classic queue; C_Delay "<< qDelay);
          DropBeforeEnqueue (item, UNFORCED_CLASSIC_DROP);
          return false;
        }
      uint8_t initialDscp, dscp;
      bool retVal = docsisItem->GetUint8Value (QueueItem::IP_DSFIELD, dscp);
      if (retVal) 
        {
          initialDscp = dscp;
          dscp &= m_classicDscpOverwrite.m_tosAndMask;
          dscp |= m_classicDscpOverwrite.m_tosOrMask;
          if (dscp != initialDscp)
            {
              NS_LOG_DEBUG ("DSCP overwrite classic packet from 0x" << std::hex << +(initialDscp >> 2) << " to 0x" << std::hex << +(dscp >> 2));
              retVal = docsisItem->SetUint8Value (QueueItem::IP_DSFIELD, dscp);
              NS_ASSERT_MSG (retVal, "Did not set DSCP value");
            }
        }
      retval = GetInternalQueue (CLASSIC)->Enqueue (item);
      if (retval)
        {
          NS_LOG_DEBUG ("Classic queue enqueue successful; C_Delay "<< qDelay);
          m_traceClassicBytes += item->GetSize ();
        }
      else
        {
          NS_LOG_DEBUG ("Drop ClassicQueue enqueue not succeeding; C_Delay "<< qDelay);
          // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called 
          // by the internal queue because QueueDisc::AddInternalQueue 
          // sets the trace callback, so no need to call it here
          return false;
        }
    }
  else
    {

      retval = Iaqm (item, GetLowLatencyQueueSize ());

    }
  NS_LOG_DEBUG ("Current size in queue-number " << queueNumber << ": " << GetInternalQueue (queueNumber)->GetCurrentSize ().GetValue ());
  NS_LOG_DEBUG ("Current size in both queues: " << GetCurrentSize ().GetValue ());
  return retval;
}

bool
DualQueueCoupledAqm::Recur (double likelihood)
{
  NS_LOG_FUNCTION (this << likelihood);
  NS_ASSERT_MSG (likelihood >= 0 && likelihood <=1, "Failed bounds checking: " << likelihood);
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
DualQueueCoupledAqm::InitializeParams (void)
{
  NS_ABORT_MSG_UNLESS (m_amsr.GetBitRate () > 0, "Invalid AMSR configuration");
  NS_ABORT_MSG_IF (m_qDelaySingleCallback.IsNull (), "Must set qDelaySingleCallback");
  m_baseProb = 0.0;
  m_classicDropProb = 0.0;
  m_probCL = 0.0;
  m_probNative = 0.0;
  m_prevq = Time (Seconds (0));
  m_count=0.0;
  m_updateEvent = Simulator::Schedule (m_sUpdate, &DualQueueCoupledAqm::CalculateDropProb, this);
  // DOCSIS PIE data plane
  m_burstState = NO_BURST;
  NS_ABORT_MSG_UNLESS (GetMaxSize ().GetUnit () == QueueSizeUnit::BYTES, "DualQ only supports byte mode");

  // Adjust IAQM thresholds based on "FLOOR"
  uint32_t floorNs = static_cast<uint32_t> (m_maxFrameSize * 2 * 8 * 1e9 / m_amsr.GetBitRate ());
  m_minTh = std::max<Time> (m_maxTh - NanoSeconds (1 << m_lgRange), NanoSeconds (floorNs));
  m_maxTh = m_minTh + NanoSeconds (1 << m_lgRange);

}

double
DualQueueCoupledAqm::GetWeight (void) const
{
  NS_ASSERT_MSG (m_schedulingWeight < 256, "Scheduling weight too high");
  return static_cast<double> (m_schedulingWeight) / 256;
}

Time
DualQueueCoupledAqm::QDelayCoupledC (uint32_t byteLength)
{
  NS_LOG_FUNCTION (this << byteLength);
  double r_L = byteLength * 8 / m_interval.GetSeconds ();
  r_L = std::min<double> (r_L, GetWeight () * m_amsr.GetBitRate ());
  NS_ASSERT_MSG (m_amsr.GetBitRate () > r_L, "Error in delay estimate: " << m_amsr << r_L);
  return Seconds (GetClassicQueueSize () * 8 / (m_amsr.GetBitRate () - r_L));
}

// The DOCSIS PIE update algorithm (called once every interval)
// The PI2 base probability is obtained by the square root of DOCSIS prob
void
DualQueueCoupledAqm::CalculateDropProb (void)
{
  NS_LOG_FUNCTION (this);
  if (m_coupled)
    {
      m_cqEstimateAtUpdate = QDelayCoupledC (m_bytesArrivingAtL); // perform before assigning qDelay below
    }
  else
    {
      m_cqEstimateAtUpdate = m_qDelaySingleCallback ();
    }
  m_bytesArrivingAtL = 0;  // Zero counter for next interval

  double dropProb = m_classicDropProb;  // Perform calculations on non-traced variable
  double baseProb = m_baseProb;  // Perform calculations on non-traced variable
  Time qDelay = GetClassicQueuingDelay ();
  Time qDelayOld = m_prevq;
  Time target = m_latencyTarget;

  double pFormula = 0;  // Variable to store p value unmodified for tracing
  if (m_burstAllowance.GetSeconds () > 0)
    {
      dropProb = 0;
      baseProb = 0;
    }
  else
    {
      NS_LOG_DEBUG ("Queuing time of first-in classic packet: " << qDelay.GetSeconds () << "; target " << target.GetSeconds ());
      double p = m_alpha * (qDelay.GetSeconds () - target.GetSeconds ()) +
    m_beta * (qDelay.GetSeconds () - qDelayOld.GetSeconds ());
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
      else if (qDelay.GetSeconds () > 0.2)
        {
          dropProb += 0.02;
        }
      dropProb = (dropProb > 0) ? dropProb : 0;
      dropProb = std::min (dropProb, m_probLow * m_meanPktSize / MIN_PKTSIZE);
      baseProb = sqrt (dropProb);
    }

  // Assign new baseProb and dropProb to traced state variables, and assign
  // any other derived values
  m_classicDropProb = dropProb;
  m_baseProb = baseProb;
  if (m_coupled)
    {
      m_probCL = std::min<double> (m_baseProb * m_couplingFactor, 1.0);
    }
  NS_LOG_DEBUG ("probCL " << m_probCL << " classic drop_prob " << m_classicDropProb);

  // Handle burst allowance updates
  if (m_burstAllowance < m_interval)
    {
      m_burstAllowance = Seconds (0);
    }
  else
    {
      m_burstAllowance -= m_interval;
    }

  uint32_t burstResetLimit = m_burstResetTimeout.GetSeconds () / m_interval.GetSeconds ();
  if ( (qDelay.GetSeconds () < 0.5 * target.GetSeconds ()) && (m_prevq.GetSeconds () < (0.5 * target.GetSeconds ())) && (m_classicDropProb == 0) && (m_burstAllowance.GetSeconds () == 0))
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
  m_updateEvent = Simulator::Schedule (m_interval, &DualQueueCoupledAqm::CalculateDropProb, this);
  m_calculatePStateTrace (qDelay, qDelayOld, pFormula, m_classicDropProb, m_baseProb, m_probCL); 
}

bool
DualQueueCoupledAqm::SelectLlQueue (void)
{
  NS_LOG_FUNCTION (this);
  while (GetCurrentSize ().GetValue () > 0)
    {
      if (m_drrQueues.none ())
        {
          NS_LOG_DEBUG ("Start new round; LL deficit: " << m_llDeficit << " classic deficit: " << m_classicDeficit);
          m_drrQueues.set (LL);
          m_drrQueues.set (CLASSIC);
          m_llDeficit += (m_drrQuantum * GetWeight ());
          m_classicDeficit += m_drrQuantum;
          NS_LOG_DEBUG ("Starting state: LL front, LL deficit " << m_llDeficit << " classic deficit " << m_classicDeficit);
        }
      if (m_drrQueues.test (LL))
        {
          if (GetInternalQueue (LL)->Peek ())
            {
              uint32_t size = GetInternalQueue (LL)->Peek ()->GetSize ();
              if (size <= m_llDeficit)
                {
                  NS_LOG_DEBUG ("Selecting LL queue");
                  m_llDeficit -= size;
                  NS_LOG_DEBUG ("State after LL selection: LL deficit << " << m_llDeficit << " classic deficit " << m_classicDeficit);
                  return true;
                }
              else
                {
                  NS_LOG_DEBUG ("Not enough deficit to send LL packet, LL deficit " << m_llDeficit << " classic deficit " << m_classicDeficit);
                  m_drrQueues.reset (LL);
                  if (!GetInternalQueue (CLASSIC)->Peek ())
                    {
                      NS_LOG_DEBUG ("Send LL packet due to no CLASSIC packet");
                      m_llDeficit = 0;
                      return true;
                    }
                }
            }
          else
            {
              NS_LOG_DEBUG ("LL has no packet, fall through to consider CLASSIC");
            }
        }
      else
        {
          if (!GetInternalQueue (CLASSIC)->Peek ())
            {
              NS_LOG_DEBUG ("Send LL packet due to no CLASSIC packet");
              m_llDeficit = 0;
              return true;
            }
        }
      if (m_drrQueues.test (CLASSIC) || GetInternalQueue (CLASSIC)->Peek ())
        {
          if (GetInternalQueue (CLASSIC)->Peek ())
            {
              uint32_t size = GetInternalQueue (CLASSIC)->Peek ()->GetSize ();
              if (size <= m_classicDeficit)
                {
                  NS_LOG_DEBUG ("Selecting Classic queue");
                  m_classicDeficit -= size;
                  NS_LOG_DEBUG ("State after Classic selection: LL deficit << " << m_llDeficit << " classic deficit " << m_classicDeficit);
                  return false;
                }
              else if (size)
                {
                  NS_LOG_DEBUG ("Not enough deficit to send Classic packet");
                  m_drrQueues.reset (CLASSIC);
                  if (!GetInternalQueue (LL)->Peek ())
                    {
                      // Send anyway since there is no LL packet
                      NS_LOG_DEBUG ("Send CLASSIC packet due to no LL packet");
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
  return false;  // Unreachable
}

Ptr<QueueDiscItem>
DualQueueCoupledAqm::LowLatencyDequeue (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<QueueDiscItem> item = GetInternalQueue (LL)->Dequeue ();
  if (!item)
    {
      NS_LOG_DEBUG ("Failed to dequeue from LL queue");
      return 0;
    }
  NS_LOG_DEBUG ("Dequeue from LL queue, packet with sojourn time " << (Simulator::Now () - item->GetTimeStamp ()).GetSeconds () * 1000 << "ms");
  if (m_traceLlBytes >= item->GetSize ())
    {
      m_traceLlBytes -= item->GetSize ();
    }
  else 
    {
      m_traceLlBytes = 0;
    }
  m_traceLlSojourn (Simulator::Now () - item->GetTimeStamp ());
  return item;
}

Ptr<QueueDiscItem>
DualQueueCoupledAqm::ClassicDequeue (void)
{
  NS_LOG_FUNCTION (this);
  // while loop accounts for possibility of a drop in the below
  while (GetCurrentSize ().GetValue () > 0)
    {
      NS_LOG_DEBUG ("Dequeue from Classic queue, classic drop prob: " << m_classicDropProb);
      Ptr<QueueDiscItem> item = GetInternalQueue (CLASSIC)->Dequeue ();
      if (!item)
        {
          NS_LOG_DEBUG ("Failed to dequeue from classic queue");
          return 0;
        }
      if (m_traceClassicBytes >= item->GetSize ())
        {
          m_traceClassicBytes -= item->GetSize ();
        }
      else 
        {
          m_traceClassicBytes = 0;
        }
      m_traceClassicSojourn (Simulator::Now () - item->GetTimeStamp ());
      return item;
    }
  return 0;
}
   
Ptr<QueueDiscItem>
DualQueueCoupledAqm::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);
  if (SelectLlQueue ())
    {
      return LowLatencyDequeue ();
    }
  else
    {
      return ClassicDequeue ();
    }
}

Ptr<const QueueDiscItem>
DualQueueCoupledAqm::DoPeek (void)
{
  // The QueueDisc::Peek() operation causes the DualQueueCoupledAqm() to
  // dequeue a packet from either the internal classic or ll queue.  This
  // packet then sits on the private m_requeue pointer internally.  Because
  // all dequeue operations by the CmNetDevice (when using the
  // CmtsUpstreamScheduler) are done directly on the internal queues, the
  // packet that may be sitting on the m_requeue pointer would be bypassed.
  // The below statement ensures that users do not try to Peek() on this
  // queue model.
  NS_FATAL_ERROR ("DualQueueCoupledAqm not designed to support Peek ()");
  return nullptr;
}

bool
DualQueueCoupledAqm::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("DualQueueCoupledAqm cannot have classes");
      return false;
    }

  if (GetNPacketFilters () == 0)
    {
      NS_FATAL_ERROR ("DualQueueCoupledAqm requires installation of at least one PacketFilter");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      NS_LOG_DEBUG ("Create default internal queues");
      // Create 2 DropTail queues
      if (m_classicBufferSize == QueueSize ("0B"))
        {
          // See C.2.2.7.11.4 Target Buffer.  Default chosen to be 100ms
          uint32_t classicSfBytes = std::max<uint32_t> ((0.1 * m_amsr.GetBitRate ()), (20 * m_maxFrameSize));
          NS_LOG_DEBUG ("Setting classic queue size to default value of " << classicSfBytes << " bytes");
          AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> >   
                              ("MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, classicSfBytes))));
        }
      else
        {
          NS_LOG_DEBUG ("Setting classic queue size to configured value of " << m_classicBufferSize << " bytes");
          AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> >   
                              ("MaxSize", QueueSizeValue (m_classicBufferSize)));
        }
      if (m_lowLatencyBufferSize == QueueSize ("0B"))
        {
          // See C.2.2.7.11.4 Target Buffer equation (3).
          uint32_t llSfBytes = std::max<uint32_t> ((CLASSIC_TARGET_BUFFER.GetSeconds () * m_amsr.GetBitRate ()), (20 * m_maxFrameSize));
          NS_LOG_DEBUG ("Setting low latency queue size to default value of " << llSfBytes << " bytes");
          AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> >   
                              ("MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, llSfBytes))));
        }
      else
        {
          NS_LOG_DEBUG ("Setting low latency queue size to configured value of " << m_lowLatencyBufferSize << " bytes");
          AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> >             
                              ("MaxSize", QueueSizeValue (m_lowLatencyBufferSize)));
        }
    }

  if (GetNInternalQueues () != 2)
    {
      NS_LOG_ERROR ("DualQueueCoupledAqm needs 2 internal queues");
      return false;
    }

  return true;
}


bool
DualQueueCoupledAqm::DropEarly (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  if (m_burstAllowance.GetSeconds () > 0)
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
      if (GetClassicQueueSize () < GetInternalQueue (CLASSIC)->GetMaxSize ().GetValue ()/ 3)
        {
          return false;
        }
      else
        {
          m_burstState = IN_BURST;
        }
    }

  double p = m_classicDropProb;

  uint32_t packetSize = item->GetSize ();

  p = p * packetSize / m_meanPktSize;
  p = std::min (p, m_probLow);
  m_accuProb += p;

  bool earlyDrop = true;
  double u =  m_uv->GetValue ();

  if ((m_prevq.GetSeconds () < (0.5 * m_latencyTarget.GetSeconds ())) && (m_classicDropProb < 0.2))
    {
      return false;
    }
  else if (GetClassicQueueSize () <= 2 * m_meanPktSize)
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
DualQueueCoupledAqm::SetLowLatencyDscpOverwrite (DscpOverwrite overwrite)
{
  NS_LOG_FUNCTION (this << +overwrite.m_tosAndMask << +overwrite.m_tosOrMask);
  NS_ABORT_MSG_IF ((overwrite.m_tosAndMask & 0x03) != 0x03, "tos-and-mask should not have either of two LS bits set to zero");
  NS_ABORT_MSG_IF (overwrite.m_tosOrMask & 0x03, "tos-or-mask should not have either of two LS bits set to one");
  m_llDscpOverwrite = overwrite;
}

DscpOverwrite DualQueueCoupledAqm::GetLowLatencyDscpOverwrite (void) const
{
  return m_llDscpOverwrite;
}

void
DualQueueCoupledAqm::SetClassicDscpOverwrite (DscpOverwrite overwrite)
{
  NS_LOG_FUNCTION (this << +overwrite.m_tosAndMask << +overwrite.m_tosOrMask);
  NS_ABORT_MSG_IF ((overwrite.m_tosAndMask & 0x0003) != 0x0003, "tos-and-mask should not have either of two LS bits set to zero");
  NS_ABORT_MSG_IF (overwrite.m_tosOrMask & 0x0003, "tos-or-mask should not have either of two LS bits set to one");
  m_classicDscpOverwrite = overwrite;
}

DscpOverwrite
DualQueueCoupledAqm::GetClassicDscpOverwrite (void) const
{
  return m_classicDscpOverwrite;
}

} //namespace docsis
} //namespace ns3
