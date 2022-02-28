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
#include "ns3/queue.h"
#include "ns3/simulator.h"
#include "ns3/mac48-address.h"
#include "ns3/ethernet-header.h"
#include "ns3/ethernet-trailer.h"
#include "ns3/error-model.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"
#include "ns3/pointer.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/queue-disc.h"
#ifdef HAVE_PACKET_H
#include "ns3/fd-net-device.h"
#endif
#include "ns3/net-device-queue-interface.h"
#include "ns3/ipv4-header.h"
#include "docsis-net-device.h"
#include "docsis-channel.h"
#include "docsis-header.h"
#include "cmts-upstream-scheduler.h"
#include "dual-queue-coupled-aqm.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DocsisNetDevice");

namespace docsis {

NS_OBJECT_ENSURE_REGISTERED (DocsisNetDevice);

TypeId 
DocsisNetDevice::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::DocsisNetDevice")
    .SetParent<NetDevice> ()
    .SetGroupName ("Docsis")
    .AddAttribute ("UseDocsisChannel",
                   "Whether to use Docsis or emulation channel",
                   BooleanValue (true),
                   MakeBooleanAccessor (&DocsisNetDevice::m_useDocsisChannel),
                   MakeBooleanChecker ())
    .AddAttribute ("ReceiveErrorModel",
                   "Receive error model used to simulate packet loss",
                   PointerValue (),
                   MakePointerAccessor (&DocsisNetDevice::m_receiveErrorModel),
                   MakePointerChecker<ErrorModel> ())
    .AddAttribute ("RemarkTcpEct0ToEct1",
                   "Enable to remark any received TCP ECT(0) packets to ECT(1)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&DocsisNetDevice::m_remarkTcpEct0ToEct1),
                   MakeBooleanChecker ())
    .AddAttribute ("UseConfigurationCache",
                   "Whether to cache values derived from constants set upon configuraiton time",
                   BooleanValue (true),
                   MakeBooleanAccessor (&DocsisNetDevice::m_useConfigurationCache),
                   MakeBooleanChecker ())
    .AddAttribute ("Mtu", "The MAC-level Maximum Transmission Unit",
                   UintegerValue (DEFAULT_MTU),
                   MakeUintegerAccessor (&DocsisNetDevice::SetMtu,
                                         &DocsisNetDevice::GetMtu),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("Address", 
                   "The MAC address of this device.",
                   Mac48AddressValue (Mac48Address ("ff:ff:ff:ff:ff:ff")),
                   MakeMac48AddressAccessor (&DocsisNetDevice::m_address),
                   MakeMac48AddressChecker ())
    .AddAttribute ("DataRate", // Deprecated
                   "The default data rate for DOCSIS links, used to compute serialization delay",
                   DataRateValue (DataRate ("32768b/s")),
                   MakeDataRateAccessor (&DocsisNetDevice::m_bps),
                   MakeDataRateChecker ())
     // Below are DOCSIS 3.1-related settable configuration attributes
     // Upstream
    .AddAttribute ("UsScSpacing",
                   "Upstream subcarrier spacing (Hz)",
                   DoubleValue (50e3),
                   MakeDoubleAccessor (&DocsisNetDevice::SetUsScSpacing,
                                       &DocsisNetDevice::GetUsScSpacing),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("NumUsSc",
                   "Number of upstream subcarriers",
                   UintegerValue (1880),
                   MakeUintegerAccessor (&DocsisNetDevice::m_numUsSc),
                   MakeUintegerChecker<uint32_t> (1,3800))
    .AddAttribute ("SymbolsPerFrame",
                   "Symbols per frame",
                   UintegerValue (6),
                   MakeUintegerAccessor (&DocsisNetDevice::m_symbolsPerFrame),
                   MakeUintegerChecker<uint32_t> (6,36))
    .AddAttribute ("UsSpectralEfficiency",
                   "Upstream spectral efficiency (bps/Hz)",
                   DoubleValue (10.0),
                   MakeDoubleAccessor (&DocsisNetDevice::m_usSpectralEfficiency),
                   MakeDoubleChecker<double> (1.0, 12.0))
    .AddAttribute ("UsCpLen",
                   "Upstream cyclic prefix length (Ncp).  Valid values: 96,128,160,192,224,256,288,320,384,512,640",
                   UintegerValue (256),
                   MakeUintegerAccessor (&DocsisNetDevice::m_usCpLen),
                   MakeUintegerChecker<uint32_t> (96, 640))
    .AddAttribute ("UsMacHdrSize",
                   "Upstream MAC header size",
                   UintegerValue (10),
                   MakeUintegerAccessor (&DocsisNetDevice::m_usMacHdrSize),
                   MakeUintegerChecker<uint32_t> (6, 246))
    .AddAttribute ("UsSegHdrSize",
                   "Upstream segment header size (always 8 bytes)",
                   UintegerValue (8),
                   MakeUintegerAccessor (&DocsisNetDevice::m_usSegHdrSize),
                   MakeUintegerChecker<uint32_t> (8,8))
    .AddAttribute ("MapInterval", 
                   "MAP interval (typically 2ms)",
                   TimeValue (MilliSeconds (2)),
                   MakeTimeAccessor (&DocsisNetDevice::m_mapInterval),
                   MakeTimeChecker ())
    .AddAttribute ("CmtsMapProcTime", 
                   "CMTS MAP processing time (typically 200us)",
                   TimeValue (MicroSeconds (200)),
                   MakeTimeAccessor (&DocsisNetDevice::m_cmtsMapProcTime),
                   MakeTimeChecker ())
    .AddAttribute ("CmtsUsPipelineFactor",
                   "Frame times in advance of burst that CM begins encoding",
                   UintegerValue (1),
                   MakeUintegerAccessor (&DocsisNetDevice::m_cmtsUsPipelineFactor),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("CmUsPipelineFactor",
                   "Frame times after burst completes that CMTS ends decoding",
                   UintegerValue (1),
                   MakeUintegerAccessor (&DocsisNetDevice::m_cmUsPipelineFactor),
                   MakeUintegerChecker<uint32_t> ())
     // Downstream
    .AddAttribute ("DsScSpacing",
                   "Downstream subcarrier spacing (Hz)",
                   DoubleValue (50e3),
                   MakeDoubleAccessor (&DocsisNetDevice::SetDsScSpacing,
                                       &DocsisNetDevice::GetDsScSpacing),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("NumDsSc",
                   "Number of downstream subcarriers",
                   UintegerValue (3745),
                   MakeUintegerAccessor (&DocsisNetDevice::m_numDsSc),
                   MakeUintegerChecker<uint32_t> (1,7537))
    .AddAttribute ("DsSpectralEfficiency",
                   "Downstream spectral efficiency (modulation choices) (bps/Hz)",
                   DoubleValue (12.0),
                   MakeDoubleAccessor (&DocsisNetDevice::m_dsSpectralEfficiency),
                   MakeDoubleChecker<double> (4.0, 14.0))
    .AddAttribute ("DsIntlvM",
                   "Downsteam interleaving M; M=1 means 'off'",
                   UintegerValue (3),
                   MakeUintegerAccessor (&DocsisNetDevice::m_dsIntlvM),
                   MakeUintegerChecker<uint32_t> (1,32))
    .AddAttribute ("DsCpLen",
                   "Downsteam cyclic prefix length (Ncp).  Valid values: 192,256,512,768,1024",
                   UintegerValue (512),
                   MakeUintegerAccessor (&DocsisNetDevice::m_dsCpLen),
                   MakeUintegerChecker<uint32_t> (192, 1024))
    .AddAttribute ("CmtsDsPipelineFactor",
                   "Symbol times in advance of tx that encoding begins",
                   UintegerValue (1),
                   MakeUintegerAccessor (&DocsisNetDevice::m_cmtsDsPipelineFactor),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("CmDsPipelineFactor",
                   "Symbol times after rx completes that decoding completes",
                   UintegerValue (1),
                   MakeUintegerAccessor (&DocsisNetDevice::m_cmDsPipelineFactor),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DsMacHdrSize",
                   "bytes (typically 10 for no channel bonding or 16 for channel bonding)",
                   UintegerValue (10),
                   MakeUintegerAccessor (&DocsisNetDevice::m_dsMacHdrSize),
                   MakeUintegerChecker<uint32_t> (6, 246))
    .AddAttribute ("AverageCodewordFill",
                   "Factor to account for 0xFF padding bytes, shortened codewords, etc.",
                   DoubleValue (0.99),
                   MakeDoubleAccessor (&DocsisNetDevice::m_averageCodewordFill),
                   MakeDoubleChecker<double> (0,1))
    .AddAttribute ("NcpModulation",
                   "allowed values:  2,4,6",
                   UintegerValue (4),
                   MakeUintegerAccessor (&DocsisNetDevice::m_ncpModulation),
                   MakeUintegerChecker<uint32_t> (2,6))
    // System configuration and other assumptions
    .AddAttribute ("NumUsChannels",
                   "number of US channels managed by this DS channel",
                   UintegerValue (1),
                   MakeUintegerAccessor (&DocsisNetDevice::m_numUsChannels),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("AverageUsBurst",
                   "bytes.  average size of an upstream burst",
                   UintegerValue (150),
                   MakeUintegerAccessor (&DocsisNetDevice::m_averageUsBurst),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("AverageUsUtilization",
                   "Average US utilization (ratio)",
                   DoubleValue (0.1),
                   MakeDoubleAccessor (&DocsisNetDevice::m_averageUsUtilization),
                   MakeDoubleChecker<double> (0,1))
    // HFC plant
    .AddAttribute ("MaximumDistance",
                   "Plant kilometers from furthest CM to CMTS",
                   DoubleValue (8),
                   MakeDoubleAccessor (&DocsisNetDevice::SetMaximumDistance,
                                       &DocsisNetDevice::GetMaximumDistance),
                   MakeDoubleChecker<double> (1,2000))
     // Below are DOCSIS 3.1-related attributes for getting derived values
     .AddAttribute ("ScPerMinislot",
                   "Upstream subcarriers per minislot",
                   TypeId::ATTR_GET,
                   UintegerValue (0), // this value is ignored for ATTR_GET
                   MakeUintegerAccessor (&DocsisNetDevice::GetScPerMinislot),
                   MakeUintegerChecker<uint32_t> ())
     .AddAttribute ("FrameDuration",
                   "Upstream frame duration",
                   TypeId::ATTR_GET,
                   TimeValue (Seconds (0)),// this value is ignored for ATTR_GET
                   MakeTimeAccessor (&DocsisNetDevice::GetFrameDuration),
                   MakeTimeChecker ())
     .AddAttribute ("MinislotsPerFrame",
                   "Upstream minislots per frame",
                   TypeId::ATTR_GET,
                   UintegerValue (0), // this value is ignored for ATTR_GET
                   MakeUintegerAccessor (&DocsisNetDevice::GetMinislotsPerFrame),
                   MakeUintegerChecker<uint32_t> ())
     .AddAttribute ("MinislotCapacity",
                   "Upstream minislot capacity (in bytes, assuming 80% code rate)",
                   TypeId::ATTR_GET,
                   UintegerValue (0), // this value is ignored for ATTR_GET
                   MakeUintegerAccessor (&DocsisNetDevice::GetMinislotCapacity),
                   MakeUintegerChecker<uint32_t> ())
     .AddAttribute ("UsCapacity",
                   "Upstream capacity (bits per second)",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetUsCapacity),
                   MakeDoubleChecker<double> ())
     .AddAttribute ("CmMapProcTime",
                   "CM MAP processing time",
                   TypeId::ATTR_GET,
                   TimeValue (Seconds (0)),// this value is ignored for ATTR_GET
                   MakeTimeAccessor (&DocsisNetDevice::GetCmMapProcTime),
                   MakeTimeChecker ())
     .AddAttribute ("DsSymbolTime",
                   "Downstream symbol time",
                   TypeId::ATTR_GET,
                   TimeValue (Seconds (0)),// this value is ignored for ATTR_GET
                   MakeTimeAccessor (&DocsisNetDevice::GetDsSymbolTime),
                   MakeTimeChecker ())
      .AddAttribute ("DsIntlvDelay",
                   "Downstream interleaver delay",
                   TypeId::ATTR_GET,
                   TimeValue (Seconds (0)), // this value is ignored for ATTR_GET
                   MakeTimeAccessor (&DocsisNetDevice::GetDsIntlvDelay),
                   MakeTimeChecker ())
      .AddAttribute ("Rtt",
                   "Round trip time",
                   TypeId::ATTR_GET,
                   TimeValue (Seconds (0)), // this value is ignored for ATTR_GET
                   MakeTimeAccessor (&DocsisNetDevice::GetRtt),
                   MakeTimeChecker ())
      .AddAttribute ("MinReqGntDelay",
                   "the closest (in frames) a request can be generated in order to be taken into account in a MAP",
                   TypeId::ATTR_GET,
                   UintegerValue (0), // this value is ignored for ATTR_GET
                   MakeUintegerAccessor (&DocsisNetDevice::GetMinReqGntDelay),
                   MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("FramesPerMap",
                   "number of OFDMA frames for each MAP interval",
                   TypeId::ATTR_GET,
                   UintegerValue (0), // this value is ignored for ATTR_GET
                   MakeUintegerAccessor (&DocsisNetDevice::GetFramesPerMap),
                   MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("MinislotsPerMap",
                   "number of minislots for each MAP interval",
                   TypeId::ATTR_GET,
                   UintegerValue (0), // this value is ignored for ATTR_GET
                   MakeUintegerAccessor (&DocsisNetDevice::GetMinislotsPerMap),
                   MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("ActualMapInterval",
                   "Reflect the actual MAP interval",
                   TypeId::ATTR_GET,
                   TimeValue (Seconds (0)), // this value is ignored for ATTR_GET
                   MakeTimeAccessor (&DocsisNetDevice::GetActualMapInterval),
                   MakeTimeChecker ())
      // MAP message downstream overhead
      .AddAttribute ("UsGrantsPerSecond",
                   "Average number of grants scheduled on each US channel",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetUsGrantsPerSecond),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("AvgIesPerMap",
                   "Average number of grants scheduled in each MAP",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetAvgIesPerMap),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("AvgMapSize",
                   "Average size of MAP message (bytes)",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetAvgMapSize),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("AvgMapDatarate",
                   "Average MAP datarate (bits/sec)",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetAvgMapDatarate),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("AvgMapOhPerSymbol",
                   "Average MAP overhead per symbol",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetAvgMapOhPerSymbol),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("ScPerNcp",
                   "subcarriers per NCP message block",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetScPerNcp),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("ScPerCw",
                   "subcarriers per codeword",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetScPerCw),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("DsCodewordsPerSymbol",
                   "Downstream codewords/symbol",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetDsCodewordsPerSymbol),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("DsSymbolCapacity",
                   "Downstream symbol capacity (bytes)",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetDsSymbolCapacity),
                   MakeDoubleChecker<double> ())
      .AddAttribute ("DsCapacity",
                   "Downstream capacity (bits/sec)",
                   TypeId::ATTR_GET,
                   DoubleValue (0), // this value is ignored for ATTR_GET
                   MakeDoubleAccessor (&DocsisNetDevice::GetDsCapacity),
                   MakeDoubleChecker<double> ())
    //
    // Trace sources at the "top" of the net device, where packets transition
    // to/from higher layers.
    //
    .AddTraceSource ("MacTx", 
                     "Trace source indicating a packet has arrived "
                     "for transmission by this device",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_macTxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("MacTxDrop", 
                     "Trace source indicating a packet has been dropped "
                     "by the device before transmission",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_macTxDropTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("MacPromiscRx", 
                     "A packet has been received by this device, "
                     "has been passed up from the physical layer "
                     "and is being forwarded up the local protocol stack.  "
                     "This is a promiscuous trace,",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_macPromiscRxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("MacRx", 
                     "A packet has been received by this device, "
                     "has been passed up from the physical layer "
                     "and is being forwarded up the local protocol stack.  "
                     "This is a non-promiscuous trace,",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_macRxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("DeviceEgress",
                     "Trace source for received packet exiting the device. "
                     "This trace source occurs at the same point as MacRx "
                     "trace point, except that it passes a non-const "
                     "Ptr<Packet> for possible tag removal."
                     "This is a non-promiscuous trace,",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_deviceEgressTrace),
                     "ns3::DocsisNetDevice::DeviceEgressCallback")
#if 0
    // Not currently implemented for this device
    .AddTraceSource ("MacRxDrop", 
                     "Trace source indicating a packet was dropped "
                     "before being forwarded up the stack",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_macRxDropTrace),
                     "ns3::Packet::TracedCallback")
#endif
    //
    // Trace souces at the "bottom" of the net device, where packets transition
    // to/from the channel.
    //
    .AddTraceSource ("PhyTxBegin", 
                     "Trace source indicating a packet has begun "
                     "transmitting over the channel",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_phyTxBeginTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("PhyTxEnd", 
                     "Trace source indicating a packet has been "
                     "completely transmitted over the channel",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_phyTxEndTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("PhyTxDrop", 
                     "Trace source indicating a packet has been "
                     "dropped by the device during transmission",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_phyTxDropTrace),
                     "ns3::Packet::TracedCallback")
#if 0
    // Not currently implemented for this device
    .AddTraceSource ("PhyRxBegin", 
                     "Trace source indicating a packet has begun "
                     "being received by the device",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_phyRxBeginTrace),
                     "ns3::Packet::TracedCallback")
#endif
    .AddTraceSource ("PhyRxEnd", 
                     "Trace source indicating a packet has been "
                     "completely received by the device",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_phyRxEndTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("PhyRxDrop", 
                     "Trace source indicating a packet has been "
                     "dropped by the device during reception",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_phyRxDropTrace),
                     "ns3::Packet::TracedCallback")

    //
    // Trace sources designed to simulate a packet sniffer facility (tcpdump).
    // Note that there is really no difference between promiscuous and 
    // non-promiscuous traces in a point-to-point link.
    //
    .AddTraceSource ("Sniffer", 
                    "Trace source simulating a non-promiscuous packet sniffer "
                     "attached to the device",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_snifferTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("PromiscSniffer", 
                     "Trace source simulating a promiscuous packet sniffer "
                     "attached to the device",
                     MakeTraceSourceAccessor (&DocsisNetDevice::m_promiscSnifferTrace),
                     "ns3::Packet::TracedCallback")
  ;
  return tid;
}


DocsisNetDevice::DocsisNetDevice () 
  :
    m_txMachineState (READY),
    m_channel (0),
    m_linkUp (false),
#ifdef HAVE_PACKET_H
    m_fdNetDevice (0),
#endif
    m_cachedFramesPerMap (0),
    m_cachedDsSymbolCapacity (0),
    m_cachedUsCapacity (0),
    m_cachedDsSymbolTime (Seconds (0)),
    m_cachedCmMapProcTime (Seconds (0)),
    m_cachedFrameDuration (Seconds (0)),
    m_cachedMinReqGntDelay (0),
    m_cachedMinislotCapacity (0),
    m_cacheInitialized (false)
{
  NS_LOG_FUNCTION (this);
}

DocsisNetDevice::~DocsisNetDevice ()
{
  NS_LOG_FUNCTION (this);
}

void
DocsisNetDevice::NotifyNewAggregate (void)
{
  NS_LOG_FUNCTION (this);
  NetDevice::NotifyNewAggregate ();
}

// Establish timings and schedule start of the CmtsUpstreamScheduler
uint32_t
DocsisNetDevice::StartScheduler (void)
{
  NS_LOG_FUNCTION (this);

  // MAP intervals will start on time boundaries corresponding to the MAP
  // interval duration.  For example, the first notional MAP interval starts
  // at time 0, the second at time GetActualMapInterval (), and so on.
  // The Ack Time will precede the first valid Alloc Start Time by
  // MinReqGntDelay (in minislots).  

  // This method advances the first Alloc Start Time to the point at which
  // the first Ack Time (i.e. Alloc Start Time - MinReqGntDelay) is
  // non-negative.

  // The following code determines what will be the first MAP interval to
  // possibly be scheduled by a MAP message arrival, and the corresponding
  // AllocStartTime and AckTime for the first MAP interval.
  uint32_t allocStartTime = TimeToMinislots (GetActualMapInterval ());
  uint32_t minReqGntDelaySlots = GetMinReqGntDelay () * GetMinislotsPerFrame ();
  Time firstGenerateMapTime = GetActualMapInterval () - GetCmMapProcTime () - GetDsIntlvDelay () - 3 * GetDsSymbolTime () - GetRtt ()/2 - GetCmtsMapProcTime ();
  while (allocStartTime < minReqGntDelaySlots)
    {
      firstGenerateMapTime += GetActualMapInterval ();
      allocStartTime += TimeToMinislots (GetActualMapInterval ());
    }
  NS_LOG_DEBUG ("First GenerateMap time: " << firstGenerateMapTime.As (Time::S) << ", AllocStartTime: " << allocStartTime);
  Simulator::Schedule (firstGenerateMapTime, &CmtsUpstreamScheduler::Start, GetCmtsUpstreamScheduler (), allocStartTime, minReqGntDelaySlots);
  return allocStartTime;
}

uint32_t
DocsisNetDevice::GetDataPduSize (uint32_t sduSize) const
{
  return std::max<uint32_t> (sduSize, 46) + 14 + 4;
}

uint32_t
DocsisNetDevice::GetUsMacFrameSize (uint32_t sduSize) const
{
  return GetDataPduSize (sduSize) + GetUsMacHdrSize ();
}

uint32_t
DocsisNetDevice::GetDsMacFrameSize (uint32_t sduSize) const
{
  return GetDataPduSize (sduSize) + GetDsMacHdrSize ();
}

uint32_t
DocsisNetDevice::TimeToMinislots (Time simulationTime) const
{
  NS_LOG_FUNCTION (this << simulationTime.GetSeconds ());
  uint64_t fullFrames = static_cast<uint64_t> (simulationTime.GetNanoSeconds () / GetFrameDuration ().GetNanoSeconds ());
  return static_cast<uint32_t> (GetMinislotsPerFrame () * fullFrames);
}

Time
DocsisNetDevice::MinislotsToTime (uint32_t minislots) const
{
  NS_LOG_FUNCTION (this << minislots);
  uint32_t frames = minislots / GetMinislotsPerFrame ();
  return (GetFrameDuration () * frames);
}


void
DocsisNetDevice::AddHeaderTrailer (Ptr<Packet> p, Mac48Address source, Mac48Address dest, uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (p << source << dest << protocolNumber);

  EthernetHeader header (false);
  header.SetSource (source);
  header.SetDestination (dest);

  //
  // All Ethernet frames must carry a minimum payload of 46 bytes.  We need
  // to pad out if we don't have enough bytes.  These must be real bytes
  // since they will be written to pcap files.
  if (p->GetSize () < 46)
    {
      uint8_t buffer[46];
      memset (buffer, 0, 46);
      Ptr<Packet> padd = Create<Packet> (buffer, 46 - p->GetSize ());
      NS_LOG_LOGIC ("Padding with " << 46 - p->GetSize () << " bytes");
      p->AddAtEnd (padd);
    }
  NS_LOG_LOGIC ("header.SetLengthType (EtherType)= (" << protocolNumber << ")");
  header.SetLengthType (protocolNumber);
  p->AddHeader (header);

  EthernetTrailer trailer;
  if (Node::ChecksumEnabled ())
    {
      trailer.EnableFcs (true);
    }
  trailer.CalcFcs (p);
  p->AddTrailer (trailer);
}

void
DocsisNetDevice::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_node = 0;
  m_channel = 0;
  m_queue = 0;
  m_cmtsUpstreamScheduler = 0;
  m_receiveErrorModel = 0;
  NetDevice::DoDispose ();
}

// This method is called at the start of simulation runtime.  The 
// TrafficControlLayer object should have been aggregated to the node
// by the InternetStackHelper.  From this object, we can obtain a
// pointer to the root QueueDisc.
void
DocsisNetDevice::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
  if (GetUsScSpacing () == 50e3)
    {
      NS_ABORT_MSG_IF (m_numUsSc > 1900, "Num_US_SC " << m_numUsSc << " too large for 50KHz subcarriers");
    }
  if (GetDsScSpacing () == 50e3)
    {
      NS_ABORT_MSG_IF (m_numDsSc > 3745, "Num_DS_SC " << m_numDsSc << " too large for 50KHz subcarriers");
    }
  if (GetDsScSpacing () == 25e3)
    {
      NS_ABORT_MSG_IF (m_dsIntlvM > 16, "DS_intlv_M " << m_dsIntlvM << " too large for 25KHz subcarriers");
    }

  if (m_ncpModulation != 2 && m_ncpModulation != 4 && m_ncpModulation != 6)
    {
      NS_ABORT_MSG ("NcpModulation illegal value " << m_ncpModulation);
    }
  if (UseDocsisChannel ())
    { 
      NS_ASSERT_MSG (m_channel, "No channel found");
      TimeValue tVal;
      m_channel->GetAttribute ("Delay", tVal);
      if (tVal.Get () != GetRtt ()/2)
        {
          NS_LOG_DEBUG ("Initializing channnel delay from " << tVal.Get ().As (Time::MS) << " ms to " << (GetRtt ()/2).As (Time::MS));
          m_channel->SetAttribute ("Delay", TimeValue (GetRtt ()/2));
        }
    }

  // Initialize cached values
  m_cachedFramesPerMap = GetFramesPerMap ();
  m_cachedDsSymbolCapacity = GetDsSymbolCapacity ();
  m_cachedUsCapacity = GetUsCapacity ();
  m_cachedDsSymbolTime = GetDsSymbolTime ();
  m_cachedCmMapProcTime = GetCmMapProcTime ();
  m_cachedFrameDuration = GetFrameDuration ();
  m_cachedMinReqGntDelay = GetMinReqGntDelay ();
  m_cachedMinislotCapacity = GetMinislotCapacity ();
  m_cacheInitialized = true;  // Set this true as a last step

}

DataRate
DocsisNetDevice::GetDataRate (void) const
{
  return m_bps;
}

Ptr<DocsisChannel>
DocsisNetDevice::GetDocsisChannel (void) const
{
  NS_ASSERT (m_channel);
  return DynamicCast<DocsisChannel> (GetChannel ());
}

bool
DocsisNetDevice::RemarkTcpEcnValue (Ptr<Packet> packet)
{
  bool result = false;
  NS_LOG_FUNCTION (this << packet);
  if (m_remarkTcpEct0ToEct1)
    {
      Ipv4Header ipv4Header;
      packet->PeekHeader (ipv4Header);
      if (ipv4Header.GetProtocol () == 6 && ipv4Header.GetEcn () == Ipv4Header::ECN_ECT0)
        {
          NS_LOG_DEBUG ("Remarking TCP packet ECT0 to ECT1");
          packet->RemoveHeader (ipv4Header);
          ipv4Header.SetEcn (Ipv4Header::ECN_ECT1);
          if (Node::ChecksumEnabled ())
            {
              ipv4Header.EnableChecksum ();
            }
          packet->AddHeader (ipv4Header);
          result = true;
        }
    }
  return result;
}

void
DocsisNetDevice::SetReceiveErrorModel (Ptr<ErrorModel> em)
{
  NS_LOG_FUNCTION (this << em);
  m_receiveErrorModel = em;
}

bool
DocsisNetDevice::Attach (Ptr<DocsisChannel> ch)
{
  NS_LOG_FUNCTION (this << &ch);

  m_channel = ch;

  m_channel->Attach (this);

  //
  // This device is up whenever it is attached to a channel.  A better plan
  // would be to have the link come up when both devices are attached, but this
  // is not done for now.
  //
  NotifyLinkUp ();
  return true;
}

void
DocsisNetDevice::Receive (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);

  m_phyRxBeginTrace (packet);
  StartDecoding (packet);
  return;
}

bool 
DocsisNetDevice::PromiscReceiveFromDevice (Ptr<NetDevice> device, 
                                           Ptr<const Packet> packet, 
                                           uint16_t protocol,
                                           Address const &source,
                                           Address const &dest,
                                           PacketType packetType)
{
  NS_LOG_FUNCTION (this << device << packet << protocol << source << dest << packetType);
  return ReceiveFromDevice (device, packet, protocol, source, dest, packetType, true);
}

bool 
DocsisNetDevice::NonPromiscReceiveFromDevice (Ptr<NetDevice> device, 
                                              Ptr<const Packet> packet, 
                                              uint16_t protocol,
                                              Address const &source)
{
  NS_LOG_FUNCTION (this << device << packet << protocol << source);
  return ReceiveFromDevice (device, packet, protocol, source, device->GetAddress (), NetDevice::PacketType (0), false);
}

bool 
DocsisNetDevice::ReceiveFromDevice (Ptr<NetDevice> device, 
                                    Ptr<const Packet> packet, 
                                    uint16_t protocol,
                                    Address const &source,
                                    Address const &dest,
                                    PacketType packetType,
                                    bool promiscuous)
{
  NS_LOG_FUNCTION (this << device << packet << protocol << source << dest << packetType << promiscuous);
  if (device == this)
    {
      // This is a placeholder to detect future cases that may arise if
      // the channel model changes and devices somehow receive their own 
      // packets
      NS_ABORT_MSG ("received own packet; check the ReceiveFromDevice method");
    }

  // Packets arrive here without any Ethernet or DOCSIS MAC headers
  // so add them here to allow for uniform processing
  Ptr<Packet> p = packet->Copy (); // Copy removes the constness
  AddHeaderTrailer (p, Mac48Address::ConvertFrom (source), Mac48Address::ConvertFrom (dest), protocol);
  AddDocsisHeader (p);
  Receive (p);
  return true;
}

void
DocsisNetDevice::ForwardUp (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);

  EthernetTrailer trailer;
  packet->RemoveTrailer (trailer);
  if (Node::ChecksumEnabled ())
    {
      trailer.EnableFcs (true);
    }

  bool crcGood = trailer.CheckFcs (packet);
  if (!crcGood)
    {
      NS_LOG_INFO ("CRC error on Packet " << packet);
      m_phyRxDropTrace (packet);
      return;
    }

  if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt (packet) )
    {
      NS_LOG_INFO ("Receiver error model is dropping packet " << packet);
      m_phyRxDropTrace (packet);
      return;
    }

  EthernetHeader header (false);
  packet->RemoveHeader (header);

  NS_LOG_LOGIC ("Pkt source is " << header.GetSource ());
  NS_LOG_LOGIC ("Pkt destination is " << header.GetDestination ());

  uint16_t protocol = header.GetLengthType ();
  //
  // Classify the packet based on its destination.
  //
  PacketType packetType;

  if (header.GetDestination ().IsBroadcast ())
    {
      packetType = PACKET_BROADCAST;
    }
  else if (header.GetDestination ().IsGroup ())
    {
      packetType = PACKET_MULTICAST;
    }
  else if (header.GetDestination () == m_address)
    {
      packetType = PACKET_HOST;
    }
  else
    {
      packetType = PACKET_OTHERHOST;
    }
  NS_LOG_LOGIC ("Pkt protocol is " << std::hex << protocol << std::dec << " type " << packetType);

  m_macPromiscRxTrace (packet);
  m_deviceEgressTrace (packet);
  if (!m_promiscCallback.IsNull ())
    {
      m_promiscCallback (this, packet, protocol, header.GetSource (), header.GetDestination (), packetType);
    }
  if (packetType != PACKET_OTHERHOST)
    {
      m_macRxTrace (packet);
      m_rxCallback (this, packet, protocol, header.GetSource ());
    }
}

void
DocsisNetDevice::NotifyLinkUp (void)
{
  NS_LOG_FUNCTION (this);
  m_linkUp = true;
  m_linkChangeCallbacks ();
}

void
DocsisNetDevice::SetIfIndex (const uint32_t index)
{
  NS_LOG_FUNCTION (this);
  m_ifIndex = index;
}

uint32_t
DocsisNetDevice::GetIfIndex (void) const
{
  return m_ifIndex;
}

Ptr<Channel>
DocsisNetDevice::GetChannel (void) const
{
  return m_channel;
}

//
// This is a point-to-point device, so we really don't need any kind of address
// information.  However, the base class NetDevice wants us to define the
// methods to get and set the address.  Rather than be rude and assert, we let
// clients get and set the address, but simply ignore them.

void
DocsisNetDevice::SetAddress (Address address)
{
  NS_LOG_FUNCTION (this << address);
  m_address = Mac48Address::ConvertFrom (address);
}

Address
DocsisNetDevice::GetAddress (void) const
{
  return m_address;
}

bool
DocsisNetDevice::IsLinkUp (void) const
{
  NS_LOG_FUNCTION (this);
  return m_linkUp;
}

void
DocsisNetDevice::AddLinkChangeCallback (Callback<void> callback)
{
  NS_LOG_FUNCTION (this);
  m_linkChangeCallbacks.ConnectWithoutContext (callback);
}

//
// This is a point-to-point device, so every transmission is a broadcast to
// all of the devices on the network.
//
bool
DocsisNetDevice::IsBroadcast (void) const
{
  NS_LOG_FUNCTION (this);
  return true;
}

//
// We don't really need any addressing information since this is a 
// point-to-point device.  The base class NetDevice wants us to return a
// broadcast address, so we make up something reasonable.
//
Address
DocsisNetDevice::GetBroadcast (void) const
{
  NS_LOG_FUNCTION (this);
  return Mac48Address ("ff:ff:ff:ff:ff:ff");
}

bool
DocsisNetDevice::IsMulticast (void) const
{
  NS_LOG_FUNCTION (this);
  return true;
}

Address
DocsisNetDevice::GetMulticast (Ipv4Address multicastGroup) const
{
  NS_LOG_FUNCTION (this);
  return Mac48Address ("01:00:5e:00:00:00");
}

Address
DocsisNetDevice::GetMulticast (Ipv6Address addr) const
{
  NS_LOG_FUNCTION (this << addr);
  return Mac48Address ("33:33:00:00:00:00");
}

bool
DocsisNetDevice::IsPointToPoint (void) const
{
  NS_LOG_FUNCTION (this);
  return true;
}

bool
DocsisNetDevice::IsBridge (void) const
{
  NS_LOG_FUNCTION (this);
  return false;
}

bool
DocsisNetDevice::SendFrom (Ptr<Packet> packet, 
                                 const Address &source, 
                                 const Address &dest, 
                                 uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (this << packet << source << dest << protocolNumber);
  return false;
}

Ptr<Node>
DocsisNetDevice::GetNode (void) const
{
  return m_node;
}

void
DocsisNetDevice::SetNode (Ptr<Node> node)
{
  NS_LOG_FUNCTION (this);
  m_node = node;
}

bool
DocsisNetDevice::NeedsArp (void) const
{
  NS_LOG_FUNCTION (this);
  return true;
}

void
DocsisNetDevice::SetReceiveCallback (NetDevice::ReceiveCallback cb)
{
  m_rxCallback = cb;
}

void
DocsisNetDevice::SetPromiscReceiveCallback (NetDevice::PromiscReceiveCallback cb)
{
  m_promiscCallback = cb;
}

bool
DocsisNetDevice::SupportsSendFrom (void) const
{
  return true;
}

Address 
DocsisNetDevice::GetRemote (void) const
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_channel->GetNDevices () == 2);
  for (uint32_t i = 0; i < m_channel->GetNDevices (); ++i)
    {
      Ptr<NetDevice> tmp = m_channel->GetDevice (i);
      if (tmp != this)
        {
          return tmp->GetAddress ();
        }
    }
  NS_ASSERT (false);
  // quiet compiler.
  return Address ();
}

bool
DocsisNetDevice::SetMtu (uint16_t mtu)
{
  NS_LOG_FUNCTION (this << mtu);
  m_mtu = mtu;
  return true;
}

uint16_t
DocsisNetDevice::GetMtu (void) const
{
  NS_LOG_FUNCTION (this);
  return m_mtu;
}

bool
DocsisNetDevice::UseDocsisChannel (void) const
{
  return m_useDocsisChannel;
}

#ifdef HAVE_PACKET_H
void
DocsisNetDevice::SetFdNetDevice (Ptr<FdNetDevice> device)
{
  m_fdNetDevice = device;
}

Ptr<FdNetDevice>
DocsisNetDevice::GetFdNetDevice (void) const
{
  return m_fdNetDevice;
}
#endif

Time
DocsisNetDevice::GetMapInterval (void) const
{
  return m_mapInterval;
}

void
DocsisNetDevice::SetUsScSpacing (double spacing)
{
  NS_ABORT_MSG_UNLESS (spacing == 25e3 || spacing == 50e3 , "Illegal spacing; must be 25e3 or 50e3");
  m_usScSpacing = spacing;
}

double
DocsisNetDevice::GetUsScSpacing (void) const
{
  return m_usScSpacing;
}

void
DocsisNetDevice::SetDsScSpacing (double spacing)
{
  NS_ABORT_MSG_UNLESS (spacing == 25e3 || spacing == 50e3 , "Illegal spacing; must be 25e3 or 50e3");
  m_dsScSpacing = spacing;
}

double
DocsisNetDevice::GetDsScSpacing (void) const
{
  return m_dsScSpacing;
}

void
DocsisNetDevice::SetMaximumDistance (double distance)
{
  m_maximumDistance = distance;
  if (m_channel)
    {
      TimeValue tVal;
      m_channel->GetAttribute ("Delay", tVal);
      if (tVal.Get () != GetRtt ()/2)
        {
          NS_LOG_DEBUG ("Setting channnel delay from " << tVal.Get ().As (Time::MS) << " ms to " << (GetRtt ()/2).As (Time::MS));
          m_channel->SetAttribute ("Delay", TimeValue (GetRtt ()/2));
        }
    }
}

double
DocsisNetDevice::GetMaximumDistance (void) const
{
  return m_maximumDistance;
}

uint32_t
DocsisNetDevice::GetScPerMinislot (void) const
{
  return (static_cast<uint32_t> (400e3 / GetUsScSpacing ()));
}

Time
DocsisNetDevice::GetFrameDuration (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedFrameDuration;
    }
  else
    {
      return (Seconds (m_symbolsPerFrame * ((1/GetUsScSpacing ()) + m_usCpLen/102.4e6)));
    }
}

uint32_t
DocsisNetDevice::GetMinislotsPerFrame (void) const
{
  return (static_cast<uint32_t> (m_numUsSc * GetUsScSpacing () / 400e3));
}

uint32_t
DocsisNetDevice::GetMinislotCapacity (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedMinislotCapacity;
    }
  else
    {
     // assuming 80% code rate
      double temp = floor (GetScPerMinislot () * m_symbolsPerFrame * m_usSpectralEfficiency * (0.8/8));
      return static_cast<uint32_t> (temp);
    }
}

double
DocsisNetDevice::GetUsCapacity (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedUsCapacity;
    }
  else
    {
      return (8 * GetMinislotCapacity () * GetMinislotsPerFrame () / GetFrameDuration ().GetSeconds ());
    }
}

Time
DocsisNetDevice::GetCmMapProcTime (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedCmMapProcTime;
    }
  else
    {
      return Seconds (600e-6 + GetFrameDuration ().GetSeconds () * (m_symbolsPerFrame + 1) / m_symbolsPerFrame);
    }
}

Time
DocsisNetDevice::GetCmtsMapProcTime (void) const
{
  return m_cmtsMapProcTime;
}

Time
DocsisNetDevice::GetDsSymbolTime (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedDsSymbolTime;
    }
  else
    {
      return Seconds ((1/GetDsScSpacing ()) + m_dsCpLen/204.8e6);
    }
}

Time
DocsisNetDevice::GetDsIntlvDelay (void) const
{
  return (m_dsIntlvM - 1) * GetDsSymbolTime ();
}

Time
DocsisNetDevice::GetRtt (void) const
{
  return Seconds (m_maximumDistance / 1e5);
}

// The closest (in frames) a request can be generated in order to be taken 
// into account in a MAP.  This assumes MAP message is sent entirely within
// one DS symbol.
uint32_t
DocsisNetDevice::GetMinReqGntDelay (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedMinReqGntDelay;
    }
  else
    {
      double pipelineFactor = (m_cmtsDsPipelineFactor + m_cmDsPipelineFactor + 1) / GetDsScSpacing ();
      uint32_t pipelineFactor2 = (m_cmUsPipelineFactor + m_cmtsUsPipelineFactor + 1);
      Time timeFactor = GetCmMapProcTime () + GetRtt () + GetDsIntlvDelay () + GetCmtsMapProcTime ();
      double temp = ceil ((timeFactor.GetSeconds () + pipelineFactor)/GetFrameDuration ().GetSeconds () + pipelineFactor2);
      return static_cast<uint32_t> (temp);
    }
}

uint32_t
DocsisNetDevice::GetFramesPerMap (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedFramesPerMap;
    }
  else
    {
      return static_cast<uint32_t> (round (m_mapInterval.GetSeconds () / GetFrameDuration ().GetSeconds ()));
    }
}

uint32_t
DocsisNetDevice::GetMinislotsPerMap (void) const
{
  return (GetFramesPerMap () * GetMinislotsPerFrame ());
}

Time
DocsisNetDevice::GetActualMapInterval (void) const
{
  return (GetFramesPerMap () * GetFrameDuration ());
}

double
DocsisNetDevice::GetUsGrantsPerSecond (void) const
{
  return (GetUsCapacity () * m_averageUsUtilization / (8 * m_averageUsBurst));
}

double
DocsisNetDevice::GetAvgIesPerMap (void) const
{
  return (GetUsGrantsPerSecond () * GetActualMapInterval ().GetSeconds ());
}

double
DocsisNetDevice::GetAvgMapSize (void) const
{
  return (46 + 4 * GetAvgIesPerMap ());
}

double
DocsisNetDevice::GetAvgMapDatarate (void) const
{
  return (m_numUsChannels * GetAvgMapSize () * 8 / GetActualMapInterval ().GetSeconds ());
}

double
DocsisNetDevice::GetAvgMapOhPerSymbol (void) const
{
  return (GetAvgMapDatarate () * GetDsSymbolTime ().GetSeconds () / 8);
}

double
DocsisNetDevice::GetScPerNcp (void) const
{
  return (48 / m_ncpModulation);
}

double
DocsisNetDevice::GetScPerCw (void) const
{
  return (2025 * 8 / m_dsSpectralEfficiency);
}

double
DocsisNetDevice::GetDsCodewordsPerSymbol (void) const
{
  return ((m_numDsSc - GetScPerNcp ())/ (GetScPerCw () + GetScPerNcp ()));
}

double
DocsisNetDevice::GetDsSymbolCapacity (void) const
{
  if (m_useConfigurationCache && m_cacheInitialized)
    {
      return m_cachedDsSymbolCapacity;
    }
  else
    {
      return round (GetDsCodewordsPerSymbol () * m_averageCodewordFill * 1777 - GetAvgMapOhPerSymbol ());
    }
}

double
DocsisNetDevice::GetDsCapacity (void) const
{
  return (GetDsSymbolCapacity () * 8 / GetDsSymbolTime ().GetSeconds ());
}

uint32_t
DocsisNetDevice::GetCmtsDsPipelineFactor (void) const
{
  return m_cmtsDsPipelineFactor;
}

uint32_t
DocsisNetDevice::GetCmDsPipelineFactor (void) const
{
  return m_cmDsPipelineFactor;
}

uint32_t
DocsisNetDevice::GetCmtsUsPipelineFactor (void) const
{
  return m_cmtsUsPipelineFactor;
}

uint32_t
DocsisNetDevice::GetCmUsPipelineFactor (void) const
{
  return m_cmUsPipelineFactor;
}

uint32_t
DocsisNetDevice::GetUsMacHdrSize (void) const
{
  return m_usMacHdrSize;
}

uint32_t
DocsisNetDevice::GetDsMacHdrSize (void) const
{
  return m_dsMacHdrSize;
}

void
DocsisNetDevice::SetQueue (Ptr<DualQueueCoupledAqm> dualQueue)
{
  NS_LOG_FUNCTION (this << dualQueue);
  m_queue = dualQueue;
}

Ptr<DualQueueCoupledAqm>
DocsisNetDevice::GetQueue (void) const
{
  return m_queue;
}

void
DocsisNetDevice::SetCmtsUpstreamScheduler (Ptr<CmtsUpstreamScheduler> scheduler)
{
  NS_LOG_FUNCTION (this << scheduler);
  m_cmtsUpstreamScheduler = scheduler;
}

Ptr<CmtsUpstreamScheduler>
DocsisNetDevice::GetCmtsUpstreamScheduler (void) const
{
  return m_cmtsUpstreamScheduler;
}

} // namespace docsis
} // namespace ns3
