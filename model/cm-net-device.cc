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
 *   Joey Padden <j.padden@cablelabs.com>
 *   Takashi Hayakawa <t.hayakawa@cablelabs.com>
 */

#include <algorithm>
#include <limits>
#include "ns3/log.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/queue.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/packet.h"
#include "ns3/address.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/error-model.h"
#include "ns3/nstime.h"
#include "ns3/queue-disc.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/queue-item.h"
#include "ns3/random-variable-stream.h"
#include "ns3/mac48-address.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#ifdef HAVE_PACKET_H
#include "ns3/fd-net-device.h"
#endif
#include "ns3/net-device-queue-interface.h"
#include "docsis-configuration.h"
#include "cm-net-device.h"
#include "cmts-upstream-scheduler.h"
#include "docsis-channel.h"
#include "docsis-header.h"
#include "docsis-queue-disc-item.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CmNetDevice");

namespace docsis {

NS_OBJECT_ENSURE_REGISTERED (CmNetDevice);

// Constants for improved code readability
static bool MAC_FRAME_BYTES = true;
static bool DATA_PDU_BYTES = false;

uint32_t
CmPipeline::GetEligible (Time deadline) const
{
  uint32_t total = 0;
  for (auto item : m_pipeline)
    {
      if (item.second <= deadline)
        {
          total += item.first;
        }
    }
  return total;
}

uint32_t
CmPipeline::GetEligible (void) const
{
  return GetEligible (Simulator::Now ());
}

uint32_t
CmPipeline::GetTotal (void) const
{
  uint32_t total = 0;
  for (auto item : m_pipeline)
    {
      total += item.first;
    }
  return total;
}

uint32_t
CmPipeline::GetSize (void) const
{
  return m_pipeline.size ();
}

void
CmPipeline::Add (uint32_t value, Time eligible)
{
  NS_LOG_FUNCTION (this << value << eligible);
  m_pipeline.push_back ( {value, eligible} );
}

std::pair<uint32_t, Time>
CmPipeline::Peek (void) const
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG (!m_pipeline.empty (), "Trying to peek from an empty pipeline");
  auto it = m_pipeline.front ();
  return m_pipeline.front ();
}

std::pair<uint32_t, Time>
CmPipeline::Pop (void)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG (!m_pipeline.empty (), "Trying to pop from an empty pipeline");
  auto it = m_pipeline.front ();
  m_pipeline.pop_front ();
  return it;
}

void
CmPipeline::Remove (uint32_t value)
{
  NS_LOG_FUNCTION (this << value);
  uint32_t remaining = value;
  while (!m_pipeline.empty () && remaining)
    {
      std::pair<uint32_t, Time> item = m_pipeline.front ();
      if (item.first <= remaining)    
        {
          remaining -= item.first;
          NS_LOG_DEBUG ("Removing " << item.first);
          m_pipeline.pop_front ();
        }
      else
        {
          NS_LOG_DEBUG ("Trimming " << remaining << " from " << m_pipeline[0].first);
          m_pipeline[0].first -= remaining;
          remaining = 0;
        }
    }
  NS_ASSERT_MSG (remaining == 0, "Accounting error");
}

void
CmPipeline::Clear (void)
{
  NS_LOG_FUNCTION (this);
  m_pipeline.clear ();
}


TypeId 
CmNetDevice::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::CmNetDevice")
    .SetParent<DocsisNetDevice> ()
    .SetGroupName ("Docsis")
    .AddConstructor<CmNetDevice> ()
    .AddAttribute ("RequestTimeVariable", "A RandomVariableStream used to pick the request time offset from start of MAP",
                   StringValue ("ns3::UniformRandomVariable"),
                   MakePointerAccessor (&CmNetDevice::m_requestTimeVariable),
                   MakePointerChecker<RandomVariableStream> ())
    .AddAttribute ("BurstPreparation", "Burst preparation time",
                   TimeValue (MicroSeconds (135)),
                   MakeTimeAccessor (&CmNetDevice::m_burstPreparationTime), 
                   MakeTimeChecker ()) 
    .AddAttribute ("PointToPointMode", "Point to point mode, for testing",
                   BooleanValue (false),
                   MakeBooleanAccessor (&CmNetDevice::m_pointToPointMode),
                   MakeBooleanChecker ()) 
   .AddTraceSource ("ClassicGrantState",
                    "Classic SFID state at end of MAP interval",
                    MakeTraceSourceAccessor (&CmNetDevice::m_cGrantStateTrace),
                    "ns3::CmNetDevice::CGrantStateTracedCallback")
   .AddTraceSource ("LowLatencyGrantState",
                    "Low Latency SFID state at end of MAP interval",
                    MakeTraceSourceAccessor (&CmNetDevice::m_lGrantStateTrace),
                    "ns3::CmNetDevice::LGrantStateTracedCallback")
   .AddTraceSource ("ClassicGrantReceived",
                    "Called when bytes are granted to Classic SF by MAP message arrival",
                    MakeTraceSourceAccessor (&CmNetDevice::m_traceCGrantReceived),
                    "ns3::CmNetDevice::UintegerTracedCallback")
   .AddTraceSource ("LowLatencyGrantReceived",
                    "Called when bytes are granted to LL SF by MAP message arrival",
                    MakeTraceSourceAccessor (&CmNetDevice::m_traceLGrantReceived),
                    "ns3::CmNetDevice::UintegerTracedCallback")
    .AddTraceSource ("ClassicSojournTime",
                     "Sojourn time of the last packet dequeued from the Classic queue",
                     MakeTraceSourceAccessor (&CmNetDevice::m_traceClassicSojourn),
                     "ns3::Time::TracedCallback")
    .AddTraceSource ("LowLatencySojournTime",
                     "Sojourn time of the last packet dequeued from the LL queue",
                     MakeTraceSourceAccessor (&CmNetDevice::m_traceLlSojourn),
                     "ns3::Time::TracedCallback")
   .AddTraceSource ("ClassicRequest",
                     "Called when a classic request is made",
                     MakeTraceSourceAccessor (&CmNetDevice::m_traceCRequest),
                     "ns3::CmNetDevice::UintegerTracedCallback")
   .AddTraceSource ("LowLatencyRequest",
                     "Called when a low latency request is made",
                     MakeTraceSourceAccessor (&CmNetDevice::m_traceLRequest),
                     "ns3::CmNetDevice::UintegerTracedCallback")
  ;
  return tid;
}

CmNetDevice::CmNetDevice () 
{
  NS_LOG_FUNCTION (this);
  m_lQueue = CreateObject<DropTailQueue<QueueDiscItem> > ();
  m_cQueue = CreateObject<DropTailQueue<QueueDiscItem> > ();
}

CmNetDevice::~CmNetDevice ()
{
  NS_LOG_FUNCTION (this);
  for (uint32_t i = 0; i < m_cQueue->GetNPackets (); i++)
    {
      m_cQueue->Dequeue ();
    }
  for (uint32_t i = 0; i < m_lQueue->GetNPackets (); i++)
    {
      m_lQueue->Dequeue ();
    }
  m_cTransmitPipeline.Clear ();
  m_lTransmitPipeline.Clear ();
  m_cGrantPipeline.Clear ();
  m_lGrantPipeline.Clear ();
  m_cRequestPipeline.Clear ();
  m_lRequestPipeline.Clear ();
}

void
CmNetDevice::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  if (GetQueue ())
    {
      GetQueue ()->Dispose ();
    }
  if (GetCmtsUpstreamScheduler ())
    {
      GetCmtsUpstreamScheduler ()->Dispose ();
    }
  if (m_asf)
    {
      m_asf->SetLowLatencyServiceFlow (0);
      m_asf->SetClassicServiceFlow (0);
    }
  m_asf = 0;
  m_sf = 0;
  m_classicSf = 0;
  m_llSf = 0;
  DocsisNetDevice::DoDispose ();
}

void
CmNetDevice::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
  DocsisNetDevice::DoInitialize ();

  m_grantBytes = 0;
  m_grantBytesNext = 0;

  NS_ABORT_MSG_UNLESS (m_asf || m_sf, "Service flows are not configured");
  Ptr<CmtsUpstreamScheduler> scheduler = GetCmtsUpstreamScheduler ();
  if (m_asf)
    {
      scheduler->ProvisionAggregateServiceFlow (m_asf);
      if (m_asf->GetNumServiceFlows () == 1)
        {
          GetQueue ()->SetAttribute ("Coupled", BooleanValue (false));
        }
    }
  else
    {
      scheduler->ProvisionSingleServiceFlow (m_sf);
      GetQueue ()->SetAttribute ("Coupled", BooleanValue (false));
    }
  InitializeTokenBucket ();

  m_cFragment.Clear ();
  m_lFragment.Clear ();

  Time latency = GetMinReqGntDelay () * GetFrameDuration () - GetCmMapProcTime ();
  NS_ABORT_MSG_UNLESS (latency > Seconds (0), "Negative latency " << latency.GetSeconds ());

  // A scheduler may allocate all of the minislots in a MAP interval and
  // provide a single grant.  Size the device queue to accommodate 
  // this possibility.
  uint32_t deviceQueueSize = 2 * GetFramesPerMap () * GetMinislotCapacity () * GetMinislotsPerFrame ();
  m_cQueue->SetAttribute ("MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, deviceQueueSize)));
  m_lQueue->SetAttribute ("MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, deviceQueueSize)));

  if (m_pointToPointMode == false)
    {
      uint32_t firstAllocStartTime = StartScheduler ();
      NS_LOG_DEBUG ("First Alloc Start Time: " << firstAllocStartTime);
      // Around startup time, the CM will need to wait for the CMTS scheduler
      // to deliver the first MAP message.  In the interim, allow the CM 
      // to generate contention-based requests, despite the lack of an 
      // actual MAP message arriving.
      uint32_t allocStartTime = 0;
      for (uint32_t i = 0; allocStartTime < firstAllocStartTime; i++)
        {
          Time requestOffset = i * GetActualMapInterval () + GetInitialRequestOffset ();
          if (m_classicSf)
            {
              NS_LOG_DEBUG ("Scheduling MakeRequest " << i << " for SFID " << m_classicSf->m_sfid << " in " << requestOffset.GetMicroSeconds () << " us");
              Simulator::Schedule (requestOffset, &CmNetDevice::MakeRequest, this, CLASSIC_SFID);
            }
          // This statement is executed regardless of whether there is a low
          // latency service flow, so that the requestOffsets used (random
          // variates) do not differ depending on the value of m_llSf 
          requestOffset = i * GetActualMapInterval () + GetInitialRequestOffset ();
          if (m_llSf)
            {
              NS_LOG_DEBUG ("Scheduling MakeRequest " << i << " for SFID " << m_llSf->m_sfid << " in " << requestOffset.GetMicroSeconds () << " us");
              Simulator::Schedule (requestOffset, &CmNetDevice::MakeRequest, this, LOW_LATENCY_SFID);
            }
          allocStartTime += TimeToMinislots (GetActualMapInterval ());
        }
    }

  NS_ABORT_MSG_UNLESS (GetQueue (), "Queue pointer not set");
  if (m_asf)
    {
      GetQueue ()->SetAsf (m_asf);
    }
  else
    {
      GetQueue ()->SetSf (m_sf);
    }
  // The service flow may contain overrides to default queue parameters
  // Set the queue's revised parameters before calling Initialize() on it
  if (m_classicSf)
    {
      // TLV processing
      if (m_classicSf->m_tosOverwrite != Ipv4Header::DscpDefault)
        {
          NS_LOG_DEBUG ("Setting TOS overwrite for classic flows");
          struct DscpOverwrite overwrite;
          overwrite.m_tosAndMask = 0x03;
          overwrite.m_tosOrMask = m_classicSf->m_tosOverwrite << 2; // DSCP type does not contain ECN bits
          GetQueue ()->SetClassicDscpOverwrite (overwrite);
        }
      if (m_classicSf->m_targetBuffer)
        {
          NS_LOG_DEBUG ("Setting classic Target Buffer to " << m_classicSf->m_targetBuffer << " bytes");
          GetQueue ()->SetAttribute ("ClassicBufferSize", QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, m_classicSf->m_targetBuffer)));
        }
      if (m_classicSf->m_aqmDisable)
        {
          NS_LOG_DEBUG ("Disabling classic AQM by setting target queue delay to 10 seconds");
          GetQueue ()->SetAttribute ("ClassicAqmLatencyTarget", TimeValue (Seconds (10)));
        }
      if (m_classicSf->m_classicAqmTarget)
        {
          Time newTarget = MilliSeconds (m_classicSf->m_classicAqmTarget);
          NS_LOG_DEBUG ("Setting DualQueue ClassicAqmLatencyTarget to " << newTarget.As (Time::MS));
          GetQueue ()->SetAttribute ("ClassicAqmLatencyTarget", TimeValue (newTarget));
        }
    }
  if (m_llSf)
    {
      // TLV processing
      if (m_llSf->m_tosOverwrite != Ipv4Header::DscpDefault)
        {
          NS_LOG_DEBUG ("Setting TOS overwrite for low latency flows");
          struct DscpOverwrite overwrite;
          overwrite.m_tosAndMask = 0x03;
          overwrite.m_tosOrMask = m_llSf->m_tosOverwrite << 2; // DSCP type does not contain ECN bits
          GetQueue ()->SetLowLatencyDscpOverwrite (overwrite);
        }
      if (m_llSf->m_targetBuffer)
        {
          NS_LOG_DEBUG ("Setting low latency Target Buffer to " << m_llSf->m_targetBuffer << " bytes");
          GetQueue ()->SetAttribute ("LowLatencyBufferSize", QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, m_llSf->m_targetBuffer)));
        }
      if (m_llSf->m_aqmDisable)
        {
          NS_LOG_DEBUG ("Disabling IAQM by setting MaxTh to 10 seconds and coupling to 0");
          GetQueue ()->SetAttribute ("MaxTh", TimeValue (Seconds (10)));
          GetQueue ()->SetAttribute ("CouplingFactor", DoubleValue (0));
        }
      if (m_llSf->m_iaqmMaxThresh)
        {
          // Note:  MaxTh may be adjusted based on FLOOR upon calling
          // GetQueue()->Initialize () below
          Time newMaxTh = MicroSeconds (m_llSf->m_iaqmMaxThresh);
          NS_LOG_DEBUG ("Setting DualQueue MaxTh to " << newMaxTh.As (Time::MS));
          GetQueue ()->SetAttribute ("MaxTh", TimeValue (newMaxTh));
        }
      if (m_llSf->m_iaqmRangeExponent)
        {
          NS_LOG_DEBUG ("Setting DualQueue LgRange to " << uint16_t (m_llSf->m_iaqmRangeExponent));
          GetQueue ()->SetAttribute ("LgRange", UintegerValue (m_llSf->m_iaqmRangeExponent));
        }
    }
  if (m_asf)
    {
      if (m_asf->m_coupled != AggregateServiceFlow::TriState::UNSPECIFIED)
        {
          NS_LOG_DEBUG ("Setting DualQueue Coupled to " << m_asf->m_coupled);
          GetQueue ()->SetAttribute ("Coupled", BooleanValue (m_asf->m_coupled));
        }
      if (m_asf->m_aqmCouplingFactor != 255)
        {
          NS_LOG_DEBUG ("Setting DualQueue CouplingFactor to " << static_cast<double> (m_asf->m_aqmCouplingFactor) / 10);
          GetQueue ()->SetAttribute ("CouplingFactor", DoubleValue (static_cast<double> (m_asf->m_aqmCouplingFactor) / 10));
        }
      if (m_asf->m_schedulingWeight)
        {
          NS_LOG_DEBUG ("Setting DualQueue SchedulingWeight to " << m_asf->m_schedulingWeight);
          GetQueue ()->SetAttribute ("SchedulingWeight", UintegerValue (m_asf->m_schedulingWeight));
        }
      if (m_asf->m_queueProtectionEnable != AggregateServiceFlow::TriState::UNSPECIFIED)
        {
          NS_LOG_DEBUG ("Setting QueueProtectionEnable to " << m_asf->m_queueProtectionEnable);
          GetQueue ()->GetQueueProtection ()->SetAttribute ("QProtectOn", BooleanValue (m_asf->m_queueProtectionEnable));
        }
      if (m_asf->m_qpLatencyThreshold != 65535)
        {
          NS_LOG_DEBUG ("Setting QpLatencyThreshold to " << m_asf->m_qpLatencyThreshold << "us");
          GetQueue ()->GetQueueProtection ()->SetAttribute ("ConfigureQpLatencyThreshold", BooleanValue (true));
          GetQueue ()->GetQueueProtection ()->SetAttribute ("QpLatencyThreshold", IntegerValue (m_asf->m_qpLatencyThreshold));
        }
      if (m_asf->m_qpQueuingScoreThreshold != 65535)
        {
          NS_LOG_DEBUG ("Setting QpQueuingScoreThreshold to " << m_asf->m_qpQueuingScoreThreshold << "us");
          GetQueue ()->GetQueueProtection ()->SetAttribute ("QpQueuingScoreThreshold", IntegerValue (m_asf->m_qpQueuingScoreThreshold));
        }
      if (m_asf->m_qpDrainRateExponent)
        {
          NS_LOG_DEBUG ("Setting QpDrainRateExponent to " << (uint16_t) m_asf->m_qpDrainRateExponent << "us");
          GetQueue ()->GetQueueProtection ()->SetAttribute ("QpDrainRateExponent", UintegerValue (m_asf->m_qpDrainRateExponent));
        }
    }

  // warn if user configured GGR to be greater than weight * AMSR
  if (m_llSf && m_llSf->m_guaranteedGrantRate.GetBitRate ())
    {
       uint64_t ggr = m_llSf->m_guaranteedGrantRate.GetBitRate ();
       if (ggr > (m_asf->m_maxSustainedRate.GetBitRate () * GetQueue ()->GetWeight ()))
         {
            std::cerr << "Warning:  configured GGR (" << m_llSf->m_guaranteedGrantRate.GetBitRate () << "bps) greater than (weight * AMSR)" << std::endl;
         }
    }

  GetQueue ()->Initialize ();

  // Queue accounting
  // Track all internal queue enqueue, dequeue, and drop events.
  // Track root queue disc DropAfterDequeue events; DropBeforeEnqueue are
  // not needed to be tracked.
  // Since packets are stored in the queue disc without Ethernet headers
  // (in order to access IPv4/v6 ECN and DSCP bits), we need to track
  // accurately each packet size and account for Ethernet padding and
  // eventual MAC headers.

  // Hook root QueueDisc callbacks for DropAfterDequeue events
  bool connected;
  connected = GetQueue ()->TraceConnectWithoutContext ("DropAfterDequeue", MakeCallback (&CmNetDevice::RootDropAfterDequeueCallback, this));
  NS_ABORT_MSG_UNLESS (connected, "Unable to hook trace source");
  connected = GetQueue ()->GetInternalQueue (0)->TraceConnectWithoutContext ("Enqueue", MakeCallback(&CmNetDevice::InternalClassicEnqueueCallback, this));
  NS_ABORT_MSG_UNLESS (connected, "Unable to hook trace source");
  connected = GetQueue ()->GetInternalQueue (0)->TraceConnectWithoutContext ("Dequeue", MakeCallback(&CmNetDevice::InternalClassicDequeueCallback, this));
  NS_ABORT_MSG_UNLESS (connected, "Unable to hook trace source");
  connected = GetQueue ()->GetInternalQueue (0)->TraceConnectWithoutContext ("Drop", MakeCallback(&CmNetDevice::InternalClassicDropCallback, this));
  NS_ABORT_MSG_UNLESS (connected, "Unable to hook trace source");
  if (GetQueue ()->GetNInternalQueues () == 2)
    {
      connected = GetQueue ()->GetInternalQueue (1)->TraceConnectWithoutContext ("Enqueue", MakeCallback(&CmNetDevice::InternalLlEnqueueCallback, this));
      NS_ABORT_MSG_UNLESS (connected, "Unable to hook trace source");
      connected = GetQueue ()->GetInternalQueue (1)->TraceConnectWithoutContext ("Dequeue", MakeCallback(&CmNetDevice::InternalLlDequeueCallback, this));
      NS_ABORT_MSG_UNLESS (connected, "Unable to hook trace source");
      connected = GetQueue ()->GetInternalQueue (1)->TraceConnectWithoutContext ("Drop", MakeCallback(&CmNetDevice::InternalLlDropCallback, this));
      NS_ABORT_MSG_UNLESS (connected, "Unable to hook trace source");
    }
}

void 
CmNetDevice::InternalClassicEnqueueCallback (Ptr<const QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_DEBUG ("Internal classic enqueue raw bytes: " << item->GetPacket ()->GetSize ()
   << " framed bytes: " << item->GetSize ());
  m_cAqmFramedBytes += item->GetSize ();
  m_cAqmPduBytes += GetDataPduSize (item->GetPacket ()->GetSize ());
}

void 
CmNetDevice::InternalClassicDequeueCallback (Ptr<const QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_DEBUG ("Internal classic dequeue raw bytes: " << item->GetPacket ()->GetSize ()
   << " framed bytes: " << item->GetSize ());
  NS_ABORT_MSG_IF (m_cAqmFramedBytes < item->GetSize (), "Accounting error");
  m_cAqmFramedBytes -= item->GetSize ();
  m_cAqmPduBytes -= GetDataPduSize (item->GetPacket ()->GetSize ());
}

void 
CmNetDevice::InternalClassicDropCallback (Ptr<const QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_DEBUG ("Internal classic drop raw bytes: " << item->GetPacket ()->GetSize ()
   << " framed bytes: " << item->GetSize ());
  // No need to adjust byte counter
}

void 
CmNetDevice::InternalLlEnqueueCallback (Ptr<const QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_DEBUG ("Internal LL enqueue raw bytes: " << item->GetPacket ()->GetSize ()
   << " framed bytes: " << item->GetSize ());
  m_lAqmFramedBytes += item->GetSize ();
  m_lAqmPduBytes += GetDataPduSize (item->GetPacket ()->GetSize ());
}

void 
CmNetDevice::InternalLlDequeueCallback (Ptr<const QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_DEBUG ("Internal LL dequeue raw bytes: " << item->GetPacket ()->GetSize ()
   << " framed bytes: " << item->GetSize ());
  NS_ABORT_MSG_IF (m_lAqmFramedBytes < item->GetSize (), "Accounting error");
  m_lAqmFramedBytes -= item->GetSize ();
  m_lAqmPduBytes -= GetDataPduSize (item->GetPacket ()->GetSize ());
}

void 
CmNetDevice::InternalLlDropCallback (Ptr<const QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_DEBUG ("Internal LL drop raw bytes: " << item->GetPacket ()->GetSize ()
   << " framed bytes: " << item->GetSize ());
  // No need to adjust byte counter
}

void 
CmNetDevice::RootDropAfterDequeueCallback (Ptr<const QueueDiscItem> item, const char* reason)
{
  NS_LOG_FUNCTION (this << item);
  NS_LOG_DEBUG ("Root drop after dequeue raw bytes: " << item->GetPacket ()->GetSize ()
   << " framed bytes: " << item->GetSize ());
  // No need to adjust byte counter
}

void
CmNetDevice::HandleMapMsg (MapMessage msg)
{
  NS_LOG_FUNCTION (this);
  Time requestOffset;
  if (msg.m_mapIeList.size () == 0) // No grant on which to piggyback request
    {
      // Schedule request to occur sometime in the MAP interval, notionally
      // within some kind of contention-based slot
      requestOffset = GetCmMapProcTime () + GetInitialRequestOffset ();
      if (m_classicSf)
        {
          NS_LOG_DEBUG ("Scheduling MakeRequest for SFID 1 in " << requestOffset.GetMicroSeconds () << " us");
          Simulator::Schedule (requestOffset, &CmNetDevice::MakeRequest, this, 1);
        }
      // If two SFIDs active, make a second independent request
      requestOffset = GetCmMapProcTime () + GetInitialRequestOffset ();
      if (m_llSf)
        {
          NS_LOG_DEBUG ("Scheduling MakeRequest for SFID 2 in " << requestOffset.GetMicroSeconds () << " us");
          Simulator::Schedule (requestOffset, &CmNetDevice::MakeRequest, this, 2);
        }
      return;
    }

  // Convert the MapMessage back into two std::queues of grants
  uint32_t grantInBytes = 0;
  uint32_t grantInBytes1 = 0;
  uint32_t grantInBytes2 = 0;
  std::vector<Grant> grantList1;
  std::vector<Grant> grantList2;
  for (auto it = msg.m_mapIeList.begin (); it != msg.m_mapIeList.end ();)
    {
      NS_LOG_DEBUG ("Process MAP IE");
      if (it->m_sfid == 0)
        {
          NS_LOG_DEBUG ("Skipping past MAP IE delimiter with SFID 0");
          ++it;
          continue;
        }
      struct Grant grant;
      grant.m_sfid = it->m_sfid;
      grant.m_offset = it->m_offset;
      // Determine length from next element offset
      ++it;
      grant.m_length = it->m_offset - grant.m_offset;
      grantInBytes += MinislotsToBytes (grant.m_length);
      if (grant.m_sfid == CLASSIC_SFID)
        {
          grantInBytes1 += MinislotsToBytes (grant.m_length);
          NS_LOG_DEBUG ("Unpacked grant; SFID: " << grant.m_sfid << " offset: " << grant.m_offset << " length bytes: " << MinislotsToBytes (grant.m_length));
          grantList1.push_back (grant);
        }
      else
        {
          grantInBytes2 += MinislotsToBytes (grant.m_length);
          NS_LOG_DEBUG ("Unpacked grant; SFID: " << grant.m_sfid << " start: " << grant.m_offset << " length bytes: " << MinislotsToBytes (grant.m_length));
          grantList2.push_back (grant);
        }
    }

  if (grantInBytes1)
    {
      m_traceCGrantReceived (grantInBytes1);
    }
  if (grantInBytes2)
    {
      m_traceLGrantReceived (grantInBytes2);
    }

  if (m_cRequestPipeline.GetTotal () > grantInBytes1)
    {
      m_cRequestPipeline.Remove (grantInBytes1);
    }
  else
    {
      m_cRequestPipeline.Clear ();
    }
  if (m_lRequestPipeline.GetTotal () > grantInBytes2)
    {
      m_lRequestPipeline.Remove (grantInBytes2);
    }
  else
    {
      m_lRequestPipeline.Clear ();
    }

  // For each grant, schedule PrepareBurst() and MakeRequest() one frame
  // earlier, then SendOut for each frame of the grant.  If no grant, then
  // schedule a contention-based request
  requestOffset = GetCmMapProcTime () + GetInitialRequestOffset ();
  if (!grantList1.empty ())
    {
      ScheduleGrantEvents (CLASSIC_SFID, grantList1);
    }
  else
    {
      NS_LOG_DEBUG ("Scheduling MakeRequest for SFID 1 in " << requestOffset.GetMicroSeconds () << " us");
      Simulator::Schedule (requestOffset, &CmNetDevice::MakeRequest, this, 1);
    }
  requestOffset = GetCmMapProcTime () + GetInitialRequestOffset ();
  if (!grantList2.empty ())
    {
      ScheduleGrantEvents (LOW_LATENCY_SFID, grantList2);
    }
  else if (m_llSf)
    {
      NS_LOG_DEBUG ("Scheduling MakeRequest for SFID 2 in " << requestOffset.GetMicroSeconds () << " us");
      Simulator::Schedule (requestOffset, &CmNetDevice::MakeRequest, this, 2);
    }
}

void
CmNetDevice::ScheduleGrantEvents (uint16_t sfid, std::vector<Grant> grantList)
{
  NS_LOG_FUNCTION (this << sfid << grantList.size ());
  MapState nextMapState;
  // grantAllocationPerFrame creates a vector of number of minislots per frame
  // allocated for this SFID.  If the grant spans frames, then the offset
  // is used to calculate how many minislots are allocated to each frame.
  std::vector<uint32_t> grantAllocationPerFrame;
  for (uint32_t j = 0; j < GetFramesPerMap (); j++)
    {
      grantAllocationPerFrame.push_back (0);
    }
  NS_LOG_DEBUG ("Frames per MAP: " << GetFramesPerMap ());
  for (auto it = grantList.begin (); it != grantList.end (); it++)
    {
      struct Grant grant = (*it);
      uint32_t startingFrame = GetFrameForMinislot (grant.m_offset);
      NS_ABORT_MSG_IF (startingFrame >= GetFramesPerMap (), "Grant offset overshoots MAP interval");
      NS_LOG_DEBUG ("Starting frame: " << startingFrame);
      Time grantTime = Now () + GetCmMapProcTime () + startingFrame * GetFrameDuration ();
      NS_LOG_DEBUG ("Scheduling a grant for SFID " << sfid << " of length " << grant.m_length << " slots at time " << grantTime.GetSeconds ());
      // We need to schedule an event to 'prepare the burst for transmission'
      // This occurs BurstPreparationTime in advance, and two things should
      // occur:
      //   1) piggyback grant request:  the dual queue should be checked for 
      //      any new data that requires a new grant request (to add these 
      //      bytes to the request pipeline)
      //   2) data preparation:  the device should dequeue enough data from the 
      //      dual queue to cover any data that will be sent in the next frame
      //
      Time burstPreparationTime = GetCmMapProcTime () + startingFrame * GetFrameDuration () - m_burstPreparationTime;
      NS_ABORT_MSG_IF (burstPreparationTime < Seconds (0), "Burst preparation time too large for this model");
      nextMapState.m_grantBytesInMap += grant.m_length * GetMinislotCapacity ();
      if (sfid == CLASSIC_SFID && grant.m_length)
        {
          m_cGrantPipeline.Add (grant.m_length * GetMinislotCapacity (), grantTime);
          NS_LOG_DEBUG ("Adding " << grant.m_length * GetMinislotCapacity () << " to cGrantPipeline at time " << grantTime.As (Time::S));
        }
      else if (sfid == LOW_LATENCY_SFID && grant.m_length)
        {
          m_lGrantPipeline.Add (grant.m_length * GetMinislotCapacity (), grantTime);
          NS_LOG_DEBUG ("Adding " << grant.m_length * GetMinislotCapacity () << " to lGrantPipeline at time " << grantTime.As (Time::S));
        }

      //
      // Next, schedule the MakeRequest at the burst preparation time 
      //
      NS_LOG_DEBUG ("Scheduling request deadline in " << burstPreparationTime.GetMicroSeconds () <<  "us, at " << 
                    (Simulator::Now () + burstPreparationTime).GetMicroSeconds ());
      Simulator::Schedule (burstPreparationTime, &CmNetDevice::MakeRequest, this, sfid);
      //
      // For a single grant, we schedule at least three events.  The first is at
      // the burst preparation time, the second (and possibly more, if the grant
      // spans frames) in the subsequent frames to send data, and the last
      // event to clean up any residual unused grant 
      //
      if (MinislotsRemainingInFrame (grant.m_offset) >= grant.m_length)
        {
          NS_LOG_DEBUG ("Grant fits entirely in one frame");
          Simulator::Schedule (burstPreparationTime, &CmNetDevice::PrepareBurst, this, sfid, grant.m_length);
          grantAllocationPerFrame [startingFrame] += grant.m_length;
        }
      else
        {
          // Grant spans multiple frames.  Make initial call of PrepareBurst()
          // and then mark grantAllocationPerFrame array for active frames
          uint32_t minislotsRemainingToSchedule = grant.m_length;
          NS_LOG_DEBUG ("Preparing burst at time " << (Simulator::Now () + 
                        burstPreparationTime).GetMicroSeconds ());
          NS_LOG_DEBUG ("Minislots remaining to schedule: " << grant.m_length);
          Simulator::Schedule (burstPreparationTime, &CmNetDevice::PrepareBurst,
                               this, sfid, minislotsRemainingToSchedule);
          uint32_t minislotsGrantedForFrame = MinislotsRemainingInFrame (grant.m_offset);
          NS_LOG_DEBUG ("Minislots granted for frame: " << minislotsGrantedForFrame);
          uint32_t nextFrameSlots = 0;
          uint32_t frameNumber = startingFrame;
          minislotsRemainingToSchedule -= minislotsGrantedForFrame;
          NS_LOG_DEBUG ("Minislots remaining to schedule: " << grant.m_length);
          // Plan to schedule the send events (grantAllocationPerFrame array)
          Time nextTime = grantTime;
          while (minislotsGrantedForFrame > 0 && minislotsRemainingToSchedule > 0)
            {
              nextFrameSlots = std::min<uint32_t> (minislotsRemainingToSchedule, GetMinislotsPerFrame ());
              grantAllocationPerFrame [frameNumber] += minislotsGrantedForFrame;
              NS_LOG_DEBUG ("Scheduling " << minislotsGrantedForFrame <<
                            " of grant for time " << nextTime.GetMicroSeconds () <<
                            " frame number " << frameNumber);
              minislotsGrantedForFrame = nextFrameSlots;
              minislotsRemainingToSchedule -= nextFrameSlots;
              frameNumber++;
              nextTime += GetFrameDuration ();
            }
          // Last frame is just a Send() event
          grantAllocationPerFrame [frameNumber] += minislotsGrantedForFrame;
          NS_LOG_DEBUG ("Last allocation " << minislotsGrantedForFrame <<
                        " of grant for time " << nextTime.GetMicroSeconds () <<
                        " frame number " << frameNumber);
        }
    }
  // Load the map state at the start of the next MAP interval
  Simulator::Schedule (GetCmMapProcTime (), &CmNetDevice::LoadMapState, this, sfid, nextMapState);
  // Schedule the accumulated send events
  Time sendTime = GetCmMapProcTime ();
  for (uint32_t j = 0; j < GetFramesPerMap (); j++)
    {
      if (grantAllocationPerFrame[j])
        {
          sendTime = GetCmMapProcTime () + j * GetFrameDuration ();
          Simulator::Schedule (sendTime, &CmNetDevice::SendFrame,
                               this, sfid, j, grantAllocationPerFrame[j]);
          NS_LOG_DEBUG ("Scheduling SendFrame of " << grantAllocationPerFrame[j]                        << " for time " << (Simulator::Now () + sendTime).GetMicroSeconds ());
        }
    }
  // One timestep after last send time above, dump any residual unused grant bytes
 NS_LOG_DEBUG ("Scheduling DumpGrant sfid " << sfid << " for time " <<
                        (Simulator::Now () + sendTime + TimeStep (1)).GetMicroSeconds ());
  Simulator::Schedule (sendTime + TimeStep (1),
                       &CmNetDevice::DumpGrant, this, sfid);
}

uint32_t
CmNetDevice::GetCUnsentBytes (bool includeMacHeaders) const
{
  return GetCAqmBytes (includeMacHeaders) + GetCDeviceBytes (includeMacHeaders);
}

uint32_t
CmNetDevice::GetCAqmBytes (bool includeMacHeaders) const
{
  if (includeMacHeaders)
    {
      // This assert is for consistency checking; in the future, we may
      // eliminate the internal tracking variable m_cAqmFramedBytes
      NS_ASSERT (m_cAqmFramedBytes == GetQueue ()->GetClassicQueueSize (includeMacHeaders));
      return m_cAqmFramedBytes;
    }
  else
    {
      NS_ASSERT (m_cAqmPduBytes == GetQueue ()->GetClassicQueueSize (includeMacHeaders));
      return m_cAqmPduBytes;
    }
}

uint32_t
CmNetDevice::GetCDeviceBytes (bool includeMacHeaders) const
{
  uint32_t returnValue;
  if (includeMacHeaders)
    { 
      returnValue = m_cQueueFramedBytes;
      if (m_cFragment.m_fragPkt)
        {
          returnValue += (m_cFragment.m_fragPkt->GetSize () + GetUsMacHdrSize () - m_cFragment.m_fragSentBytes);
        }
    }
  else
    { 
      returnValue = m_cQueuePduBytes;
      if (m_cFragment.m_fragPkt)
        {
          returnValue += (m_cFragment.m_fragPkt->GetSize () - m_cFragment.m_fragSentBytes);
        }
    }
  return returnValue;
}

uint32_t
CmNetDevice::GetLUnsentBytes (bool includeMacHeaders) const
{
  return GetLAqmBytes (includeMacHeaders) + GetLDeviceBytes (includeMacHeaders);
}

uint32_t
CmNetDevice::GetLAqmBytes (bool includeMacHeaders) const
{
  if (includeMacHeaders)
    {
      // This assert is for consistency checking; in the future, we may
      // eliminate the internal tracking variable m_lAqmFramedBytes
      NS_ASSERT (m_lAqmFramedBytes == GetQueue ()->GetLowLatencyQueueSize (includeMacHeaders));
      return m_lAqmFramedBytes;
    }
  else
    {
      NS_ASSERT (m_lAqmPduBytes == GetQueue ()->GetLowLatencyQueueSize (includeMacHeaders));
      return m_lAqmPduBytes;
    }
}

uint32_t
CmNetDevice::GetLDeviceBytes (bool includeMacHeaders) const
{
  uint32_t returnValue;
  if (includeMacHeaders)
    {
      returnValue = m_lQueueFramedBytes;
      if (m_lFragment.m_fragPkt)
        {
          returnValue += (m_lFragment.m_fragPkt->GetSize () + GetUsMacHdrSize () - m_lFragment.m_fragSentBytes);
        }
    }
  else
    {
      returnValue = m_lQueuePduBytes;
      if (m_lFragment.m_fragPkt)
        {
          returnValue += (m_lFragment.m_fragPkt->GetSize ()- m_lFragment.m_fragSentBytes);
        }
    }
  return returnValue;
}

bool
CmNetDevice::SendFrom (Ptr<Packet> packet,
                                 const Address &source,
                                 const Address &dest,
                                 uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (this << packet << source << dest << protocolNumber);
  NS_LOG_DEBUG ("Received packet " << packet->GetUid () << " Ethernet payload size: " << packet->GetSize () << " from " << source);

  RemarkTcpEcnValue (packet);
  //
  // If IsLinkUp() is false it means there is no channel to send any packet
  // over so we just hit the drop trace on the packet and return an error.
  //
  if (IsLinkUp () == false)
    {
      m_macTxDropTrace (packet);
      return false;
    }
  m_macTxTrace (packet);
  Ptr<DocsisQueueDiscItem> item = Create<DocsisQueueDiscItem> (packet, source, dest, protocolNumber, GetUsMacHdrSize ());
  NS_LOG_DEBUG ("Enqueuing frame (with MAC header) of size " << item->GetSize ());
  bool retval = GetQueue ()->Enqueue (item);
  if (retval)
    {
      NS_LOG_DEBUG ("AQM enqueue succeeded; total queue depth " << GetCAqmBytes (DATA_PDU_BYTES) + GetLAqmBytes (DATA_PDU_BYTES) << " bytes");
    }
  else
    {
      NS_LOG_DEBUG ("AQM enqueue failed; total queue depth " << GetCAqmBytes (DATA_PDU_BYTES) + GetLAqmBytes (DATA_PDU_BYTES) << " bytes");
      m_macTxDropTrace (packet);
    }
  if (m_pointToPointMode && m_pointToPointBusy == false)
    {
      SendImmediatelyIfAvailable ();
    }
  return retval;
}

bool
CmNetDevice::Send (
  Ptr<Packet> packet, 
  const Address &dest, 
  uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (this << packet << dest << protocolNumber);
  NS_FATAL_ERROR ("Not a valid operation for a bridged (L2) device");
}


// Return the number of bytes that are sent on the wire, which will be
// greater than packet->GetSize(), or return 0 if packet is not sent
uint32_t
CmNetDevice::SendOutFromCQueue (
  Ptr<Packet> packet, 
  const Address &src, 
  const Address &dest, 
  uint16_t protocolNumber,
  uint32_t eligibleBytes)
{
  NS_LOG_FUNCTION (this << packet << src << dest << protocolNumber << eligibleBytes);
  uint32_t result = 0;

  Ptr<Packet> sduPacket = packet->Copy (); // Service data unit
  m_cFragment.m_lastSduSize = packet->GetSize ();
  Mac48Address destination = Mac48Address::ConvertFrom (dest);
  Mac48Address source = Mac48Address::ConvertFrom (src);
  AddHeaderTrailer (packet, source, destination, protocolNumber);

  // since we defer addition of MAC header, account for it here
  uint32_t packetSizeOnWire = packet->GetSize () + GetUsMacHdrSize ();
  bool canSendFullPacket = false;

  if (packetSizeOnWire <= eligibleBytes)
    {
      canSendFullPacket = true;
    }
  Time transmissionTime = Seconds (0);
  Ptr<Packet> packetCopy = packet;

  // At this point, we can send if we have enough grant and space in frame
  if (canSendFullPacket || m_pointToPointMode)
    {
      NS_LOG_DEBUG ("Can send full packet at this transmission opportunity");
      if (m_pointToPointMode)
        {
          transmissionTime = GetDataRate ().CalculateBytesTxTime (packet->GetSize());
          NS_LOG_DEBUG ("Point to point mode (duration " << transmissionTime.As (Time::US) << ")");
        }
      else
        {
          transmissionTime = GetFrameDuration ();
          NS_LOG_DEBUG ("Fits in current grant - send it entirely (duration " << transmissionTime.As (Time::US) << ")");
        }
      Ptr<DocsisChannel> docsisChannel = DynamicCast<DocsisChannel> (GetChannel ());
      if (UseDocsisChannel ())
        {
          Ptr<Packet> packetWithDocsisHeader = packet->Copy ();
          AddDocsisHeader (packetWithDocsisHeader);
          m_phyTxBeginTrace (packetWithDocsisHeader);
          bool retval = docsisChannel->TransmitStart (packetWithDocsisHeader, this, transmissionTime);
          if (retval)
            {
              result = packetWithDocsisHeader->GetSize ();
              if (m_pointToPointMode)
                {
                  m_pointToPointBusy = true;
                  Simulator::Schedule (transmissionTime, &CmNetDevice::TransmitComplete, this);
                }
            }
        }
#ifdef HAVE_PACKET_H
      else
        {
          // Do not use DOCSIS MAC or Ethernet headers in emulation mode
          NS_ASSERT (GetFdNetDevice ());
          m_phyTxBeginTrace (sduPacket);
          bool retval = GetFdNetDevice ()->SendFrom (sduPacket, source, dest, protocolNumber);
          if (retval)
            {
              result = packetSizeOnWire;
            }
        }
#endif
      if (result > 0)
        {
          m_snifferTrace (packet);
          m_promiscSnifferTrace (packet);
        }
      else
        {
          NS_LOG_DEBUG ("Dropping due to TransmitStart failure");
          m_phyTxDropTrace (packet);
        }
    }
  else
    {
      // Send fragment
      m_cFragment.m_fragPkt = packet;
      m_cFragment.m_fragSdu = sduPacket; 
      m_cFragment.m_fragSrc = src;
      m_cFragment.m_fragDest = dest;
      m_cFragment.m_fragProtocolNumber = protocolNumber; 
      NS_LOG_DEBUG ("Send fragment of size " << eligibleBytes << " from packet size " << m_cFragment.m_fragPkt->GetSize () + GetUsMacHdrSize ());
      m_cFragment.m_fragSentBytes = eligibleBytes;
      result = m_cFragment.m_fragSentBytes;
    }
  return result;
}

void
CmNetDevice::SendImmediatelyIfAvailable (void)
{
  NS_LOG_FUNCTION (this);
  uint32_t cBytesOrig = GetCAqmBytes (MAC_FRAME_BYTES);
  uint32_t lBytesOrig = GetLAqmBytes (MAC_FRAME_BYTES);
  if (cBytesOrig + lBytesOrig == 0)
    {
      NS_LOG_DEBUG ("Returning due to empty AQM");
      return;
    }
  // Allow the AQM to pick which packet it is dequeueing
  Ptr<QueueDiscItem> queueDiscItem = GetQueue ()->Dequeue ();
  NS_ASSERT_MSG (queueDiscItem, "Should be an item in queue");
  Ptr<DocsisQueueDiscItem> item = DynamicCast<DocsisQueueDiscItem> (queueDiscItem);
  if (GetLAqmBytes (MAC_FRAME_BYTES) < lBytesOrig)
    {
      NS_LOG_DEBUG ("Dequeued from L queue");
      m_traceLlSojourn (Simulator::Now () - item->GetTimeStamp ());
      SendOutFromLQueue (item->GetPacket (), item->GetSource (), item->GetAddress (), item->GetProtocol (), item->GetSize ());
    }
  else if (GetCDeviceBytes (MAC_FRAME_BYTES) < cBytesOrig)
    {
      NS_LOG_DEBUG ("Dequeued from C queue");
      m_traceClassicSojourn (Simulator::Now () - item->GetTimeStamp ());
      SendOutFromCQueue (item->GetPacket (), item->GetSource (), item->GetAddress (), item->GetProtocol (), item->GetSize ());
    }
  else
    {
      NS_FATAL_ERROR ("Should be unreachable");
    }
}

void
CmNetDevice::TransmitComplete (void)
{
  NS_LOG_FUNCTION (this);
  if (m_pointToPointMode)
    {
      m_pointToPointBusy = false;
      SendImmediatelyIfAvailable ();
    }
}

// Return the number of bytes that are sent on the wire, which will be
// greater than packet->GetSize(), or return 0 if packet is not sent
uint32_t
CmNetDevice::SendOutFromLQueue (
  Ptr<Packet> packet, 
  const Address &src, 
  const Address &dest, 
  uint16_t protocolNumber,
  uint32_t eligibleBytes)
{
  NS_LOG_FUNCTION (this << packet << src << dest << protocolNumber);
  uint32_t result = 0;

  Ptr<Packet> sduPacket = packet->Copy (); // Service data unit
  m_lFragment.m_lastSduSize = packet->GetSize ();
  Mac48Address destination = Mac48Address::ConvertFrom (dest);
  Mac48Address source = Mac48Address::ConvertFrom (src);
  AddHeaderTrailer (packet, source, destination, protocolNumber);

  // since we defer addition of MAC header, account for it here
  uint32_t packetSizeOnWire = packet->GetSize () + GetUsMacHdrSize ();
  bool canSendFullPacket = false;

  if (packetSizeOnWire <= eligibleBytes)
    {
      canSendFullPacket = true;
    }
  Time transmissionTime = Seconds (0);
  Ptr<Packet> packetCopy = packet;

  // At this point, we can send if we have enough grant and space in frame
  if (canSendFullPacket || m_pointToPointMode)
    {
      NS_LOG_DEBUG ("Can send full packet at this transmission opportunity");
      if (m_pointToPointMode)
        {
          transmissionTime = GetDataRate ().CalculateBytesTxTime (packet->GetSize());
          NS_LOG_DEBUG ("Point to point mode (duration " << transmissionTime.As (Time::US) << ")");
        }
      else
        {
          transmissionTime = GetFrameDuration ();
          NS_LOG_DEBUG ("Fits in current grant - send it entirely (duration " << transmissionTime.As (Time::US) << ")");
        }
      Ptr<DocsisChannel> docsisChannel = DynamicCast<DocsisChannel> (GetChannel ());
      if (UseDocsisChannel ())
        {
          Ptr<Packet> packetWithDocsisHeader = packet->Copy ();
          AddDocsisHeader (packetWithDocsisHeader);
          m_phyTxBeginTrace (packetWithDocsisHeader);
          bool retval = docsisChannel->TransmitStart (packetWithDocsisHeader, this, transmissionTime);
          if (retval)
            {
              result = packetWithDocsisHeader->GetSize ();
              if (m_pointToPointMode)
                {
                  m_pointToPointBusy = true;
                  Simulator::Schedule (transmissionTime, &CmNetDevice::TransmitComplete, this);
                }
            }
        }
#ifdef HAVE_PACKET_H
      else
        {
          // Do not use DOCSIS MAC or Ethernet headers in emulation mode
          NS_ASSERT (GetFdNetDevice ());
          m_phyTxBeginTrace (sduPacket);
          bool retval = GetFdNetDevice ()->SendFrom (sduPacket, source, dest, protocolNumber);
          if (retval)
            {
              result = packetSizeOnWire;
            }
        }
#endif
      if (result > 0)
        {
          m_snifferTrace (packet);
          m_promiscSnifferTrace (packet);
        }
      else
        {
          NS_LOG_DEBUG ("Dropping due to TransmitStart failure");
          m_phyTxDropTrace (packet);
        }
    }
  else
    {
      // Send fragment
      m_lFragment.m_fragPkt = packet;
      m_lFragment.m_fragSdu = sduPacket; 
      m_lFragment.m_fragSrc = src;
      m_lFragment.m_fragDest = dest;
      m_lFragment.m_fragProtocolNumber = protocolNumber; 
      NS_LOG_DEBUG ("Send fragment of size " << eligibleBytes << " from packet size " << m_lFragment.m_fragPkt->GetSize () + GetUsMacHdrSize ());
      m_lFragment.m_fragSentBytes = eligibleBytes;
      result = m_lFragment.m_fragSentBytes;
    }
  return result;
}

Time
CmNetDevice::GetInitialRequestOffset (void)
{
  NS_LOG_FUNCTION (this);
  // Usually this is a uniform random variable, but in test suites, it may
  // be a deterministic variable for testing purposes, and may not support
  // the Min/Max attributes.  As a result, use the SetAttribute variants
  // that are allowed to silently fail if the attribute doesn't exist.
  m_requestTimeVariable->SetAttributeFailSafe ("Min", DoubleValue (0));
  m_requestTimeVariable->SetAttributeFailSafe ("Max", DoubleValue (GetActualMapInterval ().GetSeconds ()));
  // Align initial MakeRequest on a frame boundary
  double timeWithinMapInterval = m_requestTimeVariable->GetValue ();
  NS_ABORT_MSG_UNLESS (timeWithinMapInterval < GetActualMapInterval ().GetSeconds (), "Invalid request time");
  NS_LOG_DEBUG ("Value drawn from RNG: " << timeWithinMapInterval << " s");
  double frameNumber = floor (timeWithinMapInterval / GetFrameDuration ().GetSeconds ());
  Time requestOffset = frameNumber * GetFrameDuration ();
  NS_ABORT_MSG_UNLESS (requestOffset < GetActualMapInterval (), "Invalid request time");
  NS_LOG_DEBUG ("Request offset from start of MAP is " << requestOffset.GetMicroSeconds () << "us, in frame " << frameNumber);
  return requestOffset;
}

uint32_t
CmNetDevice::GetFrameForMinislot (uint32_t minislot)
{
  return (minislot / GetMinislotsPerFrame ()); 
}

uint32_t
CmNetDevice::GetFramingSize (void) const
{
  return (14 + 4);  // Ethernet header and trailer
}

uint32_t
CmNetDevice::MinislotRound (uint32_t bytes) const
{
  return static_cast<uint32_t> (std::round (static_cast<double> (bytes) / GetMinislotCapacity ()));
}

uint32_t
CmNetDevice::MinislotCeil (uint32_t bytes) const
{
  return static_cast<uint32_t> (std::ceil (static_cast<double> (bytes) / GetMinislotCapacity ()));
}

uint32_t
CmNetDevice::MinislotsToBytes (uint32_t minislots) const
{
  return minislots * GetMinislotCapacity ();
}

// Dequeue packets (as needed) from dual queue so that the grant that
// will be serviced burst preparation time later will be served by data already
// within this device.  This models a notional 'burst preparation' (coding,
// interleaving) in a real system and enforces that a packet that arrives
// to the dual queue after this time cannot be serviced in the burst
void
CmNetDevice::PrepareBurst (uint16_t sfid, uint32_t minislotsToPrepare)
{
  NS_LOG_FUNCTION (this << sfid << minislotsToPrepare);
  if (sfid == CLASSIC_SFID || (sfid == LOW_LATENCY_SFID && GetQueue ()->GetNInternalQueues () == 1))
    {
      PrepareBurstFromCQueue (minislotsToPrepare);
    }
  else if (sfid == LOW_LATENCY_SFID && GetQueue ()->GetNInternalQueues () == 2)
    {
      PrepareBurstFromLQueue (minislotsToPrepare);
    }
  else
    {
      NS_FATAL_ERROR ("Unsupported combination " << sfid << " " << GetQueue ()->GetNInternalQueues ());
    }
}

void
CmNetDevice::PrepareBurstFromCQueue (uint32_t minislotsToPrepare)
{
  NS_LOG_FUNCTION (this << minislotsToPrepare);
  uint32_t availableBytes = minislotsToPrepare * GetMinislotCapacity ();
  Time sendTime = Simulator::Now () + m_burstPreparationTime;
  //
  // Can send availableBytes at sendTime if that data is available now
  // All data now present in the C-queue will be in the AQM or the device
  // staging buffer; call that 'internal' bytes.  Some of that data will
  // already be scheduled to be sent in a prior burst; call that 'scheduled'.
  // The quantity unscheduledBytes = internal - scheduled
  // Two cases:  1) unscheduledBytes >= availableBytes (use all available)
  //             2) unscheduledBytes < available (use unscheduled)
  //
  uint32_t internalBytes = GetCDeviceBytes (MAC_FRAME_BYTES) + GetCAqmBytes (MAC_FRAME_BYTES);
  uint32_t scheduledBytes = m_cTransmitPipeline.GetTotal ();
  uint32_t unscheduledBytes = internalBytes - scheduledBytes;
  NS_ASSERT_MSG (internalBytes >= scheduledBytes, "Accounting error");
  NS_ASSERT_MSG (scheduledBytes + unscheduledBytes == internalBytes, "Accounting error");
  NS_LOG_DEBUG ("Available to send: " << availableBytes << " internal: " << internalBytes << " scheduled: " << scheduledBytes);

  // Pop the front of the grant pipeline, and check for alignment
  NS_ASSERT_MSG (m_cGrantPipeline.GetEligible (sendTime) > 0, "Unexpectedly empty pipeline");
  std::pair<uint32_t, Time> pipelineElement = m_cGrantPipeline.Pop ();
  NS_LOG_DEBUG ("Popped cGrantPipeline, size: " << pipelineElement.first << " time: " << pipelineElement.second.As (Time::S));
  NS_ASSERT_MSG (pipelineElement.first ==  minislotsToPrepare * GetMinislotCapacity (), "Error in pipeline bytes");
  NS_ASSERT_MSG (pipelineElement.second == sendTime, "Error in pipeline time");

  // Try to dequeue enough data to cover availableBytes
  while (availableBytes > (GetCDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes) && GetCAqmBytes (MAC_FRAME_BYTES) > 0)
  // Dequeue to staging queue
    {
      Ptr<QueueDiscItem> item;
      if (GetQueue ()->GetNInternalQueues () == 1)
        {
          item = GetQueue ()->Dequeue ();
        }
      else
        {
          item = GetQueue ()->ClassicDequeue ();
        }
      NS_ASSERT_MSG (item, "Unexpected failure to dequeue from low latency queue");
      Ptr<Packet> packet = item->GetPacket ();
      m_traceClassicSojourn (Simulator::Now () - item->GetTimeStamp ());
      if (m_cQueue->Enqueue (item))
        {
          NS_LOG_DEBUG ("Packet enqueued at time " << Simulator::Now ().GetMicroSeconds () << "us");
          m_cQueueFramedBytes += GetUsMacFrameSize (packet->GetSize ());
          m_cQueuePduBytes += GetDataPduSize (packet->GetSize ());
        }
      else
        {
          NS_FATAL_ERROR ("Packet not internally enqueued (enqueue failed-- perhaps m_cQueue not large enough?)");
        }
    }
  // If availableBytes is now <= (GetCDeviceBytes () - scheduledBytes)
  // we will be able to use all capacity when the grant arrives.
  // Otherwise (if C AQM empty), all bytes in pipeline must be in the device
  if (availableBytes <= (GetCDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes))
    {
      NS_LOG_DEBUG ("Able to use the full grant; adding " << availableBytes << " to C transmit pipeline");
      m_cTransmitPipeline.Add (availableBytes, Simulator::Now () + m_burstPreparationTime);
    }
  else if (GetCDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes > 0)
    {
      NS_LOG_DEBUG ("Not able to use " << availableBytes << "; adding " << (GetCDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes) << " to C transmit pipeline");
      m_cTransmitPipeline.Add (GetCDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes, Simulator::Now () + m_burstPreparationTime);
    }
  else
    {
      NS_LOG_DEBUG ("No bytes added to the C transmit pipeline");
    }
}

void
CmNetDevice::PrepareBurstFromLQueue (uint32_t minislotsToPrepare)
{
  NS_LOG_FUNCTION (this << minislotsToPrepare);
  uint32_t availableBytes = minislotsToPrepare * GetMinislotCapacity ();
  Time sendTime = Simulator::Now () + m_burstPreparationTime;
  //
  // Can send availableBytes at sendTime if that data is available now
  // All data now present in the L-queue will be in the AQM or the device
  // staging buffer; call that 'internal' bytes.  Some of that data will
  // already be scheduled to be sent in a prior burst; call that 'scheduled'. 
  // The quantity unscheduledBytes = internal - scheduled
  // Two cases:  1) unscheduledBytes >= availableBytes (use all available)
  //             2) unscheduledBytes < available (use unscheduled)
  //
  uint32_t internalBytes = GetLDeviceBytes (MAC_FRAME_BYTES) + GetLAqmBytes (MAC_FRAME_BYTES);
  uint32_t scheduledBytes = m_lTransmitPipeline.GetTotal ();
  uint32_t unscheduledBytes = internalBytes - scheduledBytes;
  NS_ASSERT_MSG (internalBytes >= scheduledBytes, "Accounting error");
  NS_ASSERT_MSG (scheduledBytes + unscheduledBytes == internalBytes, "Accounting error");
  NS_LOG_DEBUG ("Available to send: " << availableBytes << " internal: " << internalBytes << " scheduled: " << scheduledBytes);

  // Pop the front of the grant pipeline, and check for alignment
  NS_ASSERT_MSG (m_lGrantPipeline.GetEligible (sendTime) > 0, "Unexpectedly empty pipeline");
  std::pair<uint32_t, Time> pipelineElement = m_lGrantPipeline.Pop ();
  NS_LOG_DEBUG ("Popped lGrantPipeline, size: " << pipelineElement.first << " time: " << pipelineElement.second.As (Time::S));
  NS_ASSERT_MSG (pipelineElement.first ==  minislotsToPrepare * GetMinislotCapacity (), "Error in pipeline bytes");
  NS_ASSERT_MSG (pipelineElement.second == sendTime, "Error in pipeline time");

  // Try to dequeue enough data to cover availableBytes
  while (availableBytes > (GetLDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes) && GetLAqmBytes (MAC_FRAME_BYTES) > 0)
  // Dequeue to staging queue
    {
      Ptr<QueueDiscItem> item = GetQueue ()->LowLatencyDequeue ();
      NS_ASSERT_MSG (item, "Unexpected failure to dequeue from low latency queue");
      Ptr<Packet> packet = item->GetPacket ();
      m_traceLlSojourn (Simulator::Now () - item->GetTimeStamp ());
      if (m_lQueue->Enqueue (item))
        {
          NS_LOG_DEBUG ("Packet internally enqueued at time " << Simulator::Now ().GetMicroSeconds () << "us");
          m_lQueueFramedBytes += GetUsMacFrameSize (packet->GetSize ());
          m_lQueuePduBytes += GetDataPduSize (packet->GetSize ());
        }
      else
        {
          NS_FATAL_ERROR ("Packet not internally enqueued (enqueue failed-- perhaps m_lQueue not large enough?)");
        }
    }
  // If availableBytes is now <= (GetLDeviceBytes () - scheduledBytes),
  // we will be able to use all availableBytes when the grant arrives.
  // Otherwise (if L AQM is now empty), all bytes must now be in the device
  if (availableBytes <= (GetLDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes))
    {
      NS_LOG_DEBUG ("Able to use the full grant; adding " << availableBytes << " to L transmit pipeline");
      m_lTransmitPipeline.Add (availableBytes, Simulator::Now () + m_burstPreparationTime);
    }
  else if (GetLDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes > 0)
    {
      NS_LOG_DEBUG ("Not able to use " << availableBytes << "; adding " << (GetLDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes) << " to L transmit pipeline");
      m_lTransmitPipeline.Add (GetLDeviceBytes (MAC_FRAME_BYTES) - scheduledBytes, Simulator::Now () + m_burstPreparationTime);
    }
  else
    {
      NS_LOG_DEBUG ("No bytes added to the L transmit pipeline");
    }
}

uint32_t
CmNetDevice::SendOutFragmentFromCQueue (void)
{
  NS_LOG_FUNCTION (this);
  // At this point, m_fragPkt has Ethernet frame but no DOCSIS MAC header
  // For emulation, we have saved a copy (m_fragSdu) that has no Ethernet frame
  Ptr<Packet> packet = m_cFragment.m_fragPkt;
  Ptr<DocsisChannel> docsisChannel = DynamicCast<DocsisChannel> (GetChannel ());
  bool result = false;
  uint32_t bytesTransmitted = 0;
  if (UseDocsisChannel ())
    {
      Ptr<Packet> packetWithDocsisHeader = packet->Copy ();
      AddDocsisHeader (packetWithDocsisHeader);
      m_phyTxBeginTrace (packetWithDocsisHeader);
      Time transmissionTime = GetFrameDuration ();
      result = docsisChannel->TransmitStart (packetWithDocsisHeader, this, transmissionTime);
    }
#ifdef HAVE_PACKET_H
  else
    {
      // Do not use DOCSIS MAC or Ethernet headers in emulation mode
      NS_ASSERT (GetFdNetDevice ());
      m_phyTxBeginTrace (m_cFragment.m_fragSdu);
      result = GetFdNetDevice ()->SendFrom (m_cFragment.m_fragSdu, m_cFragment.m_fragSrc, m_cFragment.m_fragDest, m_cFragment.m_fragProtocolNumber);
    }
#endif
  if (result == true)
    {
      // Leave off DOCSIS MAC header, which tcpdump cannot parse
      m_snifferTrace (packet);
      m_promiscSnifferTrace (packet);
      bytesTransmitted = m_cFragment.m_fragPkt->GetSize () + GetUsMacHdrSize () - m_cFragment.m_fragSentBytes;
      NS_LOG_DEBUG ("Finished C-queue fragment with " << bytesTransmitted << " bytes transmitted");
    }
  else
    {
      NS_LOG_DEBUG ("Dropping due to TransmitStart failure");
      m_phyTxDropTrace (packet);
    }
  m_cFragment.Clear ();
  return bytesTransmitted;
}

uint32_t
CmNetDevice::SendOutFragmentFromLQueue (void)
{
  NS_LOG_FUNCTION (this);
  // At this point, m_fragPkt has Ethernet frame but no DOCSIS MAC header
  // For emulation, we have saved a copy (m_fragSdu) that has no Ethernet frame
  Ptr<Packet> packet = m_lFragment.m_fragPkt;
  Ptr<DocsisChannel> docsisChannel = DynamicCast<DocsisChannel> (GetChannel ());
  bool result = false;
  uint32_t bytesTransmitted = 0;
  if (UseDocsisChannel ())
    {
      Ptr<Packet> packetWithDocsisHeader = packet->Copy ();
      AddDocsisHeader (packetWithDocsisHeader);
      m_phyTxBeginTrace (packetWithDocsisHeader);
      Time transmissionTime = GetFrameDuration ();
      result = docsisChannel->TransmitStart (packetWithDocsisHeader, this, transmissionTime);
    }
#ifdef HAVE_PACKET_H
  else
    {
      // Do not use DOCSIS MAC or Ethernet headers in emulation mode
      NS_ASSERT (GetFdNetDevice ());
      m_phyTxBeginTrace (m_lFragment.m_fragSdu);
      result = GetFdNetDevice ()->SendFrom (m_lFragment.m_fragSdu, m_lFragment.m_fragSrc, m_lFragment.m_fragDest, m_lFragment.m_fragProtocolNumber);
    }
#endif
  if (result == true)
    {
      // Leave off DOCSIS MAC header, which tcpdump cannot parse
      m_snifferTrace (packet);
      m_promiscSnifferTrace (packet);
      bytesTransmitted = m_lFragment.m_fragPkt->GetSize () + GetUsMacHdrSize () - m_lFragment.m_fragSentBytes;
      NS_LOG_DEBUG ("Finished L queue fragment with " << bytesTransmitted << " bytes transmitted");
    }
  else
    {
      NS_LOG_DEBUG ("Dropping due to TransmitStart failure");
      m_phyTxDropTrace (packet);
    }
  m_lFragment.Clear ();
  return bytesTransmitted;
}

void
CmNetDevice::SendFrame (uint16_t sfid, uint32_t frameNumber, uint32_t minislotsToSend)
{
  NS_LOG_FUNCTION (this << sfid << frameNumber << minislotsToSend);
  if (sfid == CLASSIC_SFID || (sfid == LOW_LATENCY_SFID && GetQueue ()->GetNInternalQueues () == 1))
    {
      SendFrameFromCQueue (frameNumber, minislotsToSend);
    }
  else if (sfid == LOW_LATENCY_SFID && GetQueue ()->GetNInternalQueues () == 2)
    {
      SendFrameFromLQueue (frameNumber, minislotsToSend);
    }
  else
    {
      NS_FATAL_ERROR ("Unsupported combination " << sfid << " " << GetQueue ()->GetNInternalQueues ());
    }
}

void
CmNetDevice::SendFrameFromCQueue (uint32_t frameNumber, uint32_t minislotsToSend)
{
  NS_LOG_FUNCTION (this << frameNumber << minislotsToSend);
  NS_LOG_DEBUG ("Possibly sending frame " << frameNumber << " at time " << Simulator::Now ().As (Time::US));
  uint32_t bytesToSend = minislotsToSend * GetMinislotCapacity ();
  uint32_t bytesAllocated = bytesToSend;
  uint32_t bytesUsed = 0;
  NS_LOG_DEBUG ("C grant bytes in frame: " << bytesToSend << " transmit pipeline eligible: " << m_cTransmitPipeline.GetEligible ());
  bytesToSend = std::min<uint32_t> (bytesToSend, m_cTransmitPipeline.GetEligible ());
  while (m_cFragment.m_fragPkt)
    {
      // m_fragPkt points to a Packet with Ethernet header
      // The DOCSIS MAC header for it will be added in SendOutFragment ()
      uint32_t fragPktSize = m_cFragment.m_fragPkt->GetSize () + GetUsMacHdrSize ();
      NS_ASSERT_MSG (m_cFragment.m_fragSentBytes <= fragPktSize, "Error:  fragSentBytes exceeds size");
      uint32_t fragBytesRemaining = fragPktSize - m_cFragment.m_fragSentBytes;
      NS_LOG_DEBUG ("Fragment exists; size " << fragPktSize << " remaining " << fragBytesRemaining);
      if (bytesToSend >= fragBytesRemaining)
        {
          NS_LOG_DEBUG ("Can finish fragment within frame");
          bytesToSend -= fragBytesRemaining;
          bytesUsed += fragBytesRemaining;
          uint32_t wireBytesSent = SendOutFragmentFromCQueue ();
          NS_ASSERT_MSG (fragBytesRemaining == wireBytesSent, "Error: bytes sent mismatch");
        }
      else
        {
          NS_LOG_DEBUG ("Can not finish fragment within frame, sending "<< bytesToSend << " bytes");
          m_cFragment.m_fragSentBytes += bytesToSend;
          bytesUsed += bytesToSend;
          m_cMapState.m_unused += (bytesAllocated - bytesUsed);
          m_cTransmitPipeline.Remove (bytesUsed);
          return;
        }
    }
  NS_ABORT_MSG_UNLESS (m_cFragment.m_fragPkt == 0 && m_cFragment.m_fragSentBytes == 0, "Did not clear fragmentation pointer");
  if (bytesToSend == 0 || (m_cQueue->GetNPackets () == 0))
    {
      NS_LOG_DEBUG ("Return without starting a new frame; bytes to send: " << bytesToSend << " device queue packets: " << m_cQueue->GetNPackets ());
      m_cMapState.m_unused += (bytesAllocated - bytesUsed);
      m_cTransmitPipeline.Remove (bytesUsed);
      return;
    }
  NS_LOG_DEBUG ("Starting new packets with bytesToSend " << bytesToSend << " in frame " << frameNumber);
  while (bytesToSend > 0 && m_cQueue->GetNPackets ())
    {
      // Dequeue from device queue
      Ptr<DocsisQueueDiscItem> item = DynamicCast<DocsisQueueDiscItem> (m_cQueue->Dequeue ());
      NS_ABORT_MSG_UNLESS (item, "No item in queue");
      // save size of item because the call to SendOutFromCQueue will modify
      // the encapsulated packet by adding Ethernet framing
      uint32_t framedSize = item->GetSize ();
      m_cQueueFramedBytes -= framedSize;
      m_cQueuePduBytes -= GetDataPduSize (item->GetPacket ()->GetSize ());
      uint32_t sent = SendOutFromCQueue (item->GetPacket (), item->GetSource (), item->GetAddress (), item->GetProtocol (), bytesToSend);
      NS_LOG_DEBUG ("Sent bytes (including Ethernet and MAC headers) " << sent);
      NS_ASSERT_MSG (sent <= framedSize, "Error in transmission");
      if (sent < framedSize)
        {
          NS_LOG_DEBUG ("Sent fragment of " << sent << " from " << framedSize);
          bytesUsed += sent;
          break;
        }
      if (sent == framedSize)
        {
          bytesUsed += framedSize;
          bytesToSend -= framedSize;
        }
    }
  m_cMapState.m_unused += (bytesAllocated - bytesUsed);
  m_cTransmitPipeline.Remove (bytesUsed);
}

void
CmNetDevice::SendFrameFromLQueue (uint32_t frameNumber, uint32_t minislotsToSend)
{
  NS_LOG_FUNCTION (this << frameNumber << minislotsToSend);
  NS_LOG_DEBUG ("Possibly sending frame " << frameNumber << " at time " << Simulator::Now ().As (Time::US));
  uint32_t bytesToSend = minislotsToSend * GetMinislotCapacity ();
  uint32_t bytesAllocated = bytesToSend;
  uint32_t bytesUsed = 0;
  NS_LOG_DEBUG ("L grant bytes in frame: " << bytesToSend << " transmit pipeline eligible: " << m_lTransmitPipeline.GetEligible ());
  bytesToSend = std::min<uint32_t> (bytesToSend, m_lTransmitPipeline.GetEligible ());
  while (m_lFragment.m_fragPkt)
    {
      // m_fragPkt points to a Packet with Ethernet header
      // The DOCSIS MAC header for it will be added in SendOutFragment ()
      uint32_t fragPktSize = m_lFragment.m_fragPkt->GetSize () + GetUsMacHdrSize ();
      NS_ASSERT_MSG (m_lFragment.m_fragSentBytes <= fragPktSize, "Error:  fragSentBytes exceeds size");
      uint32_t fragBytesRemaining = fragPktSize - m_lFragment.m_fragSentBytes;
      NS_LOG_DEBUG ("Fragment exists; size " << fragPktSize << " remaining " << fragBytesRemaining);
      if (bytesToSend >= fragBytesRemaining)
        {
          NS_LOG_DEBUG ("Can finish fragment within frame");
          bytesToSend -= fragBytesRemaining;
          bytesUsed += fragBytesRemaining;
          uint32_t wireBytesSent = SendOutFragmentFromLQueue ();
          NS_ASSERT_MSG (fragBytesRemaining == wireBytesSent, "Error: bytes sent mismatch");
        }
      else
        {
          NS_LOG_DEBUG ("Can not finish fragment within frame, sending "<< bytesToSend << " bytes");
          m_lFragment.m_fragSentBytes += bytesToSend;
          bytesUsed += bytesToSend;
          m_lMapState.m_unused += (bytesAllocated - bytesUsed);
          m_lTransmitPipeline.Remove (bytesUsed);
          return;
        }
    }
  NS_ABORT_MSG_UNLESS (m_lFragment.m_fragPkt == 0 && m_lFragment.m_fragSentBytes == 0, "Did not clear fragmentation pointer");
  if (bytesToSend == 0 || (m_lQueue->GetNPackets () == 0))
    {
      NS_LOG_DEBUG ("Return without starting a new frame; bytes to send: " << bytesToSend << " device queue packets: " << m_lQueue->GetNPackets ());
      m_lMapState.m_unused += (bytesAllocated - bytesUsed);
      m_lTransmitPipeline.Remove (bytesUsed);
      return;
    }
  NS_LOG_DEBUG ("Starting new packets with bytesToSend " << bytesToSend << " in frame " << frameNumber);
  while (bytesToSend > 0 && m_lQueue->GetNPackets ())
    {
      // Dequeue from device queue
      Ptr<DocsisQueueDiscItem> item = DynamicCast<DocsisQueueDiscItem> (m_lQueue->Dequeue ());
      NS_ABORT_MSG_UNLESS (item, "No item in queue");
      // save size of item because the call to SendOutFromLQueue will modify
      // the encapsulated packet by adding Ethernet framing
      uint32_t framedSize = item->GetSize ();
      m_lQueueFramedBytes -= framedSize;
      m_lQueuePduBytes -= GetDataPduSize (item->GetPacket ()->GetSize ());
      uint32_t sent = SendOutFromLQueue (item->GetPacket (), item->GetSource (), item->GetAddress (), item->GetProtocol (), bytesToSend);
      NS_LOG_DEBUG ("Sent bytes (including Ethernet and MAC headers) " << sent);
      NS_ASSERT_MSG (sent <= framedSize, "Error in transmission");
      if (sent < framedSize)
        {
          NS_LOG_DEBUG ("Sent fragment of " << sent << " from " << framedSize);
          bytesUsed += sent;
          break;
        }
      if (sent == framedSize)
        {
          bytesUsed += framedSize;
          bytesToSend -= framedSize;
        }
    }
  m_lMapState.m_unused += (bytesAllocated - bytesUsed);
  m_lTransmitPipeline.Remove (bytesUsed);
}

// returns the number of completely unused minislots (i.e. not partial)
uint32_t 
CmNetDevice::MinislotsRemainingInFrame (uint32_t offset) const
{
  return (GetMinislotsPerFrame () - (offset % GetMinislotsPerFrame ()));
}

void
CmNetDevice::LoadMapState (uint16_t sfid, struct MapState mapState)
{
  NS_LOG_FUNCTION (this);
  if (sfid == CLASSIC_SFID)
    {
      m_cMapState = mapState;
      NS_LOG_DEBUG ("Loading m_cMapState.m_grant " << m_cMapState.m_grantBytesInMap << " bytes");
    }
  else if (sfid == LOW_LATENCY_SFID)
    {
      m_lMapState = mapState;
      NS_LOG_DEBUG ("Loading m_lMapState.m_grant " << m_lMapState.m_grantBytesInMap << " bytes");
    }
}

void
CmNetDevice::DumpGrant (uint16_t sfid)
{
  NS_LOG_FUNCTION (this << sfid);
  if (sfid == CLASSIC_SFID || (sfid == LOW_LATENCY_SFID && GetQueue ()->GetNInternalQueues () == 1))
    {
      CGrantState state;
      state.sfid = sfid;
      state.granted = m_cMapState.m_grantBytesInMap;
      state.used = m_cMapState.m_grantBytesInMap - m_cMapState.m_unused;
      state.unused = m_cMapState.m_unused;
      state.queued = GetCUnsentBytes (MAC_FRAME_BYTES);
      NS_LOG_DEBUG ("Used classic bytes " << state.used << " unused " << state.unused);
      state.delay = GetQueue ()->GetClassicQueuingDelay ();
      state.dropProb = GetQueue ()->GetClassicDropProbability ();
      m_cGrantStateTrace (state);
      m_cMapState.m_unused = 0;
      m_cMapState.m_grantBytesInMap = 0;
    }
  else if (sfid == LOW_LATENCY_SFID && GetQueue ()->GetNInternalQueues () == 2)
    {
      LGrantState state;
      state.sfid = sfid;
      state.granted = m_lMapState.m_grantBytesInMap;
      state.used = m_lMapState.m_grantBytesInMap - m_lMapState.m_unused;
      state.unused = m_lMapState.m_unused;
      state.queued = GetLUnsentBytes (MAC_FRAME_BYTES);
      NS_LOG_DEBUG ("Used LL bytes " << state.used << " unused " << state.unused);
      state.delay = GetQueue ()->GetLowLatencyQueuingDelay ();
      state.markProb = GetQueue ()->CalcProbNative ();
      state.coupledMarkProb = GetQueue ()->GetProbCL ();
      m_lGrantStateTrace (state);
      m_lMapState.m_unused = 0;
      m_lMapState.m_grantBytesInMap = 0;
    }
}

void
CmNetDevice::InitializeTokenBucket (void)
{
  NS_LOG_FUNCTION (this);
  uint32_t maxTrafficBurst = 0;
  if (m_classicSf)
    {
      maxTrafficBurst = m_classicSf->m_maxTrafficBurst;
    }
  m_lastUpdateTime = Simulator::Now ();
  m_peakTokens = 0;
  m_msrTokens = maxTrafficBurst;
}

void
CmNetDevice::UpdateTokenBucket (void)
{
  NS_LOG_FUNCTION (this);
  uint32_t maxTrafficBurst = 0;
  uint32_t tokenIncrement = 0;
  Time elapsed = Simulator::Now () - m_lastUpdateTime;
  if (m_classicSf)
    {
      maxTrafficBurst = m_classicSf->m_maxTrafficBurst;
      if (maxTrafficBurst == 0)
        {
          maxTrafficBurst = 3044;
        }
      tokenIncrement = static_cast<uint32_t> ((elapsed * m_classicSf->m_maxSustainedRate) / 8);
      NS_LOG_DEBUG ("Token increment: " << tokenIncrement << "B; elapsed: " << elapsed.GetMilliSeconds () << "ms");
    }
  if (m_msrTokens + tokenIncrement > m_msrTokens)
    {
      m_msrTokens += tokenIncrement;
    }
  if (m_msrTokens > maxTrafficBurst)
    {
      m_msrTokens = maxTrafficBurst;
    }
  if (m_classicSf)
    {
      m_peakTokens = static_cast<uint32_t> ((elapsed * m_classicSf->m_peakRate) / 8);
    }
  m_lastUpdateTime = Simulator::Now ();
}

uint32_t
CmNetDevice::ShapeRequest (uint32_t request)
{
  NS_LOG_FUNCTION (this << request);
  UpdateTokenBucket ();
  uint32_t shaped = std::min (request, m_msrTokens);
  if (m_classicSf->m_peakRate.GetBitRate () != 0)
    {
      shaped = std::min (shaped, m_peakTokens);
    }
  if (shaped < m_msrTokens)
    {
      m_msrTokens -= shaped;
    }
  else
    {
      m_msrTokens = 0;
    }
  NS_LOG_DEBUG ("Returning shaped bytes: " << shaped);
  return shaped;
}

/*
 * Generates request based on current queue length minus already requested
 * bytes and the next pending grant, capped by rate shaper tokens.
 */
void CmNetDevice::MakeRequest (uint16_t sfid)
{
  NS_LOG_FUNCTION (this << sfid);

  if (sfid == CLASSIC_SFID)
    {
      uint32_t queuedData = GetCDeviceBytes (MAC_FRAME_BYTES) + GetCAqmBytes (MAC_FRAME_BYTES);
      uint32_t grantPipeline = 0;
      if (m_cGrantPipeline.GetSize ())
        {
          grantPipeline = m_cGrantPipeline.Peek ().first;
          NS_ABORT_MSG_UNLESS (m_cGrantPipeline.Peek ().second >= Now (), "Error in classic grant pipeline");
        }
      NS_LOG_DEBUG ("cGrantPipeline next grant: " << grantPipeline);
      if (queuedData > (m_cRequestPipeline.GetTotal () + grantPipeline))
        {
          uint32_t newRequest = queuedData - (m_cRequestPipeline.GetTotal () + grantPipeline);
          NS_LOG_DEBUG ("new request size sfid 1: " << newRequest);
          // Request stream is filtered by token bucket state
          uint32_t shapedRequest = newRequest;
          if (m_classicSf->m_maxSustainedRate.GetBitRate () != 0)
            {
              shapedRequest = ShapeRequest (newRequest);
            }
          Time requestDelay = GetRtt ()/2 + GetFrameDuration () * (GetCmUsPipelineFactor () + GetCmtsUsPipelineFactor () + 1);
          GrantRequest grantRequest;
          grantRequest.m_sfid = CLASSIC_SFID;
          grantRequest.m_bytes = shapedRequest;
          grantRequest.m_requestTime = TimeToMinislots (Simulator::Now ());
          Simulator::Schedule (requestDelay,
                               &CmtsUpstreamScheduler::ReceiveRequest,
                               GetCmtsUpstreamScheduler (), grantRequest);
          NS_LOG_DEBUG ("Adding " << shapedRequest << " request bytes to C req. pipeline");
          m_cRequestPipeline.Add (shapedRequest, Now ());
          m_traceCRequest (shapedRequest);
        }
    }
  else if (sfid == LOW_LATENCY_SFID && GetQueue ()->GetNInternalQueues () == 2)
    {
      // Request any bytes in curq not already either in the request pipeline
      // or in the grant pipeline
      uint32_t queuedData = GetLDeviceBytes (MAC_FRAME_BYTES) + GetLAqmBytes (MAC_FRAME_BYTES);
      NS_LOG_DEBUG ("queued L Data " << queuedData);
      uint32_t grantPipeline = 0;
      if (m_lGrantPipeline.GetSize ())
        {
          grantPipeline = m_lGrantPipeline.Peek ().first;
          NS_ABORT_MSG_UNLESS (m_lGrantPipeline.Peek ().second >= Now (), "Error in LL grant pipeline");
        }
      NS_LOG_DEBUG ("lGrantPipeline next grant: " << grantPipeline);
      if (queuedData > (m_lRequestPipeline.GetTotal () + grantPipeline))
        {
          uint32_t newRequest = queuedData - (m_lRequestPipeline.GetTotal () + grantPipeline);
          NS_LOG_DEBUG ("new request size sfid 2: " << newRequest);
          Time requestDelay = GetRtt ()/2 + GetFrameDuration () * (GetCmUsPipelineFactor () + GetCmtsUsPipelineFactor () + 1);
          GrantRequest grantRequest;
          grantRequest.m_sfid = LOW_LATENCY_SFID;
          grantRequest.m_bytes = newRequest;
          grantRequest.m_requestTime = TimeToMinislots (Simulator::Now ());
          Simulator::Schedule (requestDelay,
                               &CmtsUpstreamScheduler::ReceiveRequest,
                               GetCmtsUpstreamScheduler (), grantRequest);
          m_lRequestPipeline.Add (newRequest, Now ());
          m_traceLRequest (newRequest);
        }
      else
        {
          NS_LOG_DEBUG ("Not enough queued data " << queuedData << " compared with pipeline " << (m_lRequestPipeline.GetTotal () + grantPipeline) << " for new request sfid 2");
        }
    }
  else
    {
      NS_FATAL_ERROR ("Unexpected sfid or configuration");
    }
  return;
}

// qDelaySingle() method, for single service flow and PIE queue
Time
CmNetDevice::ExpectedDelay (void) const
{
  NS_LOG_FUNCTION (this);
  if (!IsInitialized ())
    {
      return Seconds (0);
    }
  if (m_classicSf->m_maxSustainedRate.GetBitRate () == 0)
    {
      return Seconds (0);
    }
  uint32_t byteLength = GetCAqmBytes (DATA_PDU_BYTES) + GetCDeviceBytes (DATA_PDU_BYTES);
  double latency = 0;
  if (byteLength  <= m_msrTokens)
    {
      if (m_classicSf->m_peakRate.GetBitRate () == 0)
        {
          latency = 0;
        }
      else
        {
          latency = 8 * static_cast<double> (byteLength) / m_classicSf->m_peakRate.GetBitRate ();
        }
    }
  else
    {
      if (m_classicSf->m_peakRate.GetBitRate () == 0)
        {
          latency = 8 * static_cast<double> (byteLength - m_msrTokens) / m_classicSf->m_maxSustainedRate.GetBitRate ();
        }
      else
        {
          latency = 8 * static_cast<double> (byteLength - m_msrTokens) / m_classicSf->m_maxSustainedRate.GetBitRate ();
          latency += 8 * static_cast<double> (m_msrTokens) / m_classicSf->m_peakRate.GetBitRate ();
        }
    }
  NS_LOG_DEBUG ("queue size " << byteLength << " tokens " << m_msrTokens << " expected delay " << latency);
  return Seconds (latency);
}

void 
CmNetDevice::StartDecoding (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  Simulator::Schedule (GetCmDsPipelineFactor () * GetDsSymbolTime () + GetDsIntlvDelay (), &CmNetDevice::EndDecoding, this, packet);
}

void
CmNetDevice::RemoveReceivedDocsisHeader (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  DocsisHeader docsisHeader (GetDsMacHdrSize () - 6);
  packet->RemoveHeader (docsisHeader);
}

void
CmNetDevice::AddDocsisHeader (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  // Prepend a DocsisHeader; the header is currently a dummy 
  // header to account for bytes.  Pass in the value of how many 
  // extended header bytes to use
  DocsisHeader docsisHeader (GetUsMacHdrSize () - 6);
  packet->AddHeader (docsisHeader);
}

uint32_t
CmNetDevice::GetMacFrameSize (uint32_t sduSize) const
{
  return (GetDataPduSize (sduSize) + GetUsMacHdrSize ()); 
}

void
CmNetDevice::EndDecoding (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  m_phyRxEndTrace (packet);  // This trace is hit on all packets received
                             // from the channel.

  // Provide to pcap trace hooks, but first strip MAC header
  DocsisHeader hdr (GetDsMacHdrSize () - 6);
  packet->RemoveHeader (hdr);
  m_snifferTrace (packet);
  m_promiscSnifferTrace (packet);
  ForwardUp (packet);
}

void
CmNetDevice::SetUpstreamAsf (Ptr<AggregateServiceFlow> asf)
{
  NS_LOG_FUNCTION (this << asf);
  NS_ABORT_MSG_IF (IsInitialized (), "Must call before device is initialized");
  NS_ABORT_MSG_IF (m_sf, "Cannot set an ASF if a single SF was already set");
  if (m_asf)
    {
      NS_LOG_WARN ("Overwriting previously set ASF: " << m_asf);
    }
  m_asf = asf;
  m_classicSf = asf->GetClassicServiceFlow ();
  m_llSf = asf->GetLowLatencyServiceFlow ();
  // QoS parameters on the Low Latency Service Flow are not supported
  if (m_llSf)
    {
      NS_ABORT_MSG_UNLESS (m_llSf->m_maxSustainedRate.GetBitRate () == 0, "LL MSR unsupported");
      NS_ABORT_MSG_UNLESS (m_llSf->m_peakRate.GetBitRate () == 0, "LL PeakRate unsupported");
      NS_ABORT_MSG_UNLESS (m_llSf->m_maxTrafficBurst == 3044, "LL MaxTrafficBurst unsupported");
    }
}

void
CmNetDevice::SetUpstreamSf (Ptr<ServiceFlow> sf)
{
  NS_LOG_FUNCTION (this << sf);
  NS_ABORT_MSG_IF (IsInitialized (), "Must call before device is initialized");
  NS_ABORT_MSG_IF (m_asf, "Cannot set a single SF if an ASF was already set");
  if (m_sf)
    {
      NS_LOG_WARN ("Overwriting previously set SF: " << m_sf);
    }
  m_sf = sf;
  if (sf->m_sfid == CLASSIC_SFID)
    {
      m_classicSf = sf;
      m_llSf = 0;
    }
  else
    {
      m_llSf = sf;
      m_classicSf = 0;
      // QoS parameters on the Low Latency Service Flow are not supported
      NS_ABORT_MSG_UNLESS (sf->m_maxSustainedRate.GetBitRate () == 0, "LL MSR unsupported");
      NS_ABORT_MSG_UNLESS (sf->m_peakRate.GetBitRate () == 0, "LL PeakRate unsupported");
      NS_ABORT_MSG_UNLESS (sf->m_maxTrafficBurst == 3044, "LL MaxTrafficBurst unsupported");
    }
}

Ptr<const AggregateServiceFlow>
CmNetDevice::GetUpstreamAsf (void) const
{
  return m_asf;
}

Ptr<const ServiceFlow>
CmNetDevice::GetUpstreamSf (void) const
{
  return m_sf;
}

int64_t
CmNetDevice::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_requestTimeVariable->SetStream (stream++);
  return 1;
}

} // namespace docsis
} // namespace ns3
