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

#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/random-variable-stream.h"
#include "cmts-upstream-scheduler.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CmtsUpstreamScheduler");

namespace docsis {

NS_OBJECT_ENSURE_REGISTERED (CmtsUpstreamScheduler);

TypeId 
CmtsUpstreamScheduler::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::CmtsUpstreamScheduler")
    .SetParent<Object> ()
    .SetGroupName ("Docsis")
    .AddConstructor<CmtsUpstreamScheduler> ()
    .AddAttribute ("FreeCapacityMean",
                   "Average upstream free capacity (bits/sec)",
                   DoubleValue (0),
                   MakeDoubleAccessor (&CmtsUpstreamScheduler::m_freeCapacityMean),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("FreeCapacityVariation",
                   "Bound (percent) on the variation of upstream free capacity",
                   DoubleValue (0),
                   MakeDoubleAccessor (&CmtsUpstreamScheduler::m_freeCapacityVariation),
                   MakeDoubleChecker<double> (0,100))
    .AddAttribute ("CongestionVariable", "A RandomVariableStream used to pick the level of simulated congestion",
                   StringValue ("ns3::UniformRandomVariable[Max=65535]"),
                   MakePointerAccessor (&CmtsUpstreamScheduler::m_congestionVariable),
                   MakePointerChecker<RandomVariableStream> ())
    .AddAttribute ("GrantAllocationPolicy", "Grant allocation policy (Single or Spread)",
                   EnumValue (SINGLE),
                   MakeEnumAccessor (&CmtsUpstreamScheduler::m_grantAllocPolicy),
                   MakeEnumChecker (CmtsUpstreamScheduler::SINGLE, "Single",
                                    CmtsUpstreamScheduler::SPREAD, "Spread"))
    .AddAttribute ("SchedulingWeight",
                   "Proportion (SchedulingWeight/256) of grant to distribute to the Low Latency service flow",
                   UintegerValue (230),
                   MakeUintegerAccessor (&CmtsUpstreamScheduler::m_schedulingWeight),
                   MakeUintegerChecker<uint8_t> (1,255))
    .AddAttribute ("MeanPacketSize",
                   "Assumed mean packet size (bytes) to estimate number of MAC header bytes in requests",
                   UintegerValue (200),
                   MakeUintegerAccessor (&CmtsUpstreamScheduler::m_meanPacketSize),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("SchedulerRandomVariable", "A RandomVariableStream used to place the grant in the MAP",
                   StringValue ("ns3::UniformRandomVariable"),
                   MakePointerAccessor (&CmtsUpstreamScheduler::m_schedulerVariable),
                   MakePointerChecker<RandomVariableStream> ())
  ;
  return tid;
}

CmtsUpstreamScheduler::CmtsUpstreamScheduler ()
{
  NS_LOG_FUNCTION (this);
}

CmtsUpstreamScheduler::~CmtsUpstreamScheduler ()
{
  NS_LOG_FUNCTION (this);
}

void
CmtsUpstreamScheduler::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_upstream = 0;
  m_asf = 0;
  m_sf = 0;
  m_classicSf = 0;
  m_llSf = 0;
}

void
CmtsUpstreamScheduler::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
}

void
CmtsUpstreamScheduler::SetUpstream (Ptr<CmNetDevice> upstream)
{
  NS_LOG_FUNCTION (this << upstream);
  m_upstream = upstream;
}

Ptr<CmNetDevice>
CmtsUpstreamScheduler::GetUpstream (void) const
{
  return m_upstream;
}

void
CmtsUpstreamScheduler::ReceiveRequest (GrantRequest request)
{
  NS_LOG_FUNCTION (this << request.m_sfid << request.m_bytes);
  NS_ASSERT_MSG (request.m_requestTime >= m_lastRequestTime, "grant request times should increase");
  m_lastRequestTime = request.m_requestTime;
  // Check if request exists; if so, amend, if not insert 
  auto it = m_requests.find (request.m_sfid);
  if (it == m_requests.end ())
    {
      NS_LOG_DEBUG ("Inserting " << request.m_bytes << " bytes to new entry for sfid " << request.m_sfid);
      m_requests.insert ({request.m_sfid, request.m_bytes});
    }
  else
    {
      NS_LOG_DEBUG ("Adding " << request.m_bytes << " bytes to existing " << (*it).second);
      (*it).second += request.m_bytes;
    }
}

void
CmtsUpstreamScheduler::ReceiveUnusedLGrantUpdate (CmNetDevice::LGrantState state)
{
  NS_LOG_FUNCTION (this << state.unused);
  NS_LOG_DEBUG ("Replenishing " << state.unused << " tokens to MSR token bucket containing " << m_tokenBucketState.m_msrTokens);
  m_tokenBucketState.m_msrTokens += state.unused;
}

void
CmtsUpstreamScheduler::ReceiveUnusedCGrantUpdate (CmNetDevice::CGrantState state)
{
  NS_LOG_FUNCTION (this << state.unused);
  NS_LOG_DEBUG ("Replenishing " << state.unused << " tokens to MSR token bucket containing " << m_tokenBucketState.m_msrTokens);
  m_tokenBucketState.m_msrTokens += state.unused;
}

void
CmtsUpstreamScheduler::Start (uint32_t allocStartTime)
{
  NS_LOG_FUNCTION (this);
  GenerateMap (allocStartTime);
}

void
CmtsUpstreamScheduler::GenerateMap (uint32_t allocStartTime)
{
  NS_LOG_FUNCTION (this);

  // Calculate number of minislots available
  uint32_t freeMinislots = CalculateFreeCapacity ();
  NS_LOG_DEBUG ("Free minislots = " << freeMinislots);

  Time mapMessageArrivalDelay = GetUpstream ()->GetDsIntlvDelay () + GetUpstream ()->GetDsSymbolTime () * 3 + GetUpstream ()->GetRtt ()/2 + GetUpstream ()->GetCmtsMapProcTime ();

  // Allocate grants based on priority order of service flows
  // Initial implementation is not generalized; either one or two SFs are
  // present.  Focus on single service flow case initially.

  bool dataGrantPending = false;
  std::map<uint32_t, Grant> grantList;
  std::pair<int32_t, uint32_t> tokens = IncrementTokenBucketCounters ();
  if (tokens.first || tokens.second)
    {
      NS_LOG_DEBUG ("Updating token bucket counters to " << tokens.first << " msr and " << tokens.second << " peak");
    }
  uint32_t cRequestBacklog = m_requests[m_classicSf->m_sfid];
  uint32_t lRequestBacklog = 0;
  uint32_t lRequestBacklogOrGuarantee = 0;
  uint64_t ggr = 0;
  if (m_llSf)
    {
      lRequestBacklog = m_requests[m_llSf->m_sfid];
      lRequestBacklogOrGuarantee = lRequestBacklog;
      ggr = m_llSf->m_guaranteedGrantRate.GetBitRate ();
    }
  // If lRequestBacklog is less than the current GGR, increase it
  uint32_t ggrBytesPerMap = static_cast<uint32_t> ((ggr/8) * GetUpstream ()->GetActualMapInterval ().GetSeconds () );
  if (m_llSf && lRequestBacklogOrGuarantee < ggrBytesPerMap)
    {
      NS_LOG_DEBUG ("Increasing LL request to guaranteed grant rate of " << ggr << " bps");
      lRequestBacklogOrGuarantee = ggrBytesPerMap;
    }

  // Determine if grants are to be made to the aggregate service flow
  // Grant is the minimum of MSR tokens, peak rate tokens, and grant (in bytes)
  uint32_t availableTokens = 0;
  if (tokens.first < 0)
    {
      // This implementation will not grant if there is a deficit
      NS_LOG_DEBUG ("MSR tokens less than zero");
    }
  if (tokens.first > 0)
    {
      availableTokens = std::min (static_cast<uint32_t> (tokens.first), tokens.second);
    }
  uint32_t shapedBytes = std::min ((cRequestBacklog + lRequestBacklogOrGuarantee), availableTokens);

  // Perform grant allocation
  uint32_t cGrantedBytes = 0;
  uint32_t lGrantedBytes = 0;
  uint32_t cMinislots = 0;
  uint32_t lMinislots = 0;
  if (shapedBytes > 0)
    {
      NS_LOG_DEBUG ("Rate shaping allocated " << shapedBytes << " grant bytes from overall request of " << (cRequestBacklog + lRequestBacklogOrGuarantee));
      if (shapedBytes < (cRequestBacklog + lRequestBacklog))
        {
          NS_LOG_DEBUG ("Setting Data Grant Pending flag");
          dataGrantPending = true;
        }
      // Allocate bytes to the two service flows, bounded by free capacity
      std::pair<uint32_t, uint32_t> allocation = Allocate (cRequestBacklog, lRequestBacklogOrGuarantee, shapedBytes, freeMinislots);
      NS_LOG_DEBUG ("Scheduler allocated " << allocation.first << " to SFID 1 and " << allocation.second << " to SFID 2");
      cGrantedBytes = allocation.first;
      lGrantedBytes = allocation.second;
      cMinislots = MinislotCeil (cGrantedBytes);
      lMinislots = MinislotCeil (lGrantedBytes);
    }
  if (cMinislots || lMinislots)
    {
      NS_ABORT_MSG_UNLESS (cMinislots + lMinislots <= freeMinislots, "Too many granted minislots"); // Check on results of Allocate ()
      if (m_grantAllocPolicy == SINGLE) 
        {
          ScheduleGrant (grantList, cMinislots, lMinislots);
        }
      else
        {
          SpreadAndScheduleGrant (grantList, cMinislots, lMinislots);
        }
      // We granted possibly more bytes than requested by rounding up to
      // a minislot boundary.  Also, PGS service may cause overallocation.
      // Nevertheless, subtract the granted bytes from the token bucket.
      // Unused bytes will be redeposited later based on unused grant
      // tracking.
      DecrementTokenBucketCounters (MinislotsToBytes (cMinislots) + MinislotsToBytes (lMinislots));
      if (m_llSf && MinislotsToBytes (lMinislots) > m_requests[m_llSf->m_sfid])
        {
          NS_LOG_DEBUG ("Allocated " << MinislotsToBytes (lMinislots) << " for smaller LL request of " << m_requests[m_llSf->m_sfid]);
        }
      if (MinislotsToBytes (cMinislots) > m_requests[m_classicSf->m_sfid])
        {
          NS_LOG_DEBUG ("Allocated " << MinislotsToBytes (cMinislots) << " for smaller classic request of " << m_requests[m_classicSf->m_sfid]);
        }
      // Subtract the full number of bytes granted from the request backlog
      if (MinislotsToBytes (cMinislots) <= m_requests[m_classicSf->m_sfid])
        {
          m_requests[m_classicSf->m_sfid] -= MinislotsToBytes (cMinislots);
        }
      else
        {
          m_requests[m_classicSf->m_sfid] = 0;
        }
      if (m_llSf)
        {
          if (MinislotsToBytes (lMinislots) <= m_requests[m_llSf->m_sfid])
            {
              m_requests[m_llSf->m_sfid] -= MinislotsToBytes (lMinislots);
            }
          else
            {
              m_requests[m_llSf->m_sfid] = 0;
            }
        }
    }

  // Build MAP message from the list of grants
  MapMessage mapMessage = BuildMapMessage (allocStartTime, grantList, dataGrantPending);

  // Schedule the allocation MAP message for arrival at the CM
  NS_LOG_DEBUG ("Schedule MAP message arrival at " << (Simulator::Now () + mapMessageArrivalDelay).As (Time::S));
  Simulator::Schedule (mapMessageArrivalDelay, &CmNetDevice::HandleMapMsg, GetUpstream (), mapMessage);

  // Reschedule for next MAP interval
  NS_LOG_DEBUG ("Schedule next GenerateMap() at " << (Simulator::Now () + GetUpstream ()->GetActualMapInterval()).As (Time::S));
  allocStartTime += GetUpstream ()->TimeToMinislots (GetUpstream ()->GetActualMapInterval ());
  Simulator::Schedule (GetUpstream ()->GetActualMapInterval (), &CmtsUpstreamScheduler::GenerateMap, this, allocStartTime);
}

MapMessage
CmtsUpstreamScheduler::BuildMapMessage (uint32_t allocStartTime, const std::map<uint32_t, Grant>& grantList, bool dataGrantPending)
{
  NS_LOG_FUNCTION (this);
  MapMessage mapMessage;
  mapMessage.m_dataGrantPending = dataGrantPending;
  mapMessage.m_numIe = 0;
  mapMessage.m_allocStartTime = allocStartTime;
  mapMessage.m_ackTime = m_lastRequestTime;
  NS_LOG_DEBUG ("Alloc start time " << allocStartTime << " ack time " << m_lastRequestTime);
  NS_ASSERT_MSG (allocStartTime > m_lastRequestTime, "Alloc start time should be later than Ack time");
  MapIe currentIe;
  uint32_t nextOffset;
  if (grantList.size () == 0)
    {
      NS_LOG_DEBUG ("Returning null MapMessage");
      return mapMessage;
    }
  auto it = grantList.begin ();
  NS_LOG_DEBUG ("Size of grant list " << grantList.size ());
  if (it->second.m_offset > 0)
    {
      NS_LOG_DEBUG ("Add initial null IE");
      currentIe.m_sfid = 0;
      currentIe.m_offset = 0;
      mapMessage.m_mapIeList.push_back (currentIe);
    }
  
  // Add a new IE followed by a delimiter if needed
  while (it != grantList.end ())
    {
      currentIe.m_sfid = it->second.m_sfid;
      currentIe.m_offset = it->second.m_offset;
      nextOffset = it->second.m_length + it->second.m_offset;
      NS_LOG_DEBUG ("Add grant IE for SFID " << currentIe.m_sfid << " offset " << currentIe.m_offset);
      mapMessage.m_mapIeList.push_back (currentIe);
      ++it;
      if (it == grantList.end ())
        {
          NS_LOG_DEBUG ("Add terminating null IE");
          currentIe.m_sfid = 0;
          currentIe.m_offset = nextOffset;
          mapMessage.m_mapIeList.push_back (currentIe);
          break;
        }
      else if (it->second.m_offset == nextOffset)
        {
          NS_LOG_DEBUG ("Continue to an adjacent grant");
          continue;
        }
      else if (it->second.m_offset > nextOffset)
        {
          NS_LOG_DEBUG ("Add null IE between grants");
          currentIe.m_sfid = 0;
          currentIe.m_offset = nextOffset;
          mapMessage.m_mapIeList.push_back (currentIe);
          continue;
        }
      else if (it->second.m_offset > nextOffset)
        {
          NS_FATAL_ERROR ("grant list overlapping");
        }
    }
  return mapMessage;
}

uint32_t
CmtsUpstreamScheduler::CalculateFreeCapacity (void)
{
  NS_LOG_FUNCTION (this);
  if (m_freeCapacityMean == 0)
    {
      // Special value of zero signifies that user has not configured to
      // a specific value, so change it to the upstream capacity
      NS_LOG_DEBUG ("No FreeCapacityMean configured; returning full number of minislots per MAP");
      return GetUpstream ()->GetMinislotsPerMap ();
    }
  uint32_t maxGrant = static_cast<uint32_t> (std::round (m_freeCapacityMean * GetUpstream ()->GetActualMapInterval ().GetSeconds () / 8)); // bytes
  NS_LOG_DEBUG ("FreeCapacityMean (bps) " << m_freeCapacityMean << " maxGrant (bytes/MAP) " << maxGrant);
  if (m_freeCapacityVariation == 0)
    {
      uint32_t freeMinislots = MinislotRound (maxGrant);
      freeMinislots = std::min<uint32_t> (freeMinislots, GetUpstream ()->GetMinislotsPerMap ());
      NS_LOG_DEBUG ("free minislots: " << freeMinislots << " minislots per map " << GetUpstream ()->GetMinislotsPerMap ());
      return freeMinislots;
    }
  // Allow maxGrant to range lower or higher, based on the allowable
  // variation and a random variate
  int32_t w = int (m_freeCapacityVariation * maxGrant / 100);
  int32_t congestionVariate = int (m_congestionVariable->GetInteger ());
  int64_t r = (w == 0) ? 0 : congestionVariate % (2 * w) - w;
  // maximum free bytes in the next MAP is maxgrant + r
  int64_t nextmax = maxGrant + r;
  // unsignedNextMax is the free capacity in the MAP interval, in bytes
  uint32_t unsignedNextMax = nextmax < 0 ? 0 : (uint32_t) nextmax;
  NS_LOG_DEBUG ("w: " << w << " rv: " << congestionVariate << " r: " << r << " maximumGrant: " << unsignedNextMax);
  NS_ABORT_MSG_UNLESS (r >= (w * -1) && r <= w, "Error in r " << r);
  uint32_t freeMinislots = MinislotRound (unsignedNextMax);
  // Cap free minislots to the MAP capacity
  freeMinislots = std::min<uint32_t> (freeMinislots, GetUpstream ()->GetMinislotsPerMap ());
  NS_LOG_DEBUG ("free minislots: " << freeMinislots);
  return freeMinislots;
}

uint32_t
CmtsUpstreamScheduler::MinislotRound (uint32_t bytes) const
{
  return static_cast<uint32_t> (std::round (static_cast<double> (bytes) / GetUpstream ()->GetMinislotCapacity ()));
}

uint32_t
CmtsUpstreamScheduler::MinislotCeil (uint32_t bytes) const
{
  return static_cast<uint32_t> (std::ceil (static_cast<double> (bytes) / GetUpstream ()->GetMinislotCapacity ()));
}

uint32_t
CmtsUpstreamScheduler::MinislotsToBytes (uint32_t minislots) const
{
  return minislots * GetUpstream ()->GetMinislotCapacity ();
}

void
CmtsUpstreamScheduler::ProvisionAggregateServiceFlow (Ptr<AggregateServiceFlow> flow)
{
  NS_LOG_FUNCTION (this);
  NS_ABORT_MSG_IF (m_sf, "Cannot provision both an ASF and a single SF");
  if (m_asf)
    {
      NS_LOG_WARN ("Removing and replacing existing aggregate service flow");
    }
  m_asf = flow;
  m_classicSf = flow->GetClassicServiceFlow ();
  m_llSf = flow->GetLowLatencyServiceFlow ();
  // Adjust flow rates to account for MAC header bytes in bandwidth requests
  // MSR_to_use = MSR_Configured*(MeanPktSize + DOCSISHeaderSize)/MeanPktSize
  NS_LOG_DEBUG ("Increasing MSR and peak by factor of " << static_cast<double> (m_meanPacketSize + GetUpstream ()->GetUsMacHdrSize ()) / m_meanPacketSize);
  flow->m_maxSustainedRate = DataRate (static_cast<uint64_t> (flow->m_maxSustainedRate.GetBitRate () * static_cast<double> (m_meanPacketSize + GetUpstream ()->GetUsMacHdrSize ()) / m_meanPacketSize));
  flow->m_peakRate = DataRate (static_cast<uint64_t> (flow->m_peakRate.GetBitRate () * static_cast<double> (m_meanPacketSize + GetUpstream ()->GetUsMacHdrSize ()) / m_meanPacketSize));
  m_tokenBucketState.m_msrTokens = static_cast<int32_t> (flow->m_maxTrafficBurst);
  m_tokenBucketState.m_peakTokens = 0;
  m_tokenBucketState.m_lastIncrement = Simulator::Now ();
  m_tokenBucketState.m_lastDrain = Simulator::Now ();
}

Ptr<const AggregateServiceFlow>
CmtsUpstreamScheduler::GetAggregateServiceFlow (void) const
{
  return m_asf;
}

void
CmtsUpstreamScheduler::RemoveAggregateServiceFlow (void)
{
  m_asf = 0;
  m_classicSf = 0;
  m_llSf = 0;
}

void
CmtsUpstreamScheduler::ProvisionSingleServiceFlow (Ptr<ServiceFlow> flow)
{
  NS_LOG_FUNCTION (this);
  NS_ABORT_MSG_IF (m_asf, "Cannot provision both an ASF and a single SF");
  if (m_sf)
    {
      NS_LOG_WARN ("Removing and replacing existing single service flow " << flow->m_sfid);
    }
  m_asf = 0;
  m_sf = flow;
  if (m_sf->m_sfid == CLASSIC_SFID)
    {
      m_classicSf = flow;
      m_llSf = 0;
    }
  else
    {
      m_llSf = flow;
      m_classicSf = 0;
    }
  // Adjust flow rates to account for MAC header bytes in bandwidth requests
  // MSR_to_use = MSR_Configured*(MeanPktSize + DOCSISHeaderSize)/MeanPktSize
  NS_LOG_DEBUG ("Increasing MSR and peak by factor of " << static_cast<double> (m_meanPacketSize + GetUpstream ()->GetUsMacHdrSize ()) / m_meanPacketSize);
  flow->m_maxSustainedRate = DataRate (static_cast<uint64_t> (flow->m_maxSustainedRate.GetBitRate () * static_cast<double> (m_meanPacketSize + GetUpstream ()->GetUsMacHdrSize ()) / m_meanPacketSize));
  flow->m_peakRate = DataRate (static_cast<uint64_t> (flow->m_peakRate.GetBitRate () * static_cast<double> (m_meanPacketSize + GetUpstream ()->GetUsMacHdrSize ()) / m_meanPacketSize));
  m_tokenBucketState.m_msrTokens = static_cast<int32_t> (flow->m_maxTrafficBurst);
  m_tokenBucketState.m_peakTokens = 0;
  m_tokenBucketState.m_lastIncrement = Simulator::Now ();
  m_tokenBucketState.m_lastDrain = Simulator::Now ();
}

Ptr<const ServiceFlow>
CmtsUpstreamScheduler::GetSingleServiceFlow (void) const
{
  return m_sf;
}

void
CmtsUpstreamScheduler::RemoveSingleServiceFlow (void)
{
  m_sf = 0;
  m_classicSf = 0;
  m_llSf = 0;
}

std::pair<int32_t, uint32_t>
CmtsUpstreamScheduler::IncrementTokenBucketCounters (void)
{
  NS_LOG_FUNCTION (this);
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
  // Token bucket state referenced by iterator stateIt
  Time interval = Simulator::Now () - m_tokenBucketState.m_lastIncrement;
  NS_ASSERT_MSG (interval >= Seconds (0), "Error:  negative time");
  uint32_t tokenIncrement = static_cast<uint32_t> (interval.GetSeconds () * msr.GetBitRate () / 8);
  m_tokenBucketState.m_msrTokens += tokenIncrement;
  if (m_tokenBucketState.m_msrTokens > static_cast<int32_t> (mtb))
    {
      NS_LOG_DEBUG ("msrTokens limited to " << mtb);
      m_tokenBucketState.m_msrTokens = static_cast<int32_t> (mtb);
    }
  m_tokenBucketState.m_peakTokens = static_cast<uint32_t> (interval.GetSeconds () * peak.GetBitRate () / 8);
  m_tokenBucketState.m_lastIncrement = Simulator::Now ();
  return std::pair<int32_t, uint32_t> (m_tokenBucketState.m_msrTokens, m_tokenBucketState.m_peakTokens);
}

void
CmtsUpstreamScheduler::DecrementTokenBucketCounters (uint32_t decrement)
{
  NS_LOG_FUNCTION (this << decrement);

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
  if (m_tokenBucketState.m_msrTokens > static_cast<int32_t> (decrement))
    {
      m_tokenBucketState.m_msrTokens -= static_cast<int32_t> (decrement);
    }
  else
    {
      // This scheduler policy is to not allow tokens to go negative
      m_tokenBucketState.m_msrTokens = 0;
    }
  Time interval = Simulator::Now () - m_tokenBucketState.m_lastDrain;
   // Check for conformance
  if (decrement > ((interval.GetSeconds () * msr.GetBitRate () / 8) + mtb))
    {
      NS_FATAL_ERROR ("Token bucket limits (MSR) exceeded");
    }
  else if (decrement > ((interval.GetSeconds () * peak.GetBitRate () / 8) + 1522))
    {
      NS_FATAL_ERROR ("Token bucket limits (peak) exceeded");
    }
  m_tokenBucketState.m_lastDrain = Simulator::Now ();
}

std::pair<uint32_t, uint32_t>
CmtsUpstreamScheduler::Allocate (uint32_t cRequestedBytes, uint32_t lRequestedBytes, uint32_t grantBytes, uint32_t freeMinislots)
{
  NS_LOG_FUNCTION (this << cRequestedBytes << lRequestedBytes << grantBytes << freeMinislots);
  std::pair<uint32_t, uint32_t> returnValue;
  // Allocate up to m_schedulingWeight of grantBytes to low latency flow
  uint32_t lGrantBytes = static_cast<uint32_t> (static_cast<double> (grantBytes) * m_schedulingWeight / 256);
  if (lGrantBytes > lRequestedBytes)
    {
      lGrantBytes = lRequestedBytes;
    }
  uint32_t cGrantBytes = grantBytes - lGrantBytes;
  if (cGrantBytes > cRequestedBytes)
    {
      cGrantBytes = cRequestedBytes;
    }
  // Any leftover bytes go to low latency
  if (cGrantBytes + lGrantBytes < grantBytes)
    {
      lGrantBytes = grantBytes - cGrantBytes;
    }
  // Ensure that total number of free minislots is not exceeded
  while (MinislotCeil (cGrantBytes) + MinislotCeil (lGrantBytes) > freeMinislots)
    {
      if (cGrantBytes > 0)
        {
          NS_LOG_DEBUG ("Decrementing classic bytes " << cGrantBytes << " to fit within free minislots");
          cGrantBytes -= 1;
        }
      else if (lGrantBytes > 0)
        {
          NS_LOG_DEBUG ("Decrementing latency bytes " << lGrantBytes << " to fit within free minislots");
          lGrantBytes -= 1;
        }
      else
        {
          NS_FATAL_ERROR ("This branch should be unreachable");
        }
    }
  returnValue.first = cGrantBytes;
  returnValue.second = lGrantBytes;
  return returnValue;
}

void
CmtsUpstreamScheduler::ScheduleGrant (std::map<uint32_t, Grant>& grantList, uint32_t cMinislots, uint32_t lMinislots)
{
  NS_LOG_FUNCTION (this << cMinislots << lMinislots);

  // Schedule classic and low latency grants to be back-to-back, with low
  // latency one scheduled first
  uint32_t maxStartingMinislot = GetUpstream ()->GetMinislotsPerMap () - (cMinislots + lMinislots);
  // Usually this is a uniform random variable, but in test suites, it may
  // be a deterministic variable for testing purposes, and may not support
  // the Min/Max attributes.  As a result, use the SetAttribute variants
  // that are allowed to silently fail if the attribute doesn't exist.
  m_schedulerVariable->SetAttributeFailSafe ("Min", DoubleValue (0));
  m_schedulerVariable->SetAttributeFailSafe ("Max", DoubleValue (maxStartingMinislot + 1));
  uint32_t startingMinislot = std::round<uint32_t> (m_schedulerVariable->GetValue ());
  Grant lGrant;
  lGrant.m_sfid = LOW_LATENCY_SFID;
  lGrant.m_length = lMinislots;
  lGrant.m_offset = startingMinislot;
  Grant cGrant;
  cGrant.m_sfid = CLASSIC_SFID;
  cGrant.m_length = cMinislots;
  cGrant.m_offset = startingMinislot + lMinislots;
  if (cMinislots > 0)
    {
      grantList.insert (std::pair<uint32_t, Grant> (cGrant.m_offset, cGrant));
      NS_LOG_DEBUG ("Scheduled " << cMinislots << " slots for SFID 1 at " << cGrant.m_offset);
    }
  if (lMinislots > 0)
    {
      grantList.insert (std::pair<uint32_t, Grant> (lGrant.m_offset, lGrant));
      NS_LOG_DEBUG ("Scheduled " << lMinislots << " slots for SFID 2 at " << lGrant.m_offset);
    }
  NS_LOG_DEBUG ("Returning with grant list of length " << grantList.size ());
  return;
}

std::vector<uint32_t>
CmtsUpstreamScheduler::SpreadGrantAcrossFrames (uint32_t grantedSlots)
{
  NS_LOG_FUNCTION (this << grantedSlots);
  std::vector<uint32_t> grantAllocationPerFrame;
  uint32_t framesPerMap = GetUpstream ()->GetFramesPerMap ();
  for (uint32_t i = 0; i < framesPerMap; i++)
    {
      grantAllocationPerFrame.push_back (grantedSlots / framesPerMap);
    }
  uint32_t slots = grantedSlots % framesPerMap;
  // distribute slots equally among frames, starting on slot 0
  if (slots)
    {
      double interval = static_cast<double> (framesPerMap) / slots;
      for (double j = 0; j < framesPerMap && slots > 0; )
        {
          uint32_t frame = std::floor (j);
          grantAllocationPerFrame[frame] += 1;
          j += interval;
          slots--;
        }
    }
  return grantAllocationPerFrame;
}

void
CmtsUpstreamScheduler::SpreadAndScheduleGrant (std::map<uint32_t, Grant>& grantList, uint32_t cMinislots, uint32_t lMinislots)
{
  NS_LOG_FUNCTION (this << cMinislots << lMinislots);
  uint32_t cMinislotsRequested = cMinislots;
  uint32_t lMinislotsRequested = lMinislots;
  uint32_t cMinislotsAllocated = 0;
  uint32_t lMinislotsAllocated = 0;
  uint32_t framesPerMap = GetUpstream ()->GetFramesPerMap ();
  uint32_t minislotsPerFrame = GetUpstream ()->GetMinislotsPerFrame ();
  std::vector<uint32_t> grantAllocationPerFrame = SpreadGrantAcrossFrames (lMinislots);

  for (uint32_t j = 0; j < framesPerMap; j++)
    {
      Grant lGrant;
      lGrant.m_sfid = LOW_LATENCY_SFID;
      lGrant.m_length = 0;
      // Allocate in the first minislot of the frame
      lGrant.m_offset = j * minislotsPerFrame;
      Grant cGrant;
      cGrant.m_sfid = CLASSIC_SFID;
      if (grantAllocationPerFrame[j])
        {
          lGrant.m_length = grantAllocationPerFrame[j];
          NS_LOG_DEBUG ("Allocating frame " << j << " L slots " << lGrant.m_length << " offset " << lGrant.m_offset);
          grantList.insert (std::pair<uint32_t, Grant> (lGrant.m_offset, lGrant));
          lMinislotsAllocated += lGrant.m_length;
        }
      if (cMinislots && lGrant.m_length < minislotsPerFrame)
        {
          // Fill the rest of the frame with grants for SF 1
          cGrant.m_length = minislotsPerFrame - lGrant.m_length;
          if (cGrant.m_length > cMinislots)
            {
              cGrant.m_length = cMinislots;
            }
          cGrant.m_offset = lGrant.m_offset + lGrant.m_length;
          NS_LOG_DEBUG ("Allocating frame " << j << " C slots " << cGrant.m_length << " offset " << cGrant.m_offset);
          grantList.insert (std::pair<uint32_t, Grant> (cGrant.m_offset, cGrant));
          cMinislots -= cGrant.m_length;
          cMinislotsAllocated += cGrant.m_length;
        }
    }
  NS_LOG_DEBUG ("Returning with grant list of length " << grantList.size ());
  NS_ABORT_MSG_UNLESS (cMinislotsRequested == cMinislotsAllocated, "Grant spreading error");
  NS_ABORT_MSG_UNLESS (lMinislotsRequested == lMinislotsAllocated, "Grant spreading error");
  return;
}

int64_t
CmtsUpstreamScheduler::AssignStreams (int64_t stream)
{
  m_schedulerVariable->SetStream (stream++);
  m_congestionVariable->SetStream (stream);
  return 2;
}

} // namespace docsis
} // namespace ns3
