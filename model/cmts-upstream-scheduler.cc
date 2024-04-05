/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017-2020 Cable Television Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors:
 *   Tom Henderson <tomh@tomh.org>
 *   Greg White <g.white@cablelabs.com>
 *   Karthik Sundaresan <k.sundaresan@cablelabs.com>
 */

#include "cmts-upstream-scheduler.h"

#include "ns3/abort.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/pointer.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CmtsUpstreamScheduler");

namespace docsis
{

NS_OBJECT_ENSURE_REGISTERED(CmtsUpstreamScheduler);

TypeId
CmtsUpstreamScheduler::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::docsis::CmtsUpstreamScheduler")
            .SetParent<Object>()
            .SetGroupName("Docsis")
            .AddConstructor<CmtsUpstreamScheduler>()
            .AddAttribute("FreeCapacityMean",
                          "Average upstream free capacity (bits/sec)",
                          DataRateValue(DataRate(0)),
                          MakeDataRateAccessor(&CmtsUpstreamScheduler::m_freeCapacityMean),
                          MakeDataRateChecker())
            .AddAttribute("FreeCapacityVariation",
                          "Bound (percent) on the variation of upstream free capacity",
                          DoubleValue(0),
                          MakeDoubleAccessor(&CmtsUpstreamScheduler::m_freeCapacityVariation),
                          MakeDoubleChecker<double>(0, 100))
            .AddAttribute("CongestionVariable",
                          "A RandomVariableStream used to pick the level of simulated congestion",
                          StringValue("ns3::UniformRandomVariable[Max=65535]"),
                          MakePointerAccessor(&CmtsUpstreamScheduler::m_congestionVariable),
                          MakePointerChecker<RandomVariableStream>())
            .AddAttribute("SchedulingWeight",
                          "Default value of scheduling weight if not present in ASF configuration; "
                          "if value is changed here, align with DualQueueCoupledAqm",
                          UintegerValue(230),
                          MakeUintegerAccessor(&CmtsUpstreamScheduler::m_schedulingWeight),
                          MakeUintegerChecker<uint8_t>(1, 255))
            .AddAttribute("MeanPacketSize",
                          "Assumed mean packet size (bytes) to estimate number of MAC header bytes "
                          "in requests",
                          UintegerValue(200),
                          MakeUintegerAccessor(&CmtsUpstreamScheduler::m_meanPacketSize),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("SchedulerRandomVariable",
                          "A RandomVariableStream used to place the grant in the MAP",
                          StringValue("ns3::UniformRandomVariable"),
                          MakePointerAccessor(&CmtsUpstreamScheduler::m_schedulerVariable),
                          MakePointerChecker<RandomVariableStream>())
            .AddAttribute("FirstPgsGrantFrameOffset",
                          "Offset (in frames) for initial PGS grant in initial MAP interval",
                          UintegerValue(0),
                          MakeUintegerAccessor(&CmtsUpstreamScheduler::m_firstPgsGrantFrameOffset),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("PeakRateTokenInterval",
                          "Time interval used to compute a burst size for peak rate token bucket",
                          TimeValue(MilliSeconds(2)),
                          MakeTimeAccessor(&CmtsUpstreamScheduler::m_peakRateTokenInterval),
                          MakeTimeChecker())
            .AddTraceSource("MapTrace",
                            "Report CMTS internal state and generated MAP message",
                            MakeTraceSourceAccessor(&CmtsUpstreamScheduler::m_mapTrace),
                            "ns3::CmtsUpstreamScheduler::MapTracedCallback");
    return tid;
}

CmtsUpstreamScheduler::CmtsUpstreamScheduler()
{
    NS_LOG_FUNCTION(this);
}

CmtsUpstreamScheduler::~CmtsUpstreamScheduler()
{
    NS_LOG_FUNCTION(this);
}

void
CmtsUpstreamScheduler::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_upstream = nullptr;
    if (m_asf)
    {
        m_asf->SetLowLatencyServiceFlow(nullptr);
        m_asf->SetClassicServiceFlow(nullptr);
    }
    m_asf = nullptr;
    m_sf = nullptr;
    m_classicSf = nullptr;
    m_llSf = nullptr;
    if (m_generateMapEvent.IsRunning())
    {
        m_generateMapEvent.Cancel();
    }
    if (m_mapArrivalEvent.IsRunning())
    {
        m_mapArrivalEvent.Cancel();
    }
}

void
CmtsUpstreamScheduler::DoInitialize()
{
    NS_LOG_FUNCTION(this);
}

void
CmtsUpstreamScheduler::SetUpstream(Ptr<CmNetDevice> upstream)
{
    NS_LOG_FUNCTION(this << upstream);
    m_upstream = upstream;
}

Ptr<CmNetDevice>
CmtsUpstreamScheduler::GetUpstream() const
{
    return m_upstream;
}

void
CmtsUpstreamScheduler::ReceiveRequest(GrantRequest request)
{
    NS_LOG_FUNCTION(this << request.m_sfid << request.m_bytes);
    // Last request time is tracked here, but not currently used because
    // requests may arrive sporadically or not at all when PGS is used.
    m_lastRequestTime = SequenceNumber32(request.m_requestTime);
    // Check if request exists; if so, amend, if not insert
    auto it = m_requests.find(request.m_sfid);
    if (it == m_requests.end())
    {
        NS_LOG_DEBUG("Inserting " << request.m_bytes << " bytes to new entry for sfid "
                                  << request.m_sfid);
        m_requests.insert({request.m_sfid, request.m_bytes});
    }
    else
    {
        NS_LOG_DEBUG("Adding " << request.m_bytes << " bytes to existing " << (*it).second
                               << " for sfid " << request.m_sfid);
        (*it).second += request.m_bytes;
    }
}

// Unused grant tracking
void
CmtsUpstreamScheduler::ReceiveUnusedLGrantUpdate(CmNetDevice::LGrantState state)
{
    NS_LOG_FUNCTION(this << state.unused);
    NS_LOG_DEBUG("Replenishing " << state.unused << " tokens to MSR token bucket containing "
                                 << m_tokenBucketState.m_msrTokens);
    m_tokenBucketState.m_msrTokens += state.unused;
}

void
CmtsUpstreamScheduler::ReceiveUnusedCGrantUpdate(CmNetDevice::CGrantState state)
{
    NS_LOG_FUNCTION(this << state.unused);
    NS_LOG_DEBUG("Replenishing " << state.unused << " tokens to MSR token bucket containing "
                                 << m_tokenBucketState.m_msrTokens);
    m_tokenBucketState.m_msrTokens += state.unused;
}

void
CmtsUpstreamScheduler::Start(uint32_t allocStartTime, uint32_t minReqGntDelaySlots)
{
    NS_LOG_FUNCTION(this << allocStartTime << minReqGntDelaySlots);
    m_allocStartTime = SequenceNumber32(allocStartTime);
    // The Ack time for each MAP message will be the time corresponding to
    // MinReqGntDelay (in minislots) in advance of the Alloc Start Time.
    m_minReqGntDelaySlots = minReqGntDelaySlots;
    if (m_llSf && m_llSf->m_guaranteedGrantRate.GetBitRate() > 0)
    {
        uint32_t ggr = m_llSf->m_guaranteedGrantRate.GetBitRate();
        NS_LOG_DEBUG("Increasing GGR by factor of "
                     << static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
                            m_meanPacketSize);
        ggr = static_cast<uint32_t>(
            ggr * static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
            m_meanPacketSize);
        uint16_t ggi = m_llSf->m_guaranteedGrantInterval;
        // If Guaranteed Grant Interval is absent, it defaults to vendor-specific
        // value.  Here, we set it to the MAP interval
        if (ggi == 0)
        {
            ggi = GetUpstream()->GetActualMapInterval().GetMicroSeconds();
        }
        uint16_t frameIntervalUs = GetUpstream()->GetActualMapInterval().GetMicroSeconds() /
                                   GetUpstream()->GetFramesPerMap();
        NS_ABORT_MSG_IF(ggr && (ggi < frameIntervalUs),
                        "GGI must be at least one frame interval: " << frameIntervalUs);
        m_pgsFrameInterval = ggi / frameIntervalUs; // Integer division floors this value
        NS_LOG_DEBUG("GGR (bps): " << ggr << "; GGI (us): " << ggi
                                   << "; frames between PGS grants:" << m_pgsFrameInterval);
        // Ensure that enough bytes are sent per guaranteed frame interval
        // to meet the GGI
        uint32_t pgsBytesPerInterval = std::ceil(static_cast<uint64_t>(ggr / 8) *
                                                 (m_pgsFrameInterval * frameIntervalUs) / 1e6);
        NS_ASSERT_MSG(pgsBytesPerInterval, "Unexpected zero value for pgsBytesPerInterval");
        m_pgsBytesPerInterval = pgsBytesPerInterval;
        m_pgsMinislotsPerInterval = MinislotCeil(pgsBytesPerInterval);
        NS_ASSERT_MSG(m_pgsMinislotsPerInterval,
                      "Unexpected zero value for pgsMinislotsPerInterval");
        NS_LOG_DEBUG("PGS bytes/interval: " << m_pgsBytesPerInterval
                                            << "; excess/interval: " << m_excessPgsBytesPerInterval
                                            << "; minislots/interval: "
                                            << m_pgsMinislotsPerInterval);
        m_excessPgsBytesPerInterval =
            (m_pgsMinislotsPerInterval * GetUpstream()->GetMinislotCapacity()) -
            pgsBytesPerInterval;
        // An attribute configures the frame offset for the first PGS grant
        // in the first MAP interval (if PGS is enabled).  If the GGI equals
        // a MAP interval, then this attribute will control the placement of the
        // grant in the MAP interval (e.g. consistently the first frame, or the
        // last frame, etc.)
        //
        // Ensure that the first PGS grant occurs in the first MAP interval
        NS_ABORT_MSG_UNLESS(m_firstPgsGrantFrameOffset < GetUpstream()->GetFramesPerMap(),
                            "First offset: " << m_firstPgsGrantFrameOffset
                                             << " greater than or equal to frames in MAP: "
                                             << GetUpstream()->GetFramesPerMap());
        // Ensure that the subtraction that follows does not overflow
        NS_ABORT_MSG_UNLESS(
            allocStartTime + m_firstPgsGrantFrameOffset * GetUpstream()->GetMinislotsPerFrame() >=
                m_pgsFrameInterval * GetUpstream()->GetMinislotsPerFrame(),
            "Error: " << allocStartTime << " "
                      << m_pgsFrameInterval * GetUpstream()->GetMinislotsPerFrame());
        // Set the variable so that, in the first MAP interval, the PGS grant
        // will land in the correct starting frame (w.r.t. allocStartTime),
        // by subtracting off one PGS grant interval
        m_lastPgsGrantTime = (m_allocStartTime +
                              m_firstPgsGrantFrameOffset * GetUpstream()->GetMinislotsPerFrame()) -
                             m_pgsFrameInterval * GetUpstream()->GetMinislotsPerFrame();
    }
    GenerateMap();
}

// Generate a MAP message with grants to the cable modem.
//
// Calculate how many minislots must be granted to each service flow
// The demand is based on PGS configuration, low latency service flow
// requests, and classic service flow requests. The supply is constrained
// by the available minislots and token bucket rate limiting.

// If PGS is not configured, then each MAP interval will result in one
// Classic and one Low Latency grant (if backlogs for those service flows
// exist).  If PGS is configured, then the granting behavior to conform to
// the GGR and GGI will be the first grants scheduled, and then best effort
// grant requests will be handled.
void
CmtsUpstreamScheduler::GenerateMap()
{
    NS_LOG_FUNCTION(this);

    InitializeMapReport();
    // Calculate number of minislots available.  The congestion model (if
    // active) is used in CalculateFreeCapacity ().  The number of free
    // minislots may be used in the PGS scheduling task (see code below
    // that is disabled by default) and is used in the best effort scheduling
    // task.
    uint32_t freeMinislots = CalculateFreeCapacity();
    NS_LOG_DEBUG("Free minislots: " << freeMinislots
                                    << "; total: " << GetUpstream()->GetMinislotsPerMap());

    // Start to allocate grants, prioritizing the low-latency service flow.
    // The first step is PGS granting (if enabled on the service flow).

    uint32_t pgsGrantedBytes = 0;
    uint32_t excessPgsGrantedBytes = 0; // Track overage due to minislot roundup
                                        // excessPgsGrantedBytes is reported
                                        // in the MAP report but not used herein
    // Maintain grants in a notional list, but one that is implemented as a
    // C++ map, with the key to this map being the grant start time (in minislots)
    // Key to this map is the grant start time (in minislots)
    std::map<uint32_t, Grant> pgsGrantList;
    uint32_t pgsMinislots = 0;
    if (m_llSf && m_llSf->m_guaranteedGrantRate.GetBitRate() > 0)
    {
        // m_allocStartTime is the first minislot in this MAP interval being
        // scheduled.  nextAllocStartTime is the first minislot in the next
        // MAP interval being scheduled (in the next scheduling cycle)
        SequenceNumber32 nextAllocStartTime =
            m_allocStartTime +
            GetUpstream()->TimeToMinislots(GetUpstream()->GetActualMapInterval());
        NS_LOG_DEBUG("AllocStartTime: " << m_allocStartTime
                                        << "; nextAllocStartTime: " << nextAllocStartTime);
        uint32_t pgsMinislotsAllowed = freeMinislots;
        // Determine each frame that requires a PGS grant (if any), and add grant
        // Maintain time quantities in units of minislots
        for (SequenceNumber32 i =
                 m_lastPgsGrantTime + m_pgsFrameInterval * GetUpstream()->GetMinislotsPerFrame();
             i < nextAllocStartTime && pgsMinislotsAllowed;)
        {
            NS_LOG_DEBUG("PGS grant of " << m_pgsMinislotsPerInterval << " at minislot offset "
                                         << (i - m_allocStartTime));
            m_lastPgsGrantTime = i;
            // Add the grant
            Grant lGrant;
            lGrant.m_sfid = LOW_LATENCY_SFID;
            if (m_pgsMinislotsPerInterval < pgsMinislotsAllowed)
            {
                lGrant.m_length = m_pgsMinislotsPerInterval;
                pgsMinislots += m_pgsMinislotsPerInterval;
#if 0
              // See below
              pgsMinislotsAllowed -= m_pgsMinislotsPerInterval;
#endif
                pgsGrantedBytes += m_pgsBytesPerInterval;
                excessPgsGrantedBytes += m_excessPgsBytesPerInterval;
            }
#if 0
          // This code branch exists in case the model is changed to
          // constrain PGS grants by congestion state.  Presently, the
          // model will not constrain PGS grants by congestion state.
          else
            {
              NS_LOG_DEBUG ("PGS grants limited by free minislots");
              lGrant.m_length = pgsMinislotsAllowed;
              pgsMinislots += pgsMinislotsAllowed;
              pgsGrantedBytes += pgsMinislotsAllowed * GetUpstream ()->GetMinislotCapacity ();
              pgsMinislotsAllowed = 0;
            }
#endif
            int32_t offset = i - m_allocStartTime;
            NS_ASSERT_MSG(offset >= 0, "Offset calculation is negative");
            lGrant.m_offset = static_cast<uint32_t>(offset);
            pgsGrantList.insert(std::pair<uint32_t, Grant>(i.GetValue(), lGrant));
            i += (m_pgsFrameInterval * GetUpstream()->GetMinislotsPerFrame());
        }
        NS_LOG_DEBUG("PGS grants: " << pgsGrantList.size() << "; bytes: " << pgsGrantedBytes
                                    << "; excess bytes granted: " << excessPgsGrantedBytes);
        // Fill in values for the traced MAP report
        m_mapReport.m_pgsGrantedMinislots = pgsMinislots;
        m_mapReport.m_pgsGrantedBytes = pgsGrantedBytes;
        m_mapReport.m_excessPgsGrantedBytes = excessPgsGrantedBytes;
    }
    else
    {
        m_mapReport.m_pgsGrantedMinislots = 0;
        m_mapReport.m_pgsGrantedBytes = 0;
        m_mapReport.m_excessPgsGrantedBytes = 0;
    }

    // Update token bucket rate shaper
    // tokens->first == MSR, tokens->second == peak
    std::pair<int32_t, uint32_t> tokens = IncrementTokenBucketCounters();
    uint32_t availableTokens = 0;
    if (tokens.first <= 0)
    {
        // This implementation will not grant if there is a deficit
        NS_LOG_DEBUG("MSR tokens less than or equal to zero; no grant will be made");
    }
    else
    {
        availableTokens = std::min(static_cast<uint32_t>(tokens.first), tokens.second);
        NS_LOG_DEBUG("Available tokens: " << availableTokens);
    }
    m_mapReport.m_availableTokens = availableTokens;

    // Next, handle best-effort requests, constrained by availableTokens
    // and freeMinislots.
    uint32_t cRequestBacklog = m_requests[m_classicSf->m_sfid];
    uint32_t lRequestBacklog = 0;
    if (m_llSf)
    {
        lRequestBacklog = m_requests[m_llSf->m_sfid];
    }
    NS_LOG_DEBUG("cRequestBacklog: " << cRequestBacklog
                                     << "; lRequestBacklog: " << lRequestBacklog);
    // Fill in values for the traced MAP report
    m_mapReport.m_lRequestBacklog = lRequestBacklog;
    m_mapReport.m_cRequestBacklog = cRequestBacklog;

    // Perform grant allocation for remaining best-effort backlog
    uint32_t cAllocatedMinislots = 0;
    uint32_t lAllocatedMinislots = 0;
    if (availableTokens > 0 && (cRequestBacklog || lRequestBacklog))
    {
        // Bounded by freeMinislots and availableTokens, determine how much of the
        // classic and low latency backlog can be served in this MAP interval.  If PGS
        // is being used, account for the bytes and minislots already allocated.
        // This method is where the scheduling weight between the two queues
        // is applied.
        std::pair<uint32_t, uint32_t> allocation = Allocate(freeMinislots,
                                                            pgsMinislots,
                                                            availableTokens,
                                                            cRequestBacklog,
                                                            lRequestBacklog);
        NS_LOG_DEBUG("Scheduler allocated " << allocation.first << " bytes to SFID 1 and "
                                            << allocation.second << " to SFID 2");
        cAllocatedMinislots = allocation.first;
        lAllocatedMinislots = allocation.second;
    }

    // Build the grant list
    // We use a std::map functionally as an ordered std::list.  The map key
    // is the offset, the value is the grant.  Later, we use this map as an
    // ordered list; we only need to iterate on it and fetch the values
    // in order, but we exploit the property of a map that entries are stored
    // in non-descending order of keys.
    std::map<uint32_t, Grant> grantList;
    if (cAllocatedMinislots || lAllocatedMinislots || !pgsGrantList.empty())
    {
        std::pair<uint32_t, uint32_t> minislots = ScheduleGrant(grantList,
                                                                pgsGrantList,
                                                                pgsMinislots,
                                                                cAllocatedMinislots,
                                                                lAllocatedMinislots);
        uint32_t cMinislots = minislots.first;
        uint32_t lMinislots = minislots.second; // includes PGS granted minislots
        m_mapReport.m_cGrantedMinislots = cMinislots;
        m_mapReport.m_cGrantedBytes = cMinislots * GetUpstream()->GetMinislotCapacity();
        m_mapReport.m_lGrantedMinislots = lMinislots;
        m_mapReport.m_lGrantedBytes = lMinislots * GetUpstream()->GetMinislotCapacity();
        // We granted possibly more bytes than requested by rounding up to
        // a minislot boundary.  Also, PGS service may cause overallocation.
        // Subtract the total bytes granted (including roundup) from the token
        // bucket; unused bytes will be redeposited later based on unused grant
        // tracking.
        DecrementTokenBucketCounters(MinislotsToBytes(cMinislots) + MinislotsToBytes(lMinislots));
        // Subtract the full number of bytes granted from the request backlog
        if (cMinislots && (m_requests[m_classicSf->m_sfid] > MinislotsToBytes(cMinislots)))
        {
            NS_LOG_DEBUG("Allocated " << MinislotsToBytes(cMinislots)
                                      << " to partially cover classic request of "
                                      << m_requests[m_classicSf->m_sfid]);
            m_requests[m_classicSf->m_sfid] -= MinislotsToBytes(cMinislots);
        }
        else if (cMinislots && (m_requests[m_classicSf->m_sfid] <= MinislotsToBytes(cMinislots)))
        {
            NS_LOG_DEBUG("Allocated " << MinislotsToBytes(cMinislots)
                                      << " to fully cover classic request of "
                                      << m_requests[m_classicSf->m_sfid]);
            m_requests[m_classicSf->m_sfid] = 0;
        }
        if (m_llSf)
        {
            if (lMinislots && (m_requests[m_llSf->m_sfid] > MinislotsToBytes(lMinislots)))
            {
                NS_LOG_DEBUG("Allocated " << MinislotsToBytes(lMinislots)
                                          << " to partially cover LL (non-PGS) request of "
                                          << m_requests[m_llSf->m_sfid]);
                m_requests[m_llSf->m_sfid] -= MinislotsToBytes(lMinislots);
            }
            else if (lMinislots && (m_requests[m_llSf->m_sfid] <= MinislotsToBytes(lMinislots)))
            {
                NS_LOG_DEBUG("Allocated " << MinislotsToBytes(lMinislots)
                                          << " to fully cover LL (non-PGS) request of "
                                          << m_requests[m_llSf->m_sfid]);
                m_requests[m_llSf->m_sfid] = 0;
            }
        }
    }
    else
    {
        // Fill in values for the traced MAP report
        m_mapReport.m_cGrantedMinislots = 0;
        m_mapReport.m_cGrantedBytes = 0;
        m_mapReport.m_lGrantedMinislots = 0;
        m_mapReport.m_lGrantedBytes = 0;
    }

    MapMessage mapMessage = BuildMapMessage(grantList);

    // Schedule the allocation MAP message for arrival at the CM
    Time mapMessageArrivalDelay = GetUpstream()->GetDsIntlvDelay() +
                                  GetUpstream()->GetDsSymbolTime() * 3 +
                                  GetUpstream()->GetRtt() / 2 + GetUpstream()->GetCmtsMapProcTime();
    NS_LOG_DEBUG("MAP message arrival delay: " << mapMessageArrivalDelay.As(Time::S));
    NS_LOG_DEBUG("Schedule MAP message arrival at "
                 << (Simulator::Now() + mapMessageArrivalDelay).As(Time::S));
    m_mapArrivalEvent = Simulator::Schedule(mapMessageArrivalDelay,
                                            &CmNetDevice::HandleMapMsg,
                                            GetUpstream(),
                                            mapMessage);

    // Trace the MAP report
    m_mapReport.m_message = mapMessage;
    m_mapTrace(m_mapReport);

    // Reschedule for next MAP interval
    NS_LOG_DEBUG("Schedule next GenerateMap() at "
                 << (Simulator::Now() + GetUpstream()->GetActualMapInterval()).As(Time::S));
    m_allocStartTime += GetUpstream()->TimeToMinislots(GetUpstream()->GetActualMapInterval());
    m_generateMapEvent = Simulator::Schedule(GetUpstream()->GetActualMapInterval(),
                                             &CmtsUpstreamScheduler::GenerateMap,
                                             this);
}

MapMessage
CmtsUpstreamScheduler::BuildMapMessage(const std::map<uint32_t, Grant>& grantList)
{
    NS_LOG_FUNCTION(this);
    MapMessage mapMessage;
    if (m_llSf)
    {
        mapMessage.m_dataGrantPending =
            (m_requests[m_classicSf->m_sfid] || m_requests[m_llSf->m_sfid]);
    }
    else
    {
        mapMessage.m_dataGrantPending = m_requests[m_classicSf->m_sfid];
    }
    mapMessage.m_numIe = 0;
    mapMessage.m_allocStartTime = m_allocStartTime.GetValue();
    // The ACK Time in this model is estimated based on the minimum req/grant
    // delay, rather than m_lastRequest time, because there may be no grant
    // requests (due to PGS)
    mapMessage.m_ackTime = m_allocStartTime.GetValue() - m_minReqGntDelaySlots;
    NS_LOG_DEBUG("Alloc start time " << m_allocStartTime << " ack time " << mapMessage.m_ackTime);
    MapIe currentIe;
    uint32_t nextOffset;
    if (grantList.empty())
    {
        NS_LOG_DEBUG("Returning null MapMessage");
        return mapMessage;
    }
    auto it = grantList.begin();
    NS_LOG_DEBUG("Size of grant list " << grantList.size());
    if (it->second.m_offset > 0)
    {
        NS_LOG_DEBUG("Add initial null IE");
        currentIe.m_sfid = 0;
        currentIe.m_offset = 0;
        mapMessage.m_mapIeList.push_back(currentIe);
    }

    // Add a new IE followed by a delimiter if needed
    while (it != grantList.end())
    {
        currentIe.m_sfid = it->second.m_sfid;
        currentIe.m_offset = it->second.m_offset;
        nextOffset = it->second.m_length + it->second.m_offset;
        NS_LOG_DEBUG("Add grant IE for SFID " << currentIe.m_sfid << " offset "
                                              << currentIe.m_offset);
        mapMessage.m_mapIeList.push_back(currentIe);
        ++it;
        if (it == grantList.end())
        {
            NS_LOG_DEBUG("Add terminating null IE");
            currentIe.m_sfid = 0;
            currentIe.m_offset = nextOffset;
            mapMessage.m_mapIeList.push_back(currentIe);
            break;
        }
        else if (it->second.m_offset == nextOffset)
        {
            NS_LOG_DEBUG("Continue to an adjacent grant");
            continue;
        }
        else if (it->second.m_offset > nextOffset)
        {
            NS_LOG_DEBUG("Add null IE between grants");
            currentIe.m_sfid = 0;
            currentIe.m_offset = nextOffset;
            mapMessage.m_mapIeList.push_back(currentIe);
            continue;
        }
        else if (it->second.m_offset < nextOffset)
        {
            NS_FATAL_ERROR("grant list overlapping");
        }
    }
    return mapMessage;
}

void
CmtsUpstreamScheduler::InitializeMapReport()
{
    NS_LOG_FUNCTION(this);
    // Set some initial values in the next periodic traced MAP report
    m_mapReport.m_freeCapacityMean = m_freeCapacityMean;
    m_mapReport.m_freeCapacityVariation = m_freeCapacityVariation;
    m_mapReport.m_mapInterval = GetUpstream()->GetActualMapInterval();
    m_mapReport.m_framesPerMap = GetUpstream()->GetFramesPerMap();
    m_mapReport.m_minislotsPerFrame = GetUpstream()->GetMinislotsPerFrame();
    m_mapReport.m_minislotCapacity = GetUpstream()->GetMinislotCapacity();
    if (m_llSf)
    {
        m_mapReport.m_guaranteedGrantRate = m_llSf->m_guaranteedGrantRate;
        m_mapReport.m_guaranteedGrantInterval = m_llSf->m_guaranteedGrantInterval;
    }
    else
    {
        m_mapReport.m_guaranteedGrantRate = DataRate(0);
        m_mapReport.m_guaranteedGrantInterval = 0;
    }
    // Initialize others to catch uninitialized values
    m_mapReport.m_maxGrant = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_adjustedMaxGrant = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_lRequestBacklog = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_cRequestBacklog = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_availableTokens = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_lGrantedBytes = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_cGrantedBytes = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_pgsGrantedBytes = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_excessPgsGrantedBytes = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_lGrantedMinislots = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_cGrantedMinislots = std::numeric_limits<uint32_t>::max();
    m_mapReport.m_pgsGrantedMinislots = std::numeric_limits<uint32_t>::max();
}

uint32_t
CmtsUpstreamScheduler::CalculateFreeCapacity()
{
    NS_LOG_FUNCTION(this);

    if (m_freeCapacityMean.GetBitRate() == 0)
    {
        // Special value of zero signifies that user has not configured to
        // a specific value, so change it to the upstream capacity
        NS_LOG_DEBUG("No FreeCapacityMean configured; returning full number of minislots per MAP");
        m_mapReport.m_maxGrant =
            GetUpstream()->GetMinislotsPerMap() * GetUpstream()->GetMinislotCapacity();
        m_mapReport.m_adjustedMaxGrant = m_mapReport.m_maxGrant;
        return GetUpstream()->GetMinislotsPerMap();
    }
    uint32_t maxGrant = static_cast<uint32_t>(
        std::round(m_freeCapacityMean.GetBitRate() *
                   GetUpstream()->GetActualMapInterval().GetSeconds() / 8)); // bytes
    NS_LOG_DEBUG("FreeCapacityMean (bps) " << m_freeCapacityMean.GetBitRate()
                                           << " maxGrant (bytes/MAP) " << maxGrant);
    m_mapReport.m_maxGrant = maxGrant;
    m_mapReport.m_adjustedMaxGrant = maxGrant;
    if (m_freeCapacityVariation == 0)
    {
        uint32_t freeMinislots = MinislotRound(maxGrant);
        freeMinislots = std::min<uint32_t>(freeMinislots, GetUpstream()->GetMinislotsPerMap());
        NS_LOG_DEBUG("free minislots: " << freeMinislots << " minislots per map "
                                        << GetUpstream()->GetMinislotsPerMap());
        return freeMinislots;
    }
    // Allow maxGrant to range lower or higher, based on the allowable
    // variation and a random variate
    int32_t w = int(m_freeCapacityVariation * maxGrant / 100);
    int32_t congestionVariate = int(m_congestionVariable->GetInteger());
    int64_t r = (w == 0) ? 0 : congestionVariate % (2 * w) - w;
    // maximum free bytes in the next MAP is maxgrant + r
    int64_t nextmax = maxGrant + r;
    // unsignedNextMax is the free capacity in the MAP interval, in bytes
    uint32_t unsignedNextMax = nextmax < 0 ? 0 : (uint32_t)nextmax;
    m_mapReport.m_adjustedMaxGrant = unsignedNextMax;
    NS_LOG_DEBUG("w: " << w << " rv: " << congestionVariate << " r: " << r
                       << " maximumGrant: " << unsignedNextMax);
    NS_ABORT_MSG_UNLESS(r >= (w * -1) && r <= w, "Error in r " << r);
    uint32_t freeMinislots = MinislotRound(unsignedNextMax);
    // Cap free minislots to the MAP capacity
    freeMinislots = std::min<uint32_t>(freeMinislots, GetUpstream()->GetMinislotsPerMap());
    NS_LOG_DEBUG("free minislots: " << freeMinislots);
    return freeMinislots;
}

uint32_t
CmtsUpstreamScheduler::MinislotRound(uint32_t bytes) const
{
    return static_cast<uint32_t>(
        std::round(static_cast<double>(bytes) / GetUpstream()->GetMinislotCapacity()));
}

uint32_t
CmtsUpstreamScheduler::MinislotCeil(uint32_t bytes) const
{
    return static_cast<uint32_t>(
        std::ceil(static_cast<double>(bytes) / GetUpstream()->GetMinislotCapacity()));
}

uint32_t
CmtsUpstreamScheduler::MinislotsToBytes(uint32_t minislots) const
{
    return minislots * GetUpstream()->GetMinislotCapacity();
}

void
CmtsUpstreamScheduler::ProvisionAggregateServiceFlow(Ptr<const AggregateServiceFlow> flow)
{
    NS_LOG_FUNCTION(this);
    NS_ABORT_MSG_IF(m_sf, "Cannot provision both an ASF and a single SF");
    if (m_asf)
    {
        NS_LOG_WARN("Removing and replacing existing aggregate service flow");
    }
    m_asf = CopyObject<AggregateServiceFlow>(flow);
    m_classicSf = m_asf->GetClassicServiceFlow();
    m_llSf = m_asf->GetLowLatencyServiceFlow();
    if (m_asf->m_schedulingWeight)
    {
        NS_LOG_DEBUG("Setting SchedulingWeight to " << m_asf->m_schedulingWeight);
        m_schedulingWeight = m_asf->m_schedulingWeight;
    }
    // Adjust flow rates to account for MAC header bytes in bandwidth requests
    // MSR_to_use = MSR_Configured*(MeanPktSize + DOCSISHeaderSize)/MeanPktSize
    NS_LOG_DEBUG("Increasing MSR and peak by factor of "
                 << static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
                        m_meanPacketSize);
    m_asf->m_maxSustainedRate = DataRate(static_cast<uint64_t>(
        m_asf->m_maxSustainedRate.GetBitRate() *
        static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
        m_meanPacketSize));
    m_asf->m_peakRate = DataRate(static_cast<uint64_t>(
        m_asf->m_peakRate.GetBitRate() *
        static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
        m_meanPacketSize));
    NS_LOG_DEBUG("Modified MSR: " << m_asf->m_maxSustainedRate.GetBitRate()
                                  << " Peak: " << m_asf->m_peakRate.GetBitRate());
    m_tokenBucketState.m_msrTokens = static_cast<int32_t>(m_asf->m_maxTrafficBurst);
    m_tokenBucketState.m_peakTokens = 0;
    m_tokenBucketState.m_lastIncrement = Simulator::Now();
    m_tokenBucketState.m_lastDrain = Simulator::Now();
    m_peakRateBurstSize = m_peakRateTokenInterval.GetSeconds() * m_asf->m_peakRate.GetBitRate() / 8;
}

Ptr<const AggregateServiceFlow>
CmtsUpstreamScheduler::GetAggregateServiceFlow() const
{
    return m_asf;
}

void
CmtsUpstreamScheduler::RemoveAggregateServiceFlow()
{
    m_asf = nullptr;
    m_classicSf = nullptr;
    m_llSf = nullptr;
}

void
CmtsUpstreamScheduler::ProvisionSingleServiceFlow(Ptr<ServiceFlow> flow)
{
    NS_LOG_FUNCTION(this);
    NS_ABORT_MSG_IF(m_asf, "Cannot provision both an ASF and a single SF");
    if (m_sf)
    {
        NS_LOG_WARN("Removing and replacing existing single service flow " << flow->m_sfid);
    }
    m_asf = nullptr;
    m_sf = flow;
    if (m_sf->m_sfid == CLASSIC_SFID)
    {
        m_classicSf = flow;
        m_llSf = nullptr;
    }
    else
    {
        m_llSf = flow;
        m_classicSf = nullptr;
    }
    // Adjust flow rates to account for MAC header bytes in bandwidth requests
    // MSR_to_use = MSR_Configured*(MeanPktSize + DOCSISHeaderSize)/MeanPktSize
    NS_LOG_DEBUG("Increasing MSR and peak by factor of "
                 << static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
                        m_meanPacketSize);
    flow->m_maxSustainedRate = DataRate(static_cast<uint64_t>(
        flow->m_maxSustainedRate.GetBitRate() *
        static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
        m_meanPacketSize));
    flow->m_peakRate = DataRate(static_cast<uint64_t>(
        flow->m_peakRate.GetBitRate() *
        static_cast<double>(m_meanPacketSize + GetUpstream()->GetUsMacHdrSize()) /
        m_meanPacketSize));
    NS_LOG_DEBUG("Modified MSR: " << flow->m_maxSustainedRate.GetBitRate()
                                  << " Peak: " << flow->m_peakRate.GetBitRate());
    m_tokenBucketState.m_msrTokens = static_cast<int32_t>(flow->m_maxTrafficBurst);
    m_tokenBucketState.m_peakTokens = 0;
    m_tokenBucketState.m_lastIncrement = Simulator::Now();
    m_tokenBucketState.m_lastDrain = Simulator::Now();
    m_peakRateBurstSize = m_peakRateTokenInterval.GetSeconds() * flow->m_peakRate.GetBitRate() / 8;
}

Ptr<const ServiceFlow>
CmtsUpstreamScheduler::GetSingleServiceFlow() const
{
    return m_sf;
}

void
CmtsUpstreamScheduler::RemoveSingleServiceFlow()
{
    m_sf = nullptr;
    m_classicSf = nullptr;
    m_llSf = nullptr;
}

std::pair<int32_t, uint32_t>
CmtsUpstreamScheduler::IncrementTokenBucketCounters()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_DEBUG("Token bucket counters at " << m_tokenBucketState.m_msrTokens << " msr and "
                                             << m_tokenBucketState.m_peakTokens << " peak");
    DataRate msr;
    uint32_t mtb = 0;
    DataRate peak;
    if (m_asf)
    {
        msr = m_asf->m_maxSustainedRate;
        mtb = m_asf->m_maxTrafficBurst;
        peak = m_asf->m_peakRate;
    }
    else
    {
        msr = m_sf->m_maxSustainedRate;
        mtb = m_sf->m_maxTrafficBurst;
        peak = m_sf->m_peakRate;
    }
    NS_LOG_DEBUG("MSR: " << msr.GetBitRate() << " Peak: " << peak.GetBitRate() << " burst " << mtb);
    Time interval = Simulator::Now() - m_tokenBucketState.m_lastIncrement;
    if (m_tokenBucketState.m_lastIncrement == Seconds(0))
    {
        // Handle corner case that for the first MAP message generated,
        // there may not have been a full MAP interval elapsed, and the
        // number of peak tokens may be too small when the GGR is high.
        // Set the interval to the actual MAP interval in this case
        interval = GetUpstream()->GetActualMapInterval();
    }
    NS_ASSERT_MSG(interval >= Seconds(0), "Error:  negative time");
    uint32_t tokenIncrement = static_cast<uint32_t>(interval.GetSeconds() * msr.GetBitRate() / 8);
    m_tokenBucketState.m_msrTokens += tokenIncrement;
    if (m_tokenBucketState.m_msrTokens > static_cast<int32_t>(mtb))
    {
        NS_LOG_DEBUG("msrTokens limited to max traffic burst " << mtb);
        m_tokenBucketState.m_msrTokens = static_cast<int32_t>(mtb);
    }
    tokenIncrement = static_cast<uint32_t>(interval.GetSeconds() * peak.GetBitRate() / 8);
    m_tokenBucketState.m_peakTokens += tokenIncrement;
    if (m_tokenBucketState.m_peakTokens > m_peakRateBurstSize)
    {
        NS_LOG_DEBUG("peakTokens limited to " << m_peakRateBurstSize);
        m_tokenBucketState.m_peakTokens = m_peakRateBurstSize;
    }
    NS_LOG_DEBUG("Updating token bucket counters to "
                 << m_tokenBucketState.m_msrTokens << " msr and " << m_tokenBucketState.m_peakTokens
                 << " peak");
    m_tokenBucketState.m_lastIncrement = Simulator::Now();
    return std::pair<int32_t, uint32_t>(m_tokenBucketState.m_msrTokens,
                                        m_tokenBucketState.m_peakTokens);
}

void
CmtsUpstreamScheduler::DecrementTokenBucketCounters(uint32_t decrement)
{
    NS_LOG_FUNCTION(this << decrement);

    DataRate msr;
    uint32_t mtb = 0;
    DataRate peak;
    if (m_asf)
    {
        msr = m_asf->m_maxSustainedRate;
        mtb = m_asf->m_maxTrafficBurst;
        peak = m_asf->m_peakRate;
    }
    else
    {
        msr = m_sf->m_maxSustainedRate;
        mtb = m_sf->m_maxTrafficBurst;
        peak = m_sf->m_peakRate;
    }
    if (m_tokenBucketState.m_msrTokens > static_cast<int32_t>(decrement))
    {
        m_tokenBucketState.m_msrTokens -= static_cast<int32_t>(decrement);
    }
    else
    {
        // This scheduler policy is to not allow tokens to go negative
        m_tokenBucketState.m_msrTokens = 0;
    }
    if (m_tokenBucketState.m_peakTokens > decrement)
    {
        m_tokenBucketState.m_peakTokens -= decrement;
    }
    else
    {
        // This scheduler policy is to not allow tokens to go negative
        m_tokenBucketState.m_peakTokens = 0;
    }
    Time interval = Simulator::Now() - m_tokenBucketState.m_lastDrain;
    // Check for conformance.  At simulation startup time (when m_lastDrain
    // is zero), the interval may be too small and cause an error, so disable
    // this check the first time that Decrement() is called.
    if (m_tokenBucketState.m_lastDrain > Seconds(0))
    {
        if (decrement > ((interval.GetSeconds() * msr.GetBitRate() / 8) + mtb))
        {
            NS_FATAL_ERROR("Token bucket limits (MSR) exceeded");
        }
    }
    m_tokenBucketState.m_lastDrain = Simulator::Now();
}

std::pair<uint32_t, uint32_t>
CmtsUpstreamScheduler::Allocate(uint32_t freeMinislots,
                                uint32_t pgsMinislots,
                                uint32_t availableTokens,
                                uint32_t cRequestedBytes,
                                uint32_t lRequestedBytes)
{
    NS_LOG_FUNCTION(this << freeMinislots << pgsMinislots << availableTokens << cRequestedBytes
                         << lRequestedBytes);
    NS_ASSERT_MSG((availableTokens > 0 && (cRequestedBytes || lRequestedBytes)),
                  "Allocate called with invalid arguments");

    // First determine the allocation constraints.  Then try
    // to apportion minislots to classic and LL flows according
    // to scheduling weight.

    uint32_t availableMinislots = std::min<uint32_t>(MinislotCeil(availableTokens), freeMinislots);

    uint32_t maxCMinislots = MinislotCeil(cRequestedBytes);
    uint32_t maxLMinislots = std::max<uint32_t>(MinislotCeil(lRequestedBytes), pgsMinislots);
    uint32_t maxRequestedMinislots = maxCMinislots + maxLMinislots;
    NS_LOG_DEBUG("Available minislots (due to congestion or token bucket): "
                 << availableMinislots << "; maximum requested: " << maxRequestedMinislots);
    uint32_t minislotsToAllocate = std::min<uint32_t>(availableMinislots, maxRequestedMinislots);
    // At least PGS minislots must be allocated
    minislotsToAllocate = std::max<uint32_t>(minislotsToAllocate, pgsMinislots);

    // Allocate according to the following constraints
    // - pgsMinislots, if present, must service LL backlog
    // - respect scheduler weight as much as possible
    uint32_t cMinislotsAllocated = 0;
    uint32_t lMinislotsAllocated = 0;
    if (pgsMinislots >= maxLMinislots)
    {
        NS_LOG_DEBUG("PGS minislots exceed the L request backlog");
        if (availableMinislots > pgsMinislots)
        {
            cMinislotsAllocated =
                std::min<uint32_t>((availableMinislots - pgsMinislots), maxCMinislots);
        }
        lMinislotsAllocated = pgsMinislots;
        NS_LOG_DEBUG("C minislots allocated: " << cMinislotsAllocated << " L minislots allocated: "
                                               << lMinislotsAllocated);
        return std::make_pair(cMinislotsAllocated, lMinislotsAllocated);
    }
    // pgsMinislots < maxLMinislots, so try to allocate more than pgsMinislots to the L service flow
    uint32_t lWeightedMinislotsToAllocate =
        static_cast<uint32_t>(static_cast<double>(minislotsToAllocate) * m_schedulingWeight / 256);
    lWeightedMinislotsToAllocate = std::max<uint32_t>(lWeightedMinislotsToAllocate, pgsMinislots);
    NS_ASSERT_MSG(minislotsToAllocate >= lWeightedMinislotsToAllocate, "Arithmetic overflow");
    uint32_t cWeightedMinislotsToAllocate = minislotsToAllocate - lWeightedMinislotsToAllocate;
    if (maxLMinislots <= lWeightedMinislotsToAllocate &&
        maxCMinislots <= cWeightedMinislotsToAllocate)
    {
        NS_LOG_DEBUG("All minislot requests can be allocated");
        cMinislotsAllocated = maxCMinislots;
        lMinislotsAllocated = maxLMinislots;
    }
    else if (maxLMinislots > lWeightedMinislotsToAllocate &&
             maxCMinislots > cWeightedMinislotsToAllocate)
    {
        NS_LOG_DEBUG("Both minislots allocations limited; using weight of " << m_schedulingWeight);
        cMinislotsAllocated = cWeightedMinislotsToAllocate;
        lMinislotsAllocated = lWeightedMinislotsToAllocate;
    }
    else if (maxLMinislots <= lWeightedMinislotsToAllocate &&
             maxCMinislots > cWeightedMinislotsToAllocate)
    {
        NS_LOG_DEBUG("C minislots possibly limited");
        NS_ASSERT_MSG(minislotsToAllocate > maxLMinislots, "Arithmetic overflow");
        cMinislotsAllocated =
            std::min<uint32_t>((minislotsToAllocate - maxLMinislots), maxCMinislots);
        lMinislotsAllocated = maxLMinislots;
    }
    else if (maxLMinislots > lWeightedMinislotsToAllocate &&
             maxCMinislots <= cWeightedMinislotsToAllocate)
    {
        NS_LOG_DEBUG("L minislots possibly limited");
        NS_ASSERT_MSG(minislotsToAllocate > maxCMinislots, "Arithmetic overflow");
        cMinislotsAllocated = maxCMinislots;
        lMinislotsAllocated =
            std::min<uint32_t>((minislotsToAllocate - maxCMinislots), maxLMinislots);
    }
    NS_LOG_DEBUG("C minislots allocated: " << cMinislotsAllocated
                                           << " L minislots allocated: " << lMinislotsAllocated);
    return std::make_pair(cMinislotsAllocated, lMinislotsAllocated);
}

std::pair<uint32_t, uint32_t>
CmtsUpstreamScheduler::ScheduleGrant(std::map<uint32_t, Grant>& grantList,
                                     const std::map<uint32_t, Grant>& pgsGrantList,
                                     uint32_t pgsMinislots,
                                     uint32_t cMinislotsToSchedule,
                                     uint32_t lMinislotsToSchedule)
{
    NS_LOG_FUNCTION(this << pgsMinislots << cMinislotsToSchedule << lMinislotsToSchedule);

    uint32_t cScheduledMinislots = 0;
    uint32_t lScheduledMinislots = pgsMinislots;

    if (pgsMinislots)
    {
        // The PGS grant list dictates the placement of low latency service
        // flow grants within the MAP interval.  If there are low latency
        // minislots to further grant, try to append them to the earliest
        // PGS grant(s).
        if (pgsMinislots < lMinislotsToSchedule)
        {
            NS_LOG_DEBUG("Additional best-effort minislots to be appended");
            lMinislotsToSchedule -= pgsMinislots; // schedule additional minislots below
        }
        else
        {
            NS_LOG_DEBUG("PGS grants can cover the L request backlog");
            lMinislotsToSchedule = 0; // PGS minislots can cover the lGrantedBytes
        }
        for (auto it = pgsGrantList.begin(); it != pgsGrantList.end(); it++)
        {
            Grant lGrant;
            lGrant.m_sfid = LOW_LATENCY_SFID;
            if (lMinislotsToSchedule)
            {
                // Determine space available until the next grant or end of MAP
                uint32_t space = 0;
                auto it2 = std::next(it);
                if (it2 != pgsGrantList.end())
                {
                    // There is another grant following
                    space = it2->second.m_offset - (it->second.m_offset + it->second.m_length);
                    NS_LOG_DEBUG("Available minislots until next grant: " << space);
                }
                else
                {
                    // Last PGS grant in the MAP; calculate space to next MAP
                    space = GetUpstream()->GetMinislotsPerMap() -
                            (it->second.m_offset + it->second.m_length);
                    NS_LOG_DEBUG("Available minislots until end of interval: " << space);
                }
                if (lMinislotsToSchedule <= space)
                {
                    NS_LOG_DEBUG("Appending " << lMinislotsToSchedule << " minislots to PGS grant");
                    lGrant.m_length = it->second.m_length + lMinislotsToSchedule;
                    lScheduledMinislots += lMinislotsToSchedule;
                    lMinislotsToSchedule = 0;
                }
                else
                {
                    lGrant.m_length = it->second.m_length + space;
                    NS_LOG_DEBUG("Appending " << space << " minislots to PGS grant; expanding to "
                                              << lGrant.m_length);
                    lScheduledMinislots += space;
                    lMinislotsToSchedule -= space;
                }
            }
            else
            {
                // This clause covers the case in which the PGS grant list
                // just needs to be mapped to the final grant list
                NS_LOG_DEBUG("Including a PGS grant of length "
                             << it->second.m_length << " with no additional best-effort minislots");
                lGrant.m_length = it->second.m_length;
            }
            lGrant.m_offset = it->second.m_offset;
            NS_LOG_DEBUG("Scheduling LL grant of length " << lGrant.m_length << " offset "
                                                          << lGrant.m_offset);
            grantList.insert(std::pair<uint32_t, Grant>(lGrant.m_offset, lGrant));
        }
        if (lMinislotsToSchedule)
        {
            NS_LOG_DEBUG("Unable to fully append lMinislots; " << lMinislotsToSchedule
                                                               << " remaining");
            // Schedule remaining minislots in free space.  This loop will
            // iterate all grants and check the space between the previous
            // grant and the next grant
            uint32_t nextCheckedOffset = 0;
            for (auto grantIt = grantList.begin();
                 grantIt != grantList.end() && lMinislotsToSchedule;
                 grantIt++)
            {
                uint32_t available = grantIt->second.m_offset - nextCheckedOffset;
                if (available >= lMinislotsToSchedule)
                {
                    Grant lGrant;
                    lGrant.m_sfid = LOW_LATENCY_SFID;
                    lGrant.m_length = lMinislotsToSchedule;
                    lGrant.m_offset = nextCheckedOffset;
                    NS_LOG_DEBUG("Schedule low latency grant of length "
                                 << lGrant.m_length << " offset " << lGrant.m_offset);
                    // Consider emplace in future revisions
                    grantList.insert(std::pair<uint32_t, Grant>(lGrant.m_offset, lGrant));
                    lScheduledMinislots += lMinislotsToSchedule;
                    lMinislotsToSchedule = 0;
                    break;
                }
                else
                {
                    Grant lGrant;
                    lGrant.m_sfid = LOW_LATENCY_SFID;
                    lGrant.m_length = available;
                    lGrant.m_offset = nextCheckedOffset;
                    NS_LOG_DEBUG("Schedule low latency grant of length "
                                 << lGrant.m_length << " offset " << lGrant.m_offset);
                    // Consider emplace in future revisions
                    grantList.insert(std::pair<uint32_t, Grant>(lGrant.m_offset, lGrant));
                    lMinislotsToSchedule -= available;
                    lScheduledMinislots += available;
                }
                nextCheckedOffset = grantIt->second.m_offset + grantIt->second.m_length;
            }
            if (lMinislotsToSchedule)
            {
                // Above loop will not consider the space between the last
                // PGS grant and end of frame
                auto grantRit = grantList.rbegin();
                nextCheckedOffset = grantRit->second.m_offset + grantRit->second.m_length;
                uint32_t available = GetUpstream()->GetMinislotsPerMap() - nextCheckedOffset;
                // An exhaustive search of free space should not leave any
                // slots unallocated, so signal an error if any are left
                NS_ABORT_MSG_IF(lMinislotsToSchedule > available,
                                "Unable to fully allocate remaining lMinislots: "
                                    << lMinislotsToSchedule - available);
                Grant lGrant;
                lGrant.m_sfid = LOW_LATENCY_SFID;
                lGrant.m_length = lMinislotsToSchedule;
                lGrant.m_offset = nextCheckedOffset;
                NS_LOG_DEBUG("Schedule low latency grant of length "
                             << lGrant.m_length << " offset " << lGrant.m_offset);
                lScheduledMinislots += lMinislotsToSchedule;
                lMinislotsToSchedule = 0;
                // Consider emplace in future revisions
                grantList.insert(std::pair<uint32_t, Grant>(lGrant.m_offset, lGrant));
            }
        }
        // Low latency grants are scheduled; next, consider classic grants
        if (cMinislotsToSchedule > 0)
        {
            // Look for all unallocated minislots and create as
            // many classic grants as needed.
            uint32_t nextCheckedOffset = 0;
            for (auto grantIt = grantList.begin();
                 grantIt != grantList.end() && cMinislotsToSchedule;
                 grantIt++)
            {
                uint32_t available = grantIt->second.m_offset - nextCheckedOffset;
                if (available > cMinislotsToSchedule)
                {
                    Grant cGrant;
                    cGrant.m_sfid = CLASSIC_SFID;
                    cGrant.m_length = cMinislotsToSchedule;
                    cGrant.m_offset = nextCheckedOffset;
                    NS_LOG_DEBUG("Schedule classic grant of length "
                                 << cGrant.m_length << " offset " << cGrant.m_offset);
                    // Consider emplace in future revisions
                    grantList.insert(std::pair<uint32_t, Grant>(cGrant.m_offset, cGrant));
                    cScheduledMinislots += cMinislotsToSchedule;
                    cMinislotsToSchedule = 0;
                    break;
                }
                else
                {
                    Grant cGrant;
                    cGrant.m_sfid = CLASSIC_SFID;
                    cGrant.m_length = available;
                    cGrant.m_offset = nextCheckedOffset;
                    NS_LOG_DEBUG("Schedule classic grant of length "
                                 << cGrant.m_length << " offset " << cGrant.m_offset);
                    // Consider emplace in future revisions
                    grantList.insert(std::pair<uint32_t, Grant>(cGrant.m_offset, cGrant));
                    cMinislotsToSchedule -= available;
                    cScheduledMinislots += available;
                }
                nextCheckedOffset = grantIt->second.m_offset + grantIt->second.m_length;
            }
            if (cMinislotsToSchedule)
            {
                // Above loop will not consider the space between the last
                // PGS grant and end of frame; check it here
                auto grantRit = grantList.rbegin();
                nextCheckedOffset = grantRit->second.m_offset + grantRit->second.m_length;
                uint32_t available = GetUpstream()->GetMinislotsPerMap() - nextCheckedOffset;
                // An exhaustive search of free space should not leave any
                // slots unallocated, so signal an error if any are left
                NS_ABORT_MSG_IF(cMinislotsToSchedule > available,
                                "Unable to fully allocate cMinislots " << cMinislotsToSchedule);
                Grant cGrant;
                cGrant.m_sfid = CLASSIC_SFID;
                cGrant.m_length = cMinislotsToSchedule;
                cGrant.m_offset = nextCheckedOffset;
                NS_LOG_DEBUG("Schedule classic grant of length " << cGrant.m_length << " offset "
                                                                 << cGrant.m_offset);
                // Consider emplace in future revisions
                grantList.insert(std::pair<uint32_t, Grant>(cGrant.m_offset, cGrant));
                cScheduledMinislots += cMinislotsToSchedule;
            }
        }
    }
    else
    {
        // Schedule classic and low latency grants to be back-to-back, with low
        // latency one scheduled first
        uint32_t maxStartingMinislot =
            GetUpstream()->GetMinislotsPerMap() - (cMinislotsToSchedule + lMinislotsToSchedule);
        // Usually this is a uniform random variable, but in test suites, it may
        // be a deterministic variable for testing purposes, and may not support
        // the Min/Max attributes.  As a result, use the SetAttribute variants
        // that are allowed to silently fail if the attribute doesn't exist.
        m_schedulerVariable->SetAttributeFailSafe("Min", DoubleValue(0));
        m_schedulerVariable->SetAttributeFailSafe("Max", DoubleValue(maxStartingMinislot + 1));
        uint32_t startingMinislot = std::round<uint32_t>(m_schedulerVariable->GetValue());
        Grant lGrant;
        lGrant.m_sfid = LOW_LATENCY_SFID;
        lGrant.m_length = lMinislotsToSchedule;
        lGrant.m_offset = startingMinislot;
        lScheduledMinislots = lGrant.m_length;
        Grant cGrant;
        cGrant.m_sfid = CLASSIC_SFID;
        cGrant.m_length = cMinislotsToSchedule;
        cGrant.m_offset = startingMinislot + lGrant.m_length;
        cScheduledMinislots = cGrant.m_length;
        if (cMinislotsToSchedule > 0)
        {
            grantList.insert(std::pair<uint32_t, Grant>(cGrant.m_offset, cGrant));
            NS_LOG_DEBUG("Scheduled " << cMinislotsToSchedule << " slots for SFID 1 at "
                                      << cGrant.m_offset);
        }
        if (lMinislotsToSchedule > 0)
        {
            grantList.insert(std::pair<uint32_t, Grant>(lGrant.m_offset, lGrant));
            NS_LOG_DEBUG("Scheduled " << lMinislotsToSchedule << " slots for SFID 2 at "
                                      << lGrant.m_offset);
        }
        NS_LOG_DEBUG("Returning with grant list of length " << grantList.size());
    }
    NS_LOG_DEBUG("Scheduled total of " << grantList.size() << " grants with " << cScheduledMinislots
                                       << " C minislots and " << lScheduledMinislots
                                       << " L minislots");
    return std::make_pair(cScheduledMinislots, lScheduledMinislots);
}

int64_t
CmtsUpstreamScheduler::AssignStreams(int64_t stream)
{
    m_schedulerVariable->SetStream(stream++);
    m_congestionVariable->SetStream(stream);
    return 2;
}

} // namespace docsis
} // namespace ns3
