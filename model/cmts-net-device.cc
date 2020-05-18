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
#include "ns3/simulator.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/address.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/error-model.h"
#include "ns3/data-rate.h"
#include "ns3/queue-disc.h"
#include "ns3/queue-item.h"
#include "ns3/queue.h"
#include "ns3/fd-net-device.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/net-device-queue-interface.h"
#include "cmts-net-device.h"
#include "dual-queue-coupled-aqm.h"
#include "docsis-channel.h"
#include "docsis-header.h"
#include "docsis-queue-disc-item.h"
#include "docsis-configuration.h"
#include "queue-protection.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CmtsNetDevice");

namespace docsis {

NS_OBJECT_ENSURE_REGISTERED (CmtsNetDevice);

TypeId 
CmtsNetDevice::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::CmtsNetDevice")
    .SetParent<DocsisNetDevice> ()
    .SetGroupName ("Docsis")
    .AddConstructor<CmtsNetDevice> ()
    .AddAttribute ("FreeCapacityMean",
                   "Average upstream free capacity (bits/sec)",
                   UintegerValue (0),
                   MakeUintegerAccessor (&CmtsNetDevice::m_freeCapacityMean),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("FreeCapacityVariation",
                   "Bound (percent) on the variation of upstream free capacity",
                   UintegerValue (0),
                   MakeUintegerAccessor (&CmtsNetDevice::m_freeCapacityVariation),
                   MakeUintegerChecker<uint32_t> (0,100))
    .AddAttribute ("MaxPdu", "Peak rate token bucket maximum size (bytes)",
                   UintegerValue (1532),
                   MakeUintegerAccessor (&CmtsNetDevice::m_maxPdu),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("PointToPointMode", "Point to point mode, for testing",
                   BooleanValue (false),
                   MakeBooleanAccessor (&CmtsNetDevice::m_pointToPointMode),
                   MakeBooleanChecker ())
   .AddTraceSource ("Tokens",
                     "Traced value of tokens",
                     MakeTraceSourceAccessor (&CmtsNetDevice::m_tokens),
                     "ns3::TracedValueCallback::Double")
   .AddTraceSource ("State",
                     "Internal state report generated upon each packet send",
                     MakeTraceSourceAccessor (&CmtsNetDevice::m_state),
                     "ns3::CmtsNetDevice::StateTracedCallback")
    .AddAttribute ("CongestionVariable", "A RandomVariableStream used to pick the level of simulated congestion",
                   StringValue ("ns3::UniformRandomVariable[Max=65535]"),
                   MakePointerAccessor (&CmtsNetDevice::m_congestionVariable),
                   MakePointerChecker<RandomVariableStream> ())
  ;
  return tid;
}


CmtsNetDevice::CmtsNetDevice () 
  : m_tokens (0),
    m_peakTokens (0),
    m_lastUpdateTime (Seconds (0)),
    m_req (0),    
    m_fragPkt (0),
    m_fragSdu (0),
    m_fragSentBytes (0),
    m_fragProtocolNumber (0),
    m_lastSduSize (0),
    m_scheduledBytes (0),
    m_symbolState (0),
    m_queueFramedBytes (0)
{
  NS_LOG_FUNCTION (this);
  m_deviceQueue = CreateObject<DropTailQueue<QueueDiscItem> > ();
}

CmtsNetDevice::~CmtsNetDevice ()
{
  NS_LOG_FUNCTION (this);
  delete [] m_req;
  for (uint32_t i = 0; i < m_deviceQueue->GetNPackets (); i++)
    {
      m_deviceQueue->Dequeue ();
    }
}

void
CmtsNetDevice::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  if (GetQueue ())
    {
      GetQueue ()->Dispose ();
    }
  DocsisNetDevice::DoDispose ();
}

void
CmtsNetDevice::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
  DocsisNetDevice::DoInitialize ();

  NS_ABORT_MSG_UNLESS (m_asf || m_sf, "Service flows are not configured");
  if (m_asf)
    {
      m_maxSustainedRate = m_asf->m_maxSustainedRate;
      m_peakRate = m_asf->m_peakRate;
      m_maxTrafficBurst = m_asf->m_maxTrafficBurst;
      if (m_asf->GetNumServiceFlows () == 1)
        {
          GetQueue ()->SetAttribute ("Coupled", BooleanValue (false));
        }
    }
  else
    {
      m_maxSustainedRate = m_sf->m_maxSustainedRate;
      m_peakRate = m_sf->m_peakRate;
      m_maxTrafficBurst = m_sf->m_maxTrafficBurst;
      GetQueue ()->SetAttribute ("Coupled", BooleanValue (false));
    }

  // Ensure that a few symbols or packets worth of data can be stored in the device queue
  uint32_t deviceQueueSize = 3 * GetDsSymbolCapacity ();
  deviceQueueSize = std::max<uint32_t> (deviceQueueSize, 3 * 1518);
  m_deviceQueue->SetAttribute ("MaxSize", QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, deviceQueueSize)));

  NS_LOG_DEBUG ("Symbol time " << GetDsSymbolTime ().As (Time::NS));
  if (m_pointToPointMode == false)
    {
      Simulator::Schedule (GetDsSymbolTime (), &CmtsNetDevice::HandleSymbolBoundary, this);
    }
  if (m_freeCapacityMean == 0)
    {
      // Special value of zero signifies that user has not configured to
      // a specific value, so change it to the peak rate
      m_freeCapacityMean = static_cast<uint32_t> (m_peakRate.GetBitRate ());
    }

  // Set up pipeline
  m_reqCount = GetCmtsDsPipelineFactor () + 1;
  m_req = new uint32_t[m_reqCount];
  for (uint32_t i = 0; i < m_reqCount; i++)
    {
      m_req[i] = 0;
    }
  m_tokens = m_maxTrafficBurst;
  m_peakTokens = m_maxPdu;
  m_lastUpdateTime = Simulator::Now ();

  NS_ABORT_MSG_UNLESS (GetQueue (), "Queue pointer not set");
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

  GetQueue ()->Initialize ();

  // warn if user configured GGR to be greater than weight * AMSR
  if (m_llSf && m_llSf->m_guaranteedGrantRate.GetBitRate ())
    {
       uint64_t ggr = m_llSf->m_guaranteedGrantRate.GetBitRate ();
       if (ggr > (m_maxSustainedRate.GetBitRate () * GetQueue ()->GetWeight ()))
         {
            std::cerr << "Warning:  configured GGR (" << m_llSf->m_guaranteedGrantRate.GetBitRate () << "bps) greater than (weight * AMSR)" << std::endl;
         }
    }
}

void
CmtsNetDevice::Reset ()
{
  NS_LOG_FUNCTION (this);
  // Reset pipeline
  for (uint32_t i = 0; i < m_reqCount; i++)
    { 
      m_req[i] = 0;
    } 
  m_tokens = 0;
  m_peakTokens = 0;
  m_lastUpdateTime = Simulator::Now ();
}

void
CmtsNetDevice::SetDownstreamAsf (Ptr<AggregateServiceFlow> asf)
{
  NS_LOG_FUNCTION (this << asf);
  NS_ABORT_MSG_IF (IsInitialized (), "Must call before device is initialized");
  NS_ABORT_MSG_IF (m_sf, "Cannot set an ASF if a single SF was already set");
  if (m_asf)
    {
      NS_LOG_DEBUG ("Overwriting previously set ASF: " << m_asf);
    }
  m_asf = asf;
  m_classicSf = asf->GetClassicServiceFlow ();
  m_llSf = asf->GetLowLatencyServiceFlow ();
}

void
CmtsNetDevice::SetDownstreamSf (Ptr<ServiceFlow> sf)
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
    }
}

Ptr<const AggregateServiceFlow>
CmtsNetDevice::GetDownstreamAsf (void) const
{
  return m_asf;
}

Ptr<const ServiceFlow>
CmtsNetDevice::GetDownstreamSf (void) const
{
  return m_sf;
}

int64_t
CmtsNetDevice::AssignStreams (int64_t stream)
{
  m_congestionVariable->SetStream (stream);
  return 1;
}

// GetPipelineData() is the number of bytes that are being prepared for
// future symbol transmissions
uint32_t
CmtsNetDevice::GetPipelineData () const
{
  uint32_t returnValue = 0;
  for (uint32_t i = 0; i < m_reqCount; i++)
    { 
      returnValue += m_req[i];
    } 
  return returnValue;
}

// The number of bytes that exist in the device include any unsent
// bytes from the fragmented packet (if any), and bytes that exist
// in the local FIFO queue m_deviceQueue.
//
// These quantities do not account for MAC header overheads, but do
// account for Ethernet header and padding overheads
uint32_t
CmtsNetDevice::GetInternallyQueuedBytes () const
{
  uint32_t returnValue = m_queueFramedBytes;
  if (m_fragPkt)
    {
      returnValue += (m_fragPkt->GetSize () + GetDsMacHdrSize () - m_fragSentBytes);
    }
  return returnValue;
}

uint32_t
CmtsNetDevice::GetEligiblePipelineData (void) const
{
  return m_req[0];
}

void
CmtsNetDevice::PushToPipeline (uint32_t newData)
{
  NS_LOG_FUNCTION (this << newData);
  m_req[0] += m_req[1];
  for (uint32_t i = 1; i < (m_reqCount - 1); i++)
    { 
      m_req[i] = m_req[i+1];
    } 
  m_req [m_reqCount - 1] = newData;
  NS_LOG_DEBUG ("Pipeline after push, slot 0: " << m_req[0] << " slot 1: " << m_req[1]);
}

void
CmtsNetDevice::PopFromPipeline (uint32_t oldData)
{
  NS_LOG_FUNCTION (this << oldData);
  NS_ASSERT_MSG (m_req[0] >= oldData, "m_req[0] " << m_req[0] << " oldData " << oldData);
  m_req[0] -= oldData;
  NS_LOG_DEBUG ("Pipeline after pop, slot 0: " << m_req[0] << " slot 1: " << m_req[1]);
}

uint32_t
CmtsNetDevice::GetFramingSize (void) const
{
  return (14 + 4);  // Ethernet header and trailer
}

void
CmtsNetDevice::SendSymbol (uint32_t scheduledBytes, uint32_t symbolState)
{
  NS_LOG_FUNCTION (this << scheduledBytes << symbolState);
  m_symbolState = symbolState;
  m_scheduledBytes = scheduledBytes;
  if (m_fragPkt)
    {
      NS_LOG_DEBUG ("Continue with fragment");
      
      uint32_t fragmentSize = m_fragPkt->GetSize () + GetDsMacHdrSize () - m_fragSentBytes;
      uint32_t sentFragmentBytes = SendFragment ();
      if (sentFragmentBytes < fragmentSize)
        {
          NS_LOG_DEBUG ("Did not complete fragment, sending " << sentFragmentBytes << " of " << fragmentSize);
          return;
        }
      else
        {
          NS_LOG_DEBUG ("Completed fragment with " << fragmentSize << " bytes");
        }
      NS_ASSERT_MSG (scheduledBytes >= fragmentSize, "Arithmetic error");
      m_scheduledBytes -= fragmentSize;
    }
  if (m_scheduledBytes == 0)
    {
      return;
    }
  NS_LOG_DEBUG ("Send from internal queue " << m_scheduledBytes);
  uint32_t sent = 0;
  while (m_scheduledBytes > 0)
    {
      Ptr<DocsisQueueDiscItem> item = DynamicCast<DocsisQueueDiscItem> (m_deviceQueue->Dequeue ());
      if (!item)
        {
          NS_LOG_DEBUG ("No item in queue; possibly a drop on dequeue");
          break;
        }
      // save size of item because the call to SendOut() will modify
      // the encapsulated packet by adding Ethernet framing
      uint32_t framedSize = item->GetSize ();
      m_queueFramedBytes -= framedSize;
      sent = SendOut (item->GetPacket (), item->GetSource (), item->GetAddress (), item->GetProtocol ());
      NS_LOG_DEBUG ("Sent bytes (including Ethernet headers) " << sent);
      if (sent == framedSize)
        {
          if (m_scheduledBytes >= framedSize)
            {
              m_scheduledBytes -= framedSize;
            }
          else 
            {
              m_scheduledBytes = 0;
            }
          NS_LOG_DEBUG ("Sent full size frame of " << framedSize);
          continue;
        }
      else if (sent < framedSize)
        {
          NS_LOG_DEBUG ("Sent fragment of " << sent << " from " << framedSize);
          if (m_scheduledBytes >= framedSize)
            {
              m_scheduledBytes -= framedSize;
            }
          else 
            {
              m_scheduledBytes = 0;
            }
          break;
        }
      else 
        {
          NS_FATAL_ERROR ("Unexpected value for sent " << sent << " scheduledBytes " << m_scheduledBytes);
        }
    }
  if (m_scheduledBytes != 0)
    {
      NS_LOG_DEBUG ("Scheduled bytes not all used " << m_scheduledBytes);
      PopFromPipeline (m_scheduledBytes);
    }
}

void
CmtsNetDevice::HandleSymbolBoundary (void)
{
  NS_LOG_FUNCTION (this);
  m_symbolState = 0;
  IncrementTokens (Simulator::Now () - m_lastUpdateTime);
  Simulator::Schedule (GetDsSymbolTime (), &CmtsNetDevice::HandleSymbolBoundary, this);
  if (m_tokens <= 0 || m_peakTokens <= 0)
    {
      NS_LOG_DEBUG ("Insufficient tokens to prepare data for future symbol");
      PushToPipeline (0);
      return;
    }
  // Determine if any additional data should be added to transmission pipeline
  uint32_t pendingData = GetQueue ()->GetCurrentSize ().GetValue () + GetInternallyQueuedBytes ();
  if (pendingData <= GetPipelineData ())
    {
      NS_LOG_DEBUG ("No new data to add to transmission pipeline");
      PushToPipeline (0);
      return;
    }
  // Existing data exceeds what we have in our pipeline
  uint32_t dataToSchedule = std::min<uint32_t> (GetEligibleTokens (), (pendingData - GetPipelineData ()));
  uint32_t symbolState = 0;
  if (dataToSchedule)
    {
      // Any notional congestion can be handled here by advancing m_symbolState
      // according to the number of bytes used by other flows
      symbolState = static_cast<uint32_t> (GetDsSymbolCapacity () - CalculateFreeCapacity ());
      NS_LOG_DEBUG ("Starting symbol state (byte): " << symbolState << " capacity: " << GetDsSymbolCapacity ());
      dataToSchedule = std::min<uint32_t> (dataToSchedule, GetDsSymbolCapacity () - symbolState);
    }
  NS_LOG_DEBUG ("PushToPipeline " << dataToSchedule);
  PushToPipeline (dataToSchedule);

  // The below event will cause this data to be sent out after pipeline delay
  Time transmissionTime = GetCmtsDsPipelineFactor () * GetDsSymbolTime ();
  NS_LOG_DEBUG ("Scheduling " << dataToSchedule << " for " << (Simulator::Now () + transmissionTime).As (Time::US));

  Simulator::Schedule (transmissionTime, &CmtsNetDevice::SendSymbol, this, dataToSchedule, symbolState);

  while (GetInternallyQueuedBytes () < GetPipelineData ())
    {
      Ptr<QueueDiscItem> item = GetQueue ()->Dequeue ();
      if (!item)
        {
          NS_LOG_DEBUG ("Empty device queue");
          break;
        }
      Ptr<Packet> packet = item->GetPacket ();
      NS_LOG_DEBUG ("Dequeue to staging queue");
      if (m_deviceQueue->Enqueue (item))
        {
          m_queueFramedBytes += GetMacFrameSize (packet->GetSize ());
          NS_LOG_DEBUG ("Packet enqueued at time " << Simulator::Now ().GetMicroSeconds () << "us");
          if (GetQueue ()->GetCurrentSize ().GetValue () == 0)
            {
              NS_LOG_DEBUG ("Stop empty feeder queue");
              break;
            }
        }
    }
}

void
CmtsNetDevice::AdvanceTokenState (uint32_t bytes)
{
  NS_LOG_FUNCTION (this << bytes);
  m_tokens -= bytes;
  m_peakTokens -= bytes;
}

void
CmtsNetDevice::AdvanceSymbolState (uint32_t bytes)
{
  NS_LOG_FUNCTION (this << bytes);
  NS_ABORT_MSG_IF (m_symbolState + bytes > GetDsSymbolCapacity (), "Error, symbol overflowed");
  m_symbolState += bytes;
}

uint32_t
CmtsNetDevice::BytesRemainingInSymbol (void) const
{
  return (static_cast<uint32_t> (GetDsSymbolCapacity ()) - m_symbolState);
}

uint32_t
CmtsNetDevice::SendFragment (void)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG (m_fragPkt, "Packet fragment does not exist");
  NS_ASSERT_MSG (m_fragPkt->GetSize () + GetDsMacHdrSize () > m_fragSentBytes, "Fragmentation error");
  // Finish as much of the fragment as possible
  bool canSendRemainingFragment = false;
  uint32_t fragmentSize = m_fragPkt->GetSize () + GetDsMacHdrSize () - m_fragSentBytes;
  if (fragmentSize <= m_scheduledBytes && fragmentSize <= BytesRemainingInSymbol ())
    {
      canSendRemainingFragment = true;
    }
  if (canSendRemainingFragment == false)
    {
      // Either fragmentSize > m_scheduledBytes or not enough bytes in symbol
      uint32_t allowedBytes = std::min<uint32_t> (m_scheduledBytes, BytesRemainingInSymbol ());
      if (allowedBytes > 0)
        {
          NS_LOG_DEBUG ("Continuing fragment with " << allowedBytes << " but will finish in future symbol");
          m_fragSentBytes += allowedBytes;
          // Finish up without actually sending
          AdvanceSymbolState (allowedBytes);
          AdvanceTokenState (allowedBytes);
          PopFromPipeline (allowedBytes);
          return allowedBytes;
        }
      else
        {
          NS_LOG_DEBUG ("No eligible bytes can be sent; m_scheduledBytes: " << m_scheduledBytes << " BytesRemainingInSymbol (): " << BytesRemainingInSymbol());
          return 0;
        }
    }
  NS_LOG_DEBUG ("Finishing fragment with " << fragmentSize);
  // Sending rest of fragment
  Time transmissionTime = GetDsSymbolTime ();
  bool result;
  Ptr<Packet> packetCopy = m_fragPkt->Copy ();
  if (UseDocsisChannel ())
    {
      AddDocsisHeader (m_fragPkt);
      m_phyTxBeginTrace (m_fragPkt);
      result = GetDocsisChannel ()->TransmitStart (m_fragPkt, this, transmissionTime);
    }
  else
    {
      NS_ASSERT (GetFdNetDevice ());
      m_phyTxBeginTrace (m_fragSdu);
      result = GetFdNetDevice ()->SendFrom (m_fragSdu, m_fragSrc, m_fragDest, m_fragProtocolNumber);
    }
  if (result == true)
    {
      AdvanceSymbolState (fragmentSize);
      AdvanceTokenState (fragmentSize);
      PopFromPipeline (fragmentSize);
      // Provide to pcap trace hooks
      m_snifferTrace (packetCopy);
      m_promiscSnifferTrace (packetCopy);
      m_state (packetCopy->GetSize () + GetDsMacHdrSize (), m_tokens, m_peakTokens, GetInternallyQueuedBytes (), GetQueue ()->GetCurrentSize ().GetValue (), GetPipelineData ());
    }
  else
    {
      NS_LOG_DEBUG ("Dropping due to TransmitStart failure");
      m_phyTxDropTrace (m_fragPkt);
    }
  m_fragPkt = 0;
  m_fragSdu = 0;
  m_fragSentBytes = 0;
  return fragmentSize;
}

void
CmtsNetDevice::IncrementTokens (Time elapsed)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("Tokens before increment " << m_tokens << " " << m_peakTokens);
  double currentTokens = m_tokens;
  currentTokens += (elapsed.GetSeconds () * m_maxSustainedRate.GetBitRate () / 8);
  currentTokens = (currentTokens > m_maxTrafficBurst) ? m_maxTrafficBurst : currentTokens;
  double peakTokens = m_peakTokens;
  peakTokens += (elapsed.GetSeconds () * m_peakRate.GetBitRate () / 8);
  peakTokens = (peakTokens > m_maxPdu) ? m_maxPdu : peakTokens;
  // Update these at the end of arithmetic, since they are traced values
  m_tokens = currentTokens;
  m_peakTokens = peakTokens;
  m_lastUpdateTime = Simulator::Now ();
  NS_LOG_DEBUG ("Tokens after increment " << m_tokens << " " << m_peakTokens);
}

Time
CmtsNetDevice::GetTransmissionTime (uint32_t bytes) const
{
  NS_LOG_FUNCTION (this << bytes);
  return GetDsSymbolTime ();
}

uint32_t
CmtsNetDevice::GetEligibleTokens (void) const
{
  NS_LOG_FUNCTION (this);
  if (m_tokens < 0 || m_peakTokens < 0)
    {
      return 0;
    }
  NS_LOG_DEBUG ("m_tokens " << m_tokens << " m_peakTokens " << m_peakTokens);
  uint32_t eligibleTokens = static_cast<uint32_t> (std::floor (m_tokens));
  uint32_t peakTokensInt = static_cast<uint32_t> (std::floor (m_peakTokens));
  if (eligibleTokens > peakTokensInt)
    {
      NS_LOG_DEBUG ("Bytes limited by peak tokens " << m_peakTokens);
      eligibleTokens = peakTokensInt;
    }
  return eligibleTokens;
}

uint32_t
CmtsNetDevice::CalculateFreeCapacity (void)
{
  NS_LOG_FUNCTION (this);
  uint32_t symbolCapacity = m_freeCapacityMean / 8; // bytes
  if (m_freeCapacityVariation == 0)
    {
      NS_LOG_DEBUG ("FreeCapacityMean " << m_freeCapacityMean << " symbolCapacity " << std::min<uint32_t> (GetDsSymbolCapacity (), symbolCapacity));
      return std::min<uint32_t> (GetDsSymbolCapacity (), symbolCapacity);
    }
  // Allow symbolCapacity to range lower or higher, based on the allowable
  // variation and a random variate
  int32_t w = int (m_freeCapacityVariation * symbolCapacity / 100);
  int32_t congestionVariate = int (m_congestionVariable->GetInteger ());
  int64_t r = (w == 0) ? 0 : congestionVariate % (2 * w) - w;
  // maximum free bytes in this symbol is symbolCapacity + r
  int64_t nextMax = symbolCapacity + r;
  // unsignedSymbolCapacity is the free capacity in the symbol
  uint32_t unsignedSymbolCapacity = nextMax < 0 ? 0 : (uint32_t) nextMax;
  unsignedSymbolCapacity = std::min<uint32_t> (GetDsSymbolCapacity (), unsignedSymbolCapacity);
  return unsignedSymbolCapacity; // bytes
}

void
CmtsNetDevice::TransmitComplete (void)
{
  NS_LOG_FUNCTION (this);
  if (m_pointToPointMode)
    {
      m_pointToPointBusy = false;
      SendImmediatelyIfAvailable ();
    }
}

void
CmtsNetDevice::SendImmediatelyIfAvailable (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<DocsisQueueDiscItem> item = DynamicCast<DocsisQueueDiscItem> (GetQueue ()->Dequeue ());
  if (!item)
    {
      NS_LOG_DEBUG ("No item in queue");
      return;
    }
  uint32_t sent = SendOut (item->GetPacket (), item->GetSource (), item->GetAddress (), item->GetProtocol ());
  NS_LOG_DEBUG ("Sent bytes (including Ethernet headers) " << sent);
}

bool
CmtsNetDevice::SendFrom (
  Ptr<Packet> packet,
  const Address& source,
  const Address& dest, 
  uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (this << packet << source << dest << protocolNumber);
  NS_LOG_DEBUG ("Received packet " << packet->GetUid () << " Ethernet payload size " << packet->GetSize ());
  NS_ABORT_MSG_UNLESS (packet->GetSize () <= GetMtu (), "Packet bigger than MTU");

  RemarkTcpEcnValue (packet);
  // If IsLinkUp() is false it means there is no channel to send any packet
  // over so we just hit the drop trace on the packet and return an error.
  if (IsLinkUp () == false)
    {
      m_macTxDropTrace (packet);
      return false;
    }
  m_macTxTrace (packet);
  Ptr<DocsisQueueDiscItem> item = Create<DocsisQueueDiscItem> (packet, source, dest, protocolNumber, GetDsMacHdrSize ());
  NS_LOG_DEBUG ("Enqueuing frame (with MAC header) of size " << item->GetSize ());
  bool retval = GetQueue ()->Enqueue (item);
  if (retval)
    {
      NS_LOG_DEBUG ("AQM enqueue succeeded; queue depth " << GetQueue ()->GetCurrentSize ().GetValue () << " bytes");
    }
  else
    {
      NS_LOG_DEBUG ("AQM enqueue failed; queue depth " << GetQueue ()->GetCurrentSize ().GetValue () << " bytes");
      m_macTxDropTrace (packet);
    }
  if (m_pointToPointMode && m_pointToPointBusy == false)
    {
      SendImmediatelyIfAvailable ();
    }
  return retval;
}

bool
CmtsNetDevice::Send (
  Ptr<Packet> packet, 
  const Address &dest, 
  uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (this << packet << dest << protocolNumber);
  NS_LOG_DEBUG ("Received packet " << packet->GetUid () << " size " << packet->GetSize ());
  NS_ASSERT_MSG (packet->GetSize () <= GetMtu (), "Packet bigger than MTU");
  bool result = false;

  RemarkTcpEcnValue (packet);

  NS_LOG_DEBUG ("AQM bytes: " << GetQueue ()->GetCurrentSize ().GetValue ());
  
  // If IsLinkUp() is false it means there is no channel to send any packet
  // over so we just hit the drop trace on the packet and return an error.
  if (IsLinkUp () == false)
    {
      m_macTxDropTrace (packet);
      return result;
    }
  m_macTxTrace (packet);
  Ptr<DocsisQueueDiscItem> item = Create<DocsisQueueDiscItem> (packet, GetAddress(), dest, protocolNumber, GetDsMacHdrSize ());
  if (m_deviceQueue->Enqueue (item))
    {
      result = true;
      m_queueFramedBytes += GetMacFrameSize (packet->GetSize ());
      NS_LOG_DEBUG ("Packet enqueued at time " << Simulator::Now ().GetMicroSeconds () << "us");
    }
  return result;
}

uint32_t
CmtsNetDevice::SendOut (
  Ptr<Packet> packet, 
  const Address &src,
  const Address &dest, 
  uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (this << packet << src << dest << protocolNumber);
  NS_ASSERT_MSG (m_fragPkt == 0, "Packet fragment still exists");
  NS_ASSERT_MSG (m_fragSentBytes == 0, "Fragment byte count still exists");
  uint32_t result = 0;

  Ptr<Packet> sduPacket = packet->Copy (); // Service data unit
  m_lastSduSize = packet->GetSize ();

  NS_LOG_DEBUG ("Frame size " << GetMacFrameSize (packet->GetSize ()) << " tokens " << m_tokens << " peak tokens " << m_peakTokens);

  Mac48Address destination = Mac48Address::ConvertFrom (dest);
  Mac48Address source = Mac48Address::ConvertFrom (src);
  AddHeaderTrailer (packet, source, destination, protocolNumber);

  // since we defer addition of MAC header, account for it here
  uint32_t packetSizeOnWire = packet->GetSize () + GetDsMacHdrSize ();
  bool canSendPacket = false;
  if (packetSizeOnWire <= BytesRemainingInSymbol () && packetSizeOnWire <= m_scheduledBytes)
    {
      canSendPacket = true;
    }
  Time txt = Seconds (0);
  Ptr<Packet> packetCopy = packet->Copy ();
  if (canSendPacket || m_pointToPointMode)
    {
      NS_LOG_DEBUG ("Can send full packet at this transmission opportunity");
      if (m_pointToPointMode)
        {
          txt = GetDataRate ().CalculateBytesTxTime (packet->GetSize ());
          NS_LOG_DEBUG ("Point to point mode (duration " << txt.GetSeconds () << "us)");
        }
      else
        {
          txt = GetTransmissionTime (packet->GetSize ());
          NS_LOG_DEBUG ("Fits in symbol - send it entirely (duration " << txt.GetSeconds () << "us)");
        }
      if (UseDocsisChannel ())
        {
          AddDocsisHeader (packet);
          m_phyTxBeginTrace (packet);
          bool retval = GetDocsisChannel ()->TransmitStart (packet, this, txt);
          if (retval)
            {
              result = packet->GetSize ();
              if (m_pointToPointMode)
                {
                  m_pointToPointBusy = true;
                  Simulator::Schedule (txt, &CmtsNetDevice::TransmitComplete, this);
                }
            }
        }
      else
        {
          NS_ASSERT (GetFdNetDevice ());
          txt = GetTransmissionTime (packetSizeOnWire);
          m_phyTxBeginTrace (sduPacket);
          bool retval = GetFdNetDevice ()->SendFrom (sduPacket, src, dest, protocolNumber);
          if (retval)
            {
              result = packetCopy->GetSize ();
            }
        }
    }
  else
    {
      // Can't send full packet
      uint32_t eligibleBytes = std::min<uint32_t> (BytesRemainingInSymbol (), m_scheduledBytes);
      NS_LOG_DEBUG ("Can send " << eligibleBytes << " of packet size " << packetCopy->GetSize () + GetDsMacHdrSize ());
      m_fragPkt = packet;
      m_fragSdu = sduPacket;
      m_fragSentBytes = eligibleBytes;
      m_fragSrc = src;
      m_fragDest = dest;
      m_fragProtocolNumber = protocolNumber;
      NS_LOG_DEBUG ("Sending fragment of " << m_fragSentBytes << " but will finish in future symbol");
      // Finish up without actually sending
      AdvanceSymbolState (eligibleBytes);
      AdvanceTokenState (eligibleBytes);
      PopFromPipeline (eligibleBytes);
      return eligibleBytes;
    }
  if (result > 0)
    {
      // Provide to pcap trace hooks, but first strip dummy header
      m_snifferTrace (packetCopy);
      m_promiscSnifferTrace (packetCopy);
      if (m_pointToPointMode)
        {
          return result;
        }
      AdvanceSymbolState (packetSizeOnWire);
      AdvanceTokenState (packetSizeOnWire);
      m_state (packetSizeOnWire, m_tokens, m_peakTokens, GetInternallyQueuedBytes (), GetQueue ()->GetCurrentSize ().GetValue (), GetPipelineData ());
      PopFromPipeline (packetSizeOnWire);
    }
  else
    {
      NS_LOG_DEBUG ("Dropping due to TransmitStart failure");
      m_phyTxDropTrace (packet);
    }
  return result;
}

void
CmtsNetDevice::StartDecoding (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  Simulator::Schedule (GetCmtsUsPipelineFactor () * GetFrameDuration (), &CmtsNetDevice::EndDecoding, this, packet);
}

void
CmtsNetDevice::RemoveReceivedDocsisHeader (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  DocsisHeader docsisHeader (GetUsMacHdrSize () - 6);
  packet->RemoveHeader (docsisHeader);
}

void
CmtsNetDevice::AddDocsisHeader (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  // Prepend a DocsisHeader; the header is currently a dummy
  // header to account for bytes.  Pass in the value of how many
  // extended header bytes to use
  DocsisHeader docsisHeader (GetDsMacHdrSize () - 6);
  packet->AddHeader (docsisHeader);
}

uint32_t
CmtsNetDevice::GetMacFrameSize (uint32_t sduSize) const
{
  NS_LOG_FUNCTION (this << sduSize);
  return (GetEthernetFrameSize (sduSize) + GetDsMacHdrSize ());
}

void
CmtsNetDevice::EndDecoding (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  m_phyRxEndTrace (packet);  // This trace is hit on all packets received
                             // from the channel.
  
  // Provide to pcap trace hooks, but first strip MAC header
  DocsisHeader hdr (GetUsMacHdrSize () - 6);
  packet->RemoveHeader (hdr);
  m_snifferTrace (packet);
  m_promiscSnifferTrace (packet);
  ForwardUp (packet);
}

// qDelaySingle() method, for PIE queue
Time
CmtsNetDevice::ExpectedDelay (void) const
{
  NS_LOG_FUNCTION (this);
  uint32_t queueSize = GetQueue ()->GetNBytes ();
  queueSize += GetInternallyQueuedBytes ();
  double latency;

  if (queueSize <= m_tokens)
    {
      latency = (8 * queueSize / m_peakRate.GetBitRate ());
    }
  else
    {
      double tokens = m_tokens.Get ();
      latency = 8 * (queueSize - tokens) / m_maxSustainedRate.GetBitRate ();
      if (tokens > 0)
        {
          latency +=  (8 * tokens) / m_peakRate.GetBitRate ();
        }
    }
  NS_LOG_DEBUG ("queue size " << queueSize << " tokens " << m_tokens << " expected delay " << latency);
  return Seconds (latency);
}

} // namespace docsis
} // namespace ns3
