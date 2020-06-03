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
 */

// This program is designed as a general program for use in constructing
// parametric simulation studies driven by other scripts (see the
// docsis experiments directory), assuming a single residential
// customer scenario.
//
// The main output data of interest is per-packet latency for the EF
// marked packets (upstream and downstream separately), and for non-EF
// marked packets.  Other items to track include page load time and 
// unused granted bytes.
//
//   residence                                      wide-area network
//   ---------                                      -----------------
//
// udpEfClient--+---+                                  ---------------
//              | b |                               ---| udpEfServer |
// udpClient----| r |                           (3)/   --------------
//              | i |  ------  -------- (2) -------- (4) -------------
// tcpClient----| d |--| CM |--| CMTS |----| router |----| udpServer |
//              | g |  ------  --------     --------     -------------
// dctcpClient--| e |       (1)             | | \  \(5)---------------
//              |   |                       | |  \  ---|dashServer(0)|
// webClient0---+---+                       | |   \    ---------------
//                                          |  \   \   ---------------
// Default one-way link delays               \  \   ---|dashServer(1)|
// -------------------                        \  \     ---------------
// (1) 40 us (see below)                       \  \         ...
// (2) 0 ms (bridged to CMTS)                   \  \(6)---------------
// (3) 2 ms                                      \  ---| tcpServer(0)|
// (4) 2 ms                                       \    ---------------
// (5) 10 ms                                       \        ...
// (6) variable (2 ms + i*3ms)                      (7)  -------------
// (7) 5 ms                                          ----| webServer |
//                                                       -------------
//
// Note:  Delay (1) set indirectly from the DocsisNetDevice::MaximumDistance
// attribute, which works out to 40 us one-way for the default value
//
//    ENDPOINT IP ADDRESSING & APPLICATION
//  ------------------------------------
// udpEfClient      10.1.1.1 - upstream UDP-EF
// udpClient        10.1.1.2 - upstream UDP-Default
// tcpClient        10.1.1.3 - TCP client for DASH or file transfers
// dctcpClient      10.1.1.4 - DCTCP client for DASH or file transfers
// webClient0       10.1.1.5 - Web Client 0
// webClient1       10.1.1.6 - Web Client 1 (if more than one enabled), etc.
// udpEfServer      10.1.2.2 - downstream UDP-EF
// udpServer        10.1.3.2 - downstream UDP-Default
// tcpServer(i)     10.1.(i+5).2 - downstream TCP/DASH servers
// dctcpServer(i)   10.2.(i+5).2 - downstream DCTCP/DASH servers
// webServer        10.5.1.2 - Web Server
//

#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <string>
#include <map>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/docsis-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/bridge-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE ("ResidentialExample");

// These variables are declared outside of main() function so that
// they are more easily accessible by the callbacks below
std::map<uint64_t, struct QueueDiscItemRecord> g_packetsSeenCm;
std::map<uint64_t, struct QueueDiscItemRecord> g_packetsSeenCmts;
uint32_t g_cmNodeId;
uint32_t g_cmtsNodeId;
Time g_grantsUnusedSamplingInterval;
Time g_transitionFromPageReading;
std::ofstream g_fileCm;
std::ofstream g_fileCmts;
std::ofstream g_fileCmDrop;
std::ofstream g_fileCmtsDrop;
std::ofstream g_fileTcpFileCompletion;
std::ofstream g_fileDctcpFileCompletion;
std::ofstream g_filePageLoadTime;
std::ofstream g_fileSummary;
std::ofstream g_fileGrantsUnused;
std::ofstream g_fileUnusedBandwidth;
std::ofstream g_fileCGrantState;
std::ofstream g_fileLGrantState;
std::ofstream g_fileTcpPacketTrace;
std::ofstream g_fileCQueueUpstreamBytes;
std::ofstream g_fileCQueueDownstreamBytes;
std::ofstream g_fileLQueueUpstreamBytes;
std::ofstream g_fileLQueueDownstreamBytes;
std::ofstream g_fileCQueueDropProbability;
std::ofstream g_fileLQueueMarkProbability;
std::ofstream g_fileDualQLlSojourn;
std::ofstream g_fileDualQClassicSojourn;
std::ofstream g_fileCalculatePState;
std::ofstream g_fileCongestionBytes;

// unique identifier for tagging packets (for latency tracing)
uint64_t g_packetUniqueId = 0;

// whether to use the number of file transfers completed as stopping criteria
uint32_t g_ftpStoppingCriteria = 0;

// counters used in callbacks
uint32_t g_grantsUnusedCounter = 0;
uint32_t g_grantsUsedCounter = 0;
uint32_t g_grantsCounter = 0;
uint32_t g_fileTransferCounter = 0;

uint32_t g_lArrivalsSinceLastUpdate = 0;
uint32_t g_cArrivalsSinceLastUpdate = 0;

const uint16_t ipv4ProtocolNumber = 0x0800;

/**
 * Tag class for tracking unique ID.  Although class ns3::Packet carries
 * a field for unique id, the way that the Packet Uid is used by applications
 * and TCP layer doesn't guarantee uniqueness at the device layer.  This
 * class allows the traffic control layer to tag a packet with a unique
 * identifier that can be read at the channel layer.
 */
class PacketUidTag : public Tag
{
public:
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (TagBuffer i) const;
  virtual void Deserialize (TagBuffer i);
  virtual void Print (std::ostream &os) const;

  // these are our accessors to our tag structure
  void SetUniqueId (uint64_t id);
  uint64_t GetUniqueId (void) const;
private:
  uint64_t m_uniqueId;
};

TypeId
PacketUidTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::PacketUidTag")
    .SetParent<Tag> ()
    .AddConstructor<PacketUidTag> ()
  ;
  return tid;
}

TypeId
PacketUidTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t
PacketUidTag::GetSerializedSize (void) const
{
  return 8;
}

void
PacketUidTag::Serialize (TagBuffer i) const
{
  i.WriteU64 (m_uniqueId);
}

void
PacketUidTag::Print (std::ostream &os) const
{
  os << "v=" << m_uniqueId;
}

void
PacketUidTag::Deserialize (TagBuffer i)
{
  m_uniqueId = i.ReadU64 ();
}

void
PacketUidTag::SetUniqueId (uint64_t value)
{
  m_uniqueId = value;
}

uint64_t
PacketUidTag::GetUniqueId (void) const
{
  return m_uniqueId;
}

struct QueueDiscItemRecord
{
  Ipv4Header header;
  Time txTime;
};

void
LowLatencySojournTrace (Time newVal)
{
  g_fileDualQLlSojourn << Simulator::Now ().GetSeconds () << " " << newVal.GetSeconds () * 1000 << std::endl;
}

void
ClassicSojournTrace (Time newVal)
{
  g_fileDualQClassicSojourn << Simulator::Now ().GetSeconds () << " " << newVal.GetSeconds () * 1000 << std::endl;
}

void
ClassicArrivalTrace (uint32_t size)
{
  g_cArrivalsSinceLastUpdate++;
}

void
LowLatencyArrivalTrace (uint32_t size)
{
  g_lArrivalsSinceLastUpdate++;
}


void
CalculatePStateTrace (Time qDelay, Time qDelayOld, double p, double dropProb, double baseProb, double probCL)
{
  g_fileCalculatePState << std::fixed << std::setprecision (6) << Simulator::Now ().GetSeconds () << " " 
    << qDelay.GetSeconds () << " " 
    << std::setw (7) <<  std::setprecision (3) << (qDelay - qDelayOld).GetSeconds () * 1000 << " " 
    << std::setw (3) << g_lArrivalsSinceLastUpdate << " " 
    << std::setw (3) << g_cArrivalsSinceLastUpdate << " " 
    << std::setw (6) << p << " " 
    << std::setprecision (4) << dropProb << " " 
    << baseProb << " " 
    << probCL << std::endl;
  g_lArrivalsSinceLastUpdate = 0;
  g_cArrivalsSinceLastUpdate = 0;
}

void
CQueueDropProbabilityTrace (double oldValue, double newValue)
{
  g_fileCQueueDropProbability << Simulator::Now ().GetSeconds () << " " << newValue << std::endl;
}

void
LQueueMarkProbabilityTrace (double oldValue, double newValue)
{
  g_fileLQueueMarkProbability << Simulator::Now ().GetSeconds () << " " << newValue << std::endl;
}

void
CQueueUpstreamBytesTrace (uint32_t oldValue, uint32_t newValue)
{
  g_fileCQueueUpstreamBytes << Simulator::Now ().GetSeconds () << " " << newValue << std::endl;
}

void
CQueueDownstreamBytesTrace (uint32_t oldValue, uint32_t newValue)
{
  g_fileCQueueDownstreamBytes << Simulator::Now ().GetSeconds () << " " << newValue << std::endl;
}

void
LQueueUpstreamBytesTrace (uint32_t oldValue, uint32_t newValue)
{
  g_fileLQueueUpstreamBytes << Simulator::Now ().GetSeconds () << " " << newValue << std::endl;
}

void
LQueueDownstreamBytesTrace (uint32_t oldValue, uint32_t newValue)
{
  g_fileLQueueDownstreamBytes << Simulator::Now ().GetSeconds () << " " << newValue << std::endl;
}

void
UpstreamFileTransferCompletionCallback (Time completionTime, uint32_t bytes, double throughput)
{
  g_fileTransferCounter++;
  g_fileTcpFileCompletion << Simulator::Now ().GetSeconds () << " " << completionTime.GetSeconds () << " " << bytes << " " << throughput / 1000000 << std::endl;
  if (g_ftpStoppingCriteria && g_fileTransferCounter >= g_ftpStoppingCriteria)
    {
      NS_LOG_INFO ("Stopping upstream file transfers at " << Simulator::Now ().GetSeconds () << " after " << g_fileTransferCounter << " FTPs");
      Simulator::Stop ();
    }
}

void
UpstreamDctcpFileTransferCompletionCallback (Time completionTime, uint32_t bytes, double throughput)
{
  g_fileDctcpFileCompletion << Simulator::Now ().GetSeconds () << " " << completionTime.GetSeconds () << " " << bytes << " " << throughput / 1000000 << std::endl;
}

void
GrantUnusedBandwidthTrace (void)
{
  g_fileUnusedBandwidth << Simulator::Now ().GetSeconds () << " "
    << (g_grantsUnusedCounter * 8 / g_grantsUnusedSamplingInterval.GetSeconds ())/1000000 << std::endl;
  g_grantsUnusedCounter = 0;
  Simulator::Schedule (g_grantsUnusedSamplingInterval, &GrantUnusedBandwidthTrace);
}

void
ClassicGrantStateTrace (CmNetDevice::CGrantState state)
{
  g_grantsUsedCounter += state.used;
  g_fileGrantsUnused << Simulator::Now ().GetSeconds () << " " << state.unused << std::endl;
  g_grantsUnusedCounter += state.unused;

  if (g_fileCGrantState.is_open ())
    {
      g_fileCGrantState << std::setw (8) << std::fixed << std::setprecision (6)
                        << Simulator::Now ().GetSeconds () << " "
                        << "sfid:"  << state.sfid << " "
                        << std::setw (5) << state.granted << " "
                        << std::setw (5) << state.used << " "
                        << std::setw (5) << state.unused << " " 
                        << std::setw (7) << state.queued << " "
                        << std::setprecision (3) 
                        << std::setw (7) << state.delay.GetSeconds () * 1000 << " "
                        << std::setw (6) << state.dropProb << std::endl;
    }
}

void
LowLatencyGrantStateTrace (CmNetDevice::LGrantState state)
{
  g_grantsUsedCounter += state.used;
  g_fileGrantsUnused << Simulator::Now ().GetSeconds () << " " << state.unused << std::endl;
  g_grantsUnusedCounter += state.unused;

  if (g_fileLGrantState.is_open ())
    {
      g_fileLGrantState << std::setw (8) << std::fixed << std::setprecision (6)
                        << Simulator::Now ().GetSeconds () << " "
                        << "sfid:"  << state.sfid << " "
                        << std::setw (5) << state.granted << " "
                        << std::setw (5) << state.used << " "
                        << std::setw (5) << state.unused << " " 
                        << std::setw (7) << state.queued << " "
                        << std::setprecision (3) 
                        << std::setw (7) << state.delay.GetSeconds () * 1000 << " "
                        << std::setw (6) << state.markProb << " "
                        << std::setw (6) << state.coupledMarkProb << std::endl;
    }
}

void
GrantTrace (uint32_t bytes)
{
  g_grantsCounter += bytes;
}

void
CongestionBytesTrace (uint32_t flowId, double p, uint32_t size)
{
    g_fileCongestionBytes << Simulator::Now ().GetSeconds () << " " << flowId << " " << p*size << " " << p << " " << size << std::endl;
}

void
DocsisEnqueueCallback (std::string context, Ptr<const QueueDiscItem> item)
{
  NS_LOG_FUNCTION (context << item);
  Ptr<const DocsisQueueDiscItem> docsisItem = DynamicCast<const DocsisQueueDiscItem> (item);
  NS_ABORT_MSG_UNLESS (docsisItem, "Should observe only DOCSIS queue disc items");
  if (docsisItem->GetProtocol () != ipv4ProtocolNumber)
    {
      NS_LOG_LOGIC ("non DOCSIS IPv4 packet received; value = " << std::hex << docsisItem->GetProtocol ());
      return;
    }
  Ipv4Header hdr;
  item->GetPacket ()->PeekHeader (hdr);
  QueueDiscItemRecord record;
  record.txTime = Simulator::Now ();
  record.header = hdr;
  if (context == "CM")
    {
      NS_LOG_LOGIC ("context " << context << " header " << record.header);
      PacketUidTag tag;
      uint64_t uid = g_packetUniqueId++;
      tag.SetUniqueId (uid);
      item->GetPacket ()->ReplacePacketTag (tag);
      g_packetsSeenCm[uid] = record;
    }
  else if (context == "CMTS")
    {
      NS_LOG_LOGIC ("context " << context << " header " << record.header);
      PacketUidTag tag;
      uint64_t uid = g_packetUniqueId++;
      tag.SetUniqueId (uid);
      item->GetPacket ()->ReplacePacketTag (tag);
      g_packetsSeenCmts[uid] = record;
    }
  else
    {
      std::cerr << "Unknown context " << context << std::endl;
      exit (1);
    }
}

void
DeviceEgressCallback (std::string context, Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (context << packet);
  std::map<uint64_t, struct QueueDiscItemRecord>::iterator it;
  PacketUidTag tag;
  bool found = packet->PeekPacketTag (tag);
  if (!found)
    {
      return;
    }
  uint64_t uid = tag.GetUniqueId ();
  if (context == "CM")
    {
      it = g_packetsSeenCm.find (uid);
      if (it != g_packetsSeenCm.end ())
        {
          QueueDiscItemRecord record = it->second;
          // Data format:  timestamp sojourn src_addr dst_addr proto tos
          // sojourn time is in units of ms
          g_fileCm << Simulator::Now ().GetMicroSeconds () / 1000000.0 << " "
                    << (Simulator::Now () - it->second.txTime).GetNanoSeconds () / 1000000.0 << " "
                    << record.header.GetSource ()  << " "
                    << record.header.GetDestination () << " "
                    << packet->GetSize () << " "
                    << (uint16_t) record.header.GetProtocol () << " "
                    << std::hex << (uint16_t) record.header.GetTos () << " "
                    << std::dec << std::endl;
          g_packetsSeenCm.erase (it); // only trace first instance of packet
          packet->RemovePacketTag (tag);
        }
    }
  else if (context == "CMTS")
    {
      it = g_packetsSeenCmts.find (uid);
      if (it != g_packetsSeenCmts.end ())
        {
          QueueDiscItemRecord record = it->second;
          // Data format:  timestamp sojourn src_addr dst_addr proto tos
          // sojourn time is in units of ms
          g_fileCmts << Simulator::Now ().GetMicroSeconds () / 1000000.0 << " "
                      << (Simulator::Now () - it->second.txTime).GetNanoSeconds () / 1000000.0 << " "
                      <<  record.header.GetSource ()  << " "
                      <<  record.header.GetDestination () << " "
                      << packet->GetSize () << " "
                      <<  (uint16_t) record.header.GetProtocol () << " "
                      << std::hex <<  (uint16_t) record.header.GetTos () << " "
                      << std::dec << std::endl;
          g_packetsSeenCmts.erase (it); // only trace first instance of packet
          packet->RemovePacketTag (tag);
        }
    }
  else
    {
      std::cerr << "Unknown context " << context << std::endl;
      exit (1);
    }
}

void 
DocsisDropCallback (std::string context, Ptr<const QueueDiscItem> item)
{
  Ipv4Header hdr;
  item->GetPacket ()->PeekHeader (hdr);

  if (context == "CM")
    {
      g_fileCmDrop << Simulator::Now ().GetMicroSeconds () / 1000000.0 << " "
                    << hdr.GetSource ()  << " "
                    << hdr.GetDestination () << " "
                    << item->GetPacket ()->GetSize () << " "
                    << (uint16_t) hdr.GetProtocol () << " "
                    << std::hex << (uint16_t) hdr.GetTos () << " "
                    << std::dec << std::endl;
    }
  else if (context == "CMTS")
    {
      g_fileCmtsDrop << Simulator::Now ().GetMicroSeconds () / 1000000.0 << " "
                    << hdr.GetSource ()  << " "
                    << hdr.GetDestination () << " "
                    << item->GetPacket ()->GetSize () << " "
                    << (uint16_t) hdr.GetProtocol () << " "
                    << std::hex << (uint16_t) hdr.GetTos () << " "
                    << std::dec << std::endl;
    }
  else
    {
      std::cerr << "Unknown context " << context << std::endl;
      exit (1);
    }
}

void
ServerConnectionEstablished (Ptr<const ThreeGppHttpServer>, Ptr<Socket>)
{
  NS_LOG_LOGIC ("Client has established a connection to the server.");

}

void
MainObjectGenerated (uint32_t size)
{
  NS_LOG_LOGIC ("Server generated a main object of " << size << " bytes.");
}

void
EmbeddedObjectGenerated (uint32_t size)
{
  NS_LOG_LOGIC ("Server generated an embedded object of " << size << " bytes.");
}

void
ServerTx (Ptr<const Packet> packet)
{
  NS_LOG_LOGIC ("Server sent a packet of " << packet->GetSize () << " bytes.");
}

void
ClientRx (std::string context, Ptr<const Packet> packet, const Address &address)
{
  NS_LOG_LOGIC ("Node " << context << " client received a packet of " << packet->GetSize () << " bytes from " << address);
}

void
ClientMainObjectReceived (std::string context, Ptr<const ThreeGppHttpClient>, Ptr<const Packet> packet)
{
  NS_LOG_LOGIC ("Node " << context << " client main object received");
  Ptr<Packet> p = packet->Copy ();
  ThreeGppHttpHeader header;
  p->RemoveHeader (header);
  if (header.GetContentLength () == p->GetSize ()
      && header.GetContentType () == ThreeGppHttpHeader::MAIN_OBJECT)
    {
      NS_LOG_LOGIC ("Client has succesfully received a main object of "
                   << p->GetSize () << " bytes.");
    }
  else
    {
      NS_LOG_LOGIC ("Client failed to parse a main object. ");
    }
}

void
ClientEmbeddedObjectReceived (std::string context, Ptr<const ThreeGppHttpClient>, Ptr<const Packet> packet)
{
  NS_LOG_LOGIC ("Node " << context << " client embedded object received");
  Ptr<Packet> p = packet->Copy ();
  ThreeGppHttpHeader header;
  p->RemoveHeader (header);
  if (header.GetContentLength () == p->GetSize ()
      && header.GetContentType () == ThreeGppHttpHeader::EMBEDDED_OBJECT)
    {
      NS_LOG_LOGIC ("Client has succesfully received an embedded object of "
                   << p->GetSize () << " bytes.");
    }
  else
    {
      NS_LOG_LOGIC ("Client failed to parse an embedded object. ");
    }
}

void
ClientStateTransitionCallback (std::string context, const std::string &oldState, const std::string &newState)
{
  NS_LOG_LOGIC ("Node " << context << " old state " << oldState << " new state " << newState);

  if (oldState == "READING" || oldState == "CONNECTING")
    {
      NS_LOG_LOGIC ("Transition from READING");
      g_transitionFromPageReading = Simulator::Now ();
    }
  if (newState == "READING")
    {
      NS_LOG_LOGIC ("Transition to READING");
      g_filePageLoadTime << Simulator::Now ().GetSeconds () << " " << (Simulator::Now () - g_transitionFromPageReading).GetSeconds () << std::endl;
    }
}

void
UnicastForwardTrace (const Ipv4Header & header, Ptr<const Packet> packet, uint32_t interface)
{
  if (header.GetProtocol () == 6)
    {
      if (header.GetFragmentOffset () == 0)
        {
          TcpHeader tcpHeader;
          packet->PeekHeader (tcpHeader);
          g_fileTcpPacketTrace << Simulator::Now ().GetSeconds ();
          // Add 20 bytes IPv4 header
          g_fileTcpPacketTrace << " " << packet->GetSize () + 20;
          g_fileTcpPacketTrace << " " << header.GetSource () << " " << tcpHeader.GetSourcePort ();
          g_fileTcpPacketTrace << " > " << header.GetDestination () << " " << tcpHeader.GetDestinationPort ();
          g_fileTcpPacketTrace << " " << (uint16_t) header.GetProtocol ();
          g_fileTcpPacketTrace << " " << std::hex << header.GetEcn () << std::dec;
          g_fileTcpPacketTrace << std::endl;
        }
      else
        {
          NS_LOG_WARN ("IP fragmentation of TCP segment detected; packet not counted");
        }
    }
}

////////////////////////////////////////////////////////////
// main program                                           //
////////////////////////////////////////////////////////////

int
main (int argc, char *argv[])
{

////////////////////////////////////////////////////////////
// variable configuration                                 //
////////////////////////////////////////////////////////////

  // Variables used in command-line arguments
  uint16_t numTcpDownloads = 0;
  uint16_t numTcpUploads = 0;
  uint16_t numTcpDashStreams = 0;
  uint16_t numDctcpDownloads = 0;
  uint16_t numDctcpUploads = 0;
  uint16_t numDctcpDashStreams = 0;
  uint16_t numWebUsers = 1;
  uint16_t numUdpEfUp = 1;
  uint16_t numUdpEfDown = 1;
  uint16_t numUdpBeUp = 1;
  uint16_t numUdpBeDown = 1;
  uint16_t numServiceFlows = 2;
  Time tcpDelayStart ("2ms");
  Time tcpDelayStep ("3ms");
  Time linkDelayWebServer ("5ms");
  DataRate upstreamMsr ("20Mbps"); 
  DataRate upstreamPeakRate (0); // defaults to 2 * upstreamMsr
  DataRate downstreamMsr ("100Mbps"); 
  DataRate downstreamPeakRate (0); // defaults to 2 * downstreamMsr
  Time maximumBurstTime = MilliSeconds (100);  // Used to set Maximum Traffic Burst
  DataRate guaranteedGrantRate (0);
  Time simulationEndTime ("10s");
  Time ftpStartTime ("0.2s");
  Time ftpStartStep ("1s");
  bool enableTrace = false;
  bool enablePcap = false;
  bool disableQP = false;
  Time queueDepthTime ("50ms"); // target latency for C-queue

  // Filenames used in command-line arguments; trace files written by default
  std::string fileNameCm = "latency-CM.dat";
  std::string fileNameCmts = "latency-CMTS.dat";
  std::string fileNamePageLoad = "pageLoadTime.dat";
  std::string fileNameTcpFileCompletion = "tcpFileCompletion.dat";
  std::string fileNameDctcpFileCompletion = "dctcpFileCompletion.dat";
  std::string fileNameSummary = "summary.dat";  
  // The following trace files are not written by default
  std::string fileNameGrantsUnused;
  std::string fileNameCQueueDownstreamBytes;
  std::string fileNameCQueueUpstreamBytes;
  std::string fileNameLQueueDownstreamBytes;
  std::string fileNameLQueueUpstreamBytes;
  std::string fileNameCQueueDropProbability;
  std::string fileNameLQueueMarkProbability;
  std::string fileNameUnusedBandwidth;
  std::string fileNameTcpPacketTrace;
  std::string fileNameCGrantState;
  std::string fileNameLGrantState;
  std::string fileNameClassicSojourn;
  std::string fileNameLowLatencySojourn;
  std::string fileNameCalculatePState;
  std::string fileNameCongestionBytes;
  std::string fileNameCmDrop;
  std::string fileNameCmtsDrop;
  // Filenames or prefixes used if pcap or ascii tracing is enabled
  std::string fileNamePcap = "residential";
  std::string fileNameUpstreamTrace = "docsis_upstream.tr";
  std::string fileNameDownstreamTrace = "docsis_downstream.tr";

  // Other constants
  int64_t streamIndex = 100;  // for assigning fixed random variable streams
  uint16_t servPortDownstream = 37000;  // base server port for downstream flows
  uint16_t servPortUpstream = 38500; // base server port for upstream flows
  uint16_t servPortDash = 39000; // base server port for DASH flows
  uint16_t servPortEfUp = 20000;
  uint16_t servPortEfDown = 25000;
  uint16_t servPortBeUp = 30000;
  uint16_t servPortBeDown = 35000;
  QueueSize queueSize (QueueSizeUnit::PACKETS, 100);  // packet queue depth; used for PointToPoint links
  Time linkDelayRtrToEfServer ("2ms");
  Time linkDelayRtrToUdpServer ("2ms");
  DataRate linkRateCsma ("1Gbps");
  DataRate linkRateWan ("1Gbps");
  std::string fileModel = "empirical";
  uint32_t maxDashBytes = 3750000;  // 3.75 MB for 2.5 seconds ~ 6 Mb/s
  Time dashDelayStart ("10ms"); // 10ms yields 20ms base RTT for DASH
  Time dashDelayStep ("0ms");
  Time dashStartTime ("0.2s");
  Time webClientStartTime ("1s");
  g_grantsUnusedSamplingInterval = MilliSeconds (1000);
  g_grantsUnusedCounter = 0;
  g_transitionFromPageReading = Seconds (0);
  std::string tcpSocketFactoryType = "ns3::TcpSocketFactory";
  std::string dctcpSocketFactoryType = "ns3::TcpSocketFactory";

  // HTTP model configuration
  Config::SetDefault ("ns3::ThreeGppHttpVariables::ReadingTimeMean",TimeValue (Seconds (3)));
  Config::SetDefault ("ns3::FileTransferApplication::UseFileTransferDuration", BooleanValue (true));
  // Avoid fragmentation; this variable controls TCP segment size
  Config::SetDefault ("ns3::ThreeGppHttpVariables::HighMtuSize", UintegerValue (1440));

  // TCP configuration -- increase default buffer sizes to improve throughput
  // over long delay paths
  Config::SetDefault ("ns3::TcpSocket::SndBufSize",UintegerValue (2048000));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize",UintegerValue (2048000));
  // Use 1500 byte IP packets for full-sized TCP data segments
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpNewReno::GetTypeId ()));
  // By default ns-3 DCTCP uses ECT(0) instead of ECT(1)
  Config::SetDefault ("ns3::TcpDctcp::UseEct0", BooleanValue (false));

  Config::SetDefault ("ns3::QueueDisc::Quota", UintegerValue (1024));

////////////////////////////////////////////////////////////
// command-line argument handling                         //
////////////////////////////////////////////////////////////

  CommandLine cmd;

  cmd.AddValue ("numTcpDownloads","Number of TCP downloads", numTcpDownloads);
  cmd.AddValue ("numTcpUploads","Number of TCP uploads", numTcpUploads);
  cmd.AddValue ("numTcpDashStreams","Number of Dash streams", numTcpDashStreams);
  cmd.AddValue ("numDctcpDownloads","Number of DCTCP downloads", numDctcpDownloads);
  cmd.AddValue ("numDctcpUploads","Number of DCTCP uploads", numDctcpUploads);
  cmd.AddValue ("numDctcpDashStreams","Number of Dash streams", numDctcpDashStreams);
  cmd.AddValue ("numWebUsers", "Number of Web users", numWebUsers);
  cmd.AddValue ("numUdpEfUp", "Number of upstream UDP EF flows", numUdpEfUp);
  cmd.AddValue ("numUdpEfDown", "Number of downstream UDP EF flows", numUdpEfDown);
  cmd.AddValue ("numUdpBeUp", "Number of upstream UDP best effort flows", numUdpBeUp);
  cmd.AddValue ("numUdpBeDown", "Number of downstream UDP best effort flows", numUdpBeDown);
  cmd.AddValue ("numServiceFlows", "Number of service flows (1 or 2)", numServiceFlows);
  cmd.AddValue ("tcpDelayStart","Point to Point Delay",tcpDelayStart);
  cmd.AddValue ("tcpDelayStep","Point to Point Step",tcpDelayStep);
  cmd.AddValue ("linkDelayWebServer","link delay from router to web server",linkDelayWebServer);
  cmd.AddValue ("upstreamMsr", "Upstream (aggregate) MSR", upstreamMsr);
  cmd.AddValue ("upstreamPeakRate", "Upstream peak rate", upstreamPeakRate);
  cmd.AddValue ("downstreamMsr", "Downstream (aggregate) MSR", downstreamMsr);
  cmd.AddValue ("downstreamPeakRate", "Downstream peak rate", downstreamPeakRate);
  cmd.AddValue ("guaranteedGrantRate", "Guaranteed grant rate for low latency flow", guaranteedGrantRate);
  cmd.AddValue ("fileModel", "Model the FTP file size:  empirical, speedtest, or unlimited", fileModel);
  cmd.AddValue ("simulationEndTime", "Time to end the simulation", simulationEndTime);
  cmd.AddValue ("ftpStoppingCriteria", "Number of FTP completions after which to end the simulation", g_ftpStoppingCriteria);
  cmd.AddValue ("ftpStartTime", "Ftp Start time", ftpStartTime);
  cmd.AddValue ("ftpStartStep", "Ftp Start time offset for each additional flow", ftpStartStep);
  cmd.AddValue ("fileNameCm", "filename for CM tracing", fileNameCm);
  cmd.AddValue ("fileNameCmts", "filename for CMTS tracing", fileNameCmts);
  cmd.AddValue ("fileNamePageLoad", "filename for Page Load Time tracing", fileNamePageLoad);
  cmd.AddValue ("fileNameTcpFileCompletion", "filename for upstream file transfer completion tracing", fileNameTcpFileCompletion);
  cmd.AddValue ("fileNameDctcpFileCompletion", "filename for upstream DCTCP file transfer completion tracing", fileNameDctcpFileCompletion);
  cmd.AddValue ("fileNameGrantsUnused", "filename for grants unused tracing", fileNameGrantsUnused);
  cmd.AddValue ("fileNameUnusedBandwidth", "filename for unused bandwidth tracing", fileNameUnusedBandwidth);
  cmd.AddValue ("fileNameTcpPacketTrace", "filename for TCP packet tracing", fileNameTcpPacketTrace);
  cmd.AddValue ("fileNameCGrantState", "filename for classic grant state tracing", fileNameCGrantState);
  cmd.AddValue ("fileNameLGrantState", "filename for low-latency grant state tracing", fileNameLGrantState);
  cmd.AddValue ("fileNameLowLatencySojourn", "filename for LL sojourn time tracing", fileNameLowLatencySojourn);
  cmd.AddValue ("fileNameClassicSojourn", "filename for classic sojourn time tracing", fileNameClassicSojourn);
  cmd.AddValue ("fileNameCQueueUpstreamBytes", "filename for upstream classic queue bytes tracing", fileNameCQueueUpstreamBytes);
  cmd.AddValue ("fileNameCQueueDownstreamBytes", "filename for downstream classic queue bytes tracing", fileNameCQueueDownstreamBytes);
  cmd.AddValue ("fileNameLQueueUpstreamBytes", "filename for upstream LL queue bytes tracing", fileNameLQueueUpstreamBytes);
  cmd.AddValue ("fileNameLQueueDownstreamBytes", "filename for downstream LL queue bytes tracing", fileNameLQueueDownstreamBytes);
  cmd.AddValue ("fileNameCQueueDropProbability", "filename for upstream classic queue drop probability", fileNameCQueueDropProbability);
  cmd.AddValue ("fileNameLQueueMarkProbability", "filename for upstream classic queue mark probability", fileNameLQueueMarkProbability);
  cmd.AddValue ("fileNameCalculatePState", "filename for LLD ClassicP state tracing", fileNameCalculatePState);
  cmd.AddValue ("fileNameSummary", "filename for summary statistics", fileNameSummary);
  cmd.AddValue ("fileNamePcap", "filename for .pcap", fileNamePcap);
  cmd.AddValue ("fileNameUpstreamTrace", "filename for upstream ascii", fileNameUpstreamTrace);
  cmd.AddValue ("fileNameDownstreamTrace", "filename for downstream ascii", fileNameDownstreamTrace);
  cmd.AddValue ("fileNameCongestionBytes", "filename for congestion bytes tracing", fileNameCongestionBytes);
  cmd.AddValue ("fileNameCmDrop", "filename for CM tracing", fileNameCmDrop);
  cmd.AddValue ("fileNameCmtsDrop", "filename for CM tracing", fileNameCmtsDrop);
  cmd.AddValue ("enableTrace", "enable tracing", enableTrace);
  cmd.AddValue ("enablePcap", "enable Pcap", enablePcap);
  cmd.AddValue ("queueDepthTime", "queue depth time", queueDepthTime);
  cmd.AddValue ("disableQP","disable queue protection", disableQP);
  cmd.Parse (argc, argv);

  LogComponentEnable ("ResidentialExample", LOG_LEVEL_INFO);

  if (numServiceFlows > 2 || numServiceFlows < 1)
    {
      std::cerr << "Error: only 1 or 2 service flows permitted" << std::endl;
      exit (1);
    }

  if (g_ftpStoppingCriteria > 0)
    {
      // Do not stop at a fixed simulation time, but instead when 
      // ftpStoppingCriteria transfers have completed.  Set
      // simulationEndTime to a large value; roughly 600 seconds (10 minutes)
      // for each FTP requested, so the simulation doesn't run forever
      simulationEndTime = Seconds (600 * g_ftpStoppingCriteria);
    }

  if (guaranteedGrantRate.GetBitRate () > 0)
    {
      Config::SetDefault ("ns3::docsis::CmtsUpstreamScheduler::GrantAllocationPolicy", EnumValue (CmtsUpstreamScheduler::SPREAD));
    }

  if (disableQP)
    {
      Config::SetDefault ("ns3::docsis::QueueProtection::QProtectOn", BooleanValue (false));
    }

  if (upstreamPeakRate.GetBitRate () == 0)
    {
      // Unless user has explicitly configured via command-line, set it to
      // twice the upstream token rate
      upstreamPeakRate = DataRate (2 * upstreamMsr.GetBitRate ());
    }
  if (downstreamPeakRate.GetBitRate () == 0)
    {
      // Unless user has explicitly configured via command-line, set it to
      // twice the downstream token rate
      downstreamPeakRate = DataRate (2 * downstreamMsr.GetBitRate ());
    }

  // Open and format active trace files
  if (!fileNameCm.empty ())
    {
      g_fileCm.open (fileNameCm.c_str (), std::ofstream::out);
      g_fileCm << std::fixed << std::setprecision (6);
    }
  if (!fileNameCmts.empty ())
    {
      g_fileCmts.open (fileNameCmts.c_str (), std::ofstream::out);
      g_fileCmts << std::fixed << std::setprecision (6);
    }
  if (!fileNameCmDrop.empty ())
    {
      g_fileCmDrop.open (fileNameCmDrop.c_str (), std::ofstream::out);
      g_fileCmDrop << std::fixed << std::setprecision (6);
    }
  if (!fileNameCmtsDrop.empty ())
    {
      g_fileCmtsDrop.open (fileNameCmtsDrop.c_str (), std::ofstream::out);
      g_fileCmtsDrop << std::fixed << std::setprecision (6);
    }
  if (!fileNameTcpFileCompletion.empty ())
    {
      g_fileTcpFileCompletion.open (fileNameTcpFileCompletion.c_str(), std::ofstream::out);
    }
  if (!fileNameDctcpFileCompletion.empty ())
    {
      g_fileDctcpFileCompletion.open (fileNameDctcpFileCompletion.c_str(), std::ofstream::out);
    }
  if (!fileNamePageLoad.empty ())
    {
      g_filePageLoadTime.open (fileNamePageLoad.c_str (), std::ofstream::out);
    }

  // Try to configure feeder queue depth to meet a latency target
  // Both downstream and upstream queues have different rates, configured above
  // Downstream is 10 Mb/s, Upstream is 2 Mb/s
  uint32_t downstreamFeederQueueDepth = uint32_t (queueDepthTime.GetSeconds () * downstreamMsr.GetBitRate () / 8);
  NS_LOG_INFO ("Using downstream feeder queue depth of " << downstreamFeederQueueDepth << " for queue depth time " << queueDepthTime.GetMilliSeconds () << "ms");
  uint32_t upstreamFeederQueueDepth = uint32_t (queueDepthTime.GetSeconds () * upstreamMsr.GetBitRate () /  8);
  NS_LOG_INFO ("Using upstream feeder queue depth of " << upstreamFeederQueueDepth << " for queue depth time " << queueDepthTime.GetMilliSeconds () << "ms");
  QueueSize upstreamMaxSize = QueueSize (QueueSizeUnit::BYTES, upstreamFeederQueueDepth);
  QueueSize downstreamMaxSize = QueueSize (QueueSizeUnit::BYTES, downstreamFeederQueueDepth);

////////////////////////////////////////////////////////////
// topology creation                                      //
////////////////////////////////////////////////////////////

  Ptr<Node> udpEfClient = CreateObject<Node> ();
  Ptr<Node> udpClient = CreateObject<Node> ();
  Ptr<Node> tcpClient = CreateObject<Node> ();
  Ptr<Node> dctcpClient = CreateObject<Node> ();
  NodeContainer webClients;
  webClients.Create (numWebUsers);
  Ptr<Node> bridge = CreateObject<Node> ();
  Ptr<Node> CM = CreateObject<Node> ();
  g_cmNodeId = CM->GetId ();
  Ptr<Node> CMTS = CreateObject<Node> ();
  g_cmtsNodeId = CMTS->GetId ();
  Ptr<Node> router = CreateObject<Node> ();
  Ptr<Node> udpEfServer = CreateObject<Node> ();
  Ptr<Node> udpServer = CreateObject<Node> ();

  // compute the maximum value of TcpUp and TcpDown
  uint16_t totalTcpDownloads = numTcpDownloads + numTcpDashStreams;
  uint16_t numTcpServers = std::max (totalTcpDownloads, numTcpUploads);
  uint16_t totalDctcpDownloads = numDctcpDownloads + numDctcpDashStreams;
  uint16_t numDctcpServers = std::max (totalDctcpDownloads, numDctcpUploads);
  NodeContainer tcpServers;
  tcpServers.Create (numTcpServers);
  NodeContainer dctcpServers;
  dctcpServers.Create (numDctcpServers);
  Ptr<Node> webServer = CreateObject<Node> ();

  //
  // Create the physical layer topology
  //
  NodeContainer clientLanEndpoints (udpEfClient, udpClient, tcpClient, dctcpClient);
  NodeContainer clientNs3Endpoints (udpEfClient, udpClient);
  NodeContainer clientTcpEndpoints (tcpClient);
  NodeContainer clientDctcpEndpoints (dctcpClient);
  if (numWebUsers > 0)
    {
      clientLanEndpoints.Add (webClients);
      clientNs3Endpoints.Add (webClients);
    }
  NodeContainer clientLan = clientLanEndpoints;
  clientLan.Add (CM);

  NetDeviceContainer clientLanDevices;
  NetDeviceContainer clientBridgeDevices;

  // (default) 1 Gbps and 1 microsecond delay for CSMA channel
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", DataRateValue (linkRateCsma));
  csma.SetChannelAttribute ("Delay", TimeValue (MicroSeconds (1)));

  NetDeviceContainer lanLink;
  for (uint32_t i = 0; i < clientLan.GetN () - 1; i++)
    {
      // install a csma channel between the ith toplan node and the bridge node
      lanLink = csma.Install (NodeContainer (clientLan.Get (i), bridge));
      clientLanDevices.Add (lanLink.Get (0));
      clientBridgeDevices.Add (lanLink.Get (1));
    }
  lanLink = csma.Install (NodeContainer (clientLan.Get (clientLan.GetN () - 1), bridge));
  clientBridgeDevices.Add (lanLink.Get (1));

  BridgeHelper bridgeHelper;
  bridgeHelper.Install (bridge, clientBridgeDevices);

  DocsisHelper docsis;

  NodeContainer docDevices (CMTS, CM);
  NetDeviceContainer linkDocsis = docsis.Install (CMTS, CM);

  NodeContainer cmtsRouterNodes (CMTS, router);
  NetDeviceContainer cmtsRouterLink = csma.Install (cmtsRouterNodes);
  // Add the router interface to the container for IP subnet addressing
  clientLanDevices.Add (cmtsRouterLink.Get (1));

  // TODO encapsulate this configuration within DocsisHelper
  NetDeviceContainer cmDevices;
  cmDevices.Add (linkDocsis.Get (1));
  cmDevices.Add (lanLink.Get (0));
  
  NetDeviceContainer cmtsDevices;
  cmtsDevices.Add (linkDocsis.Get (0));
  cmtsDevices.Add (cmtsRouterLink.Get (0));

  bridgeHelper.Install (CM, cmDevices);
  bridgeHelper.Install (CMTS, cmtsDevices);

  PointToPointHelper p2pRtrEfServer;
  p2pRtrEfServer.SetDeviceAttribute ("DataRate", DataRateValue (linkRateWan));
  p2pRtrEfServer.SetQueue ("ns3::DropTailQueue","MaxSize",QueueSizeValue (queueSize));
  p2pRtrEfServer.SetChannelAttribute ("Delay", TimeValue (linkDelayRtrToEfServer));

  NodeContainer routerUdpEfServer (router, udpEfServer);
  NetDeviceContainer routerUdpEfDevices = p2pRtrEfServer.Install (routerUdpEfServer);

  PointToPointHelper p2pRtrUdpServer;
  p2pRtrUdpServer.SetDeviceAttribute ("DataRate", DataRateValue (linkRateWan));
  p2pRtrUdpServer.SetQueue ("ns3::DropTailQueue","MaxSize",QueueSizeValue (queueSize));
  p2pRtrUdpServer.SetChannelAttribute ("Delay", TimeValue (linkDelayRtrToUdpServer));

  NodeContainer routerUdpServer (router, udpServer);
  NetDeviceContainer routerUdpDevices = p2pRtrUdpServer.Install (routerUdpServer);

  PointToPointHelper p2pRtrWebServer;
  NodeContainer routerWebServer (router, webServer);
  p2pRtrWebServer.SetDeviceAttribute ("DataRate", DataRateValue ( linkRateWan));
  p2pRtrWebServer.SetQueue ("ns3::DropTailQueue","MaxSize",QueueSizeValue (queueSize));
  p2pRtrWebServer.SetChannelAttribute ("Delay", TimeValue (linkDelayWebServer));
  NetDeviceContainer routerWebServerDevices = p2pRtrWebServer.Install (routerWebServer);

  // numTcpServers is the maximum of the sum of downstream FTP and dash servers
  // and the number of upstream FTP flows.  Create a point-to-point link
  // between router and each server, each with a base delay and each
  // subsequent one with a delay increment.  Handle DASH servers first, and
  // the rest are TCP (FTP) servers.
  std::vector<NetDeviceContainer> routerTcpServerDevices;
  uint16_t index = 0;
  for (; index < numTcpDashStreams; index++)
    {
      PointToPointHelper dashPtpHelper;
      NodeContainer routerTcpServer (router, tcpServers.Get (index));
      dashPtpHelper.SetDeviceAttribute ("DataRate", DataRateValue (linkRateWan));
      dashPtpHelper.SetQueue ("ns3::DropTailQueue", "MaxSize", QueueSizeValue (queueSize));
      Time delay = dashDelayStart + index * dashDelayStep;
      NS_LOG_INFO ("Link delay from DASH client/FTP upstream server node to router:  " << delay.GetMilliSeconds () << "ms");
      dashPtpHelper.SetChannelAttribute ("Delay", TimeValue (delay));
      NetDeviceContainer nodeC = dashPtpHelper.Install (routerTcpServer);
      routerTcpServerDevices.push_back (nodeC);
    }
  // Keep going with index, but start a new index j to reset the delay offset
  for (uint16_t j = 0; index < numTcpServers; index++, j++)
    {
      PointToPointHelper tcpPtpHelper;
      NodeContainer routerTcpServer (router, tcpServers.Get (index));
      tcpPtpHelper.SetDeviceAttribute ("DataRate", DataRateValue (linkRateWan));
      tcpPtpHelper.SetQueue ("ns3::DropTailQueue", "MaxSize", QueueSizeValue (queueSize));
      Time delay = tcpDelayStart + j * tcpDelayStep;
      NS_LOG_INFO ("Link delay from FTP client node to router: " << delay.GetMilliSeconds () << "ms");
      tcpPtpHelper.SetChannelAttribute ("Delay", TimeValue (delay));
      NetDeviceContainer nodeC = tcpPtpHelper.Install (routerTcpServer);
      routerTcpServerDevices.push_back (nodeC);
    }

  // Repeat above but for DCTCP servers.
  // numDctcpServers is the maximum of the sum of downstream FTP and dash servers
  // and the number of upstream FTP flows.  Create a point-to-point link
  // between router and each server, each with a base delay and each
  // subsequent one with a delay increment.  Handle DASH servers first, and
  // the rest are DCTCP (FTP) servers.
  std::vector<NetDeviceContainer> routerDctcpServerDevices;
  for (index = 0; index < numDctcpDashStreams; index++)
    {
      PointToPointHelper dashPtpHelper;
      NodeContainer routerDctcpServer (router, dctcpServers.Get (index));
      dashPtpHelper.SetDeviceAttribute ("DataRate", DataRateValue (linkRateWan));
      dashPtpHelper.SetQueue ("ns3::DropTailQueue", "MaxSize", QueueSizeValue (queueSize));
      Time delay = dashDelayStart + index * dashDelayStep;
      NS_LOG_INFO ("Link delay from DCTCP DASH client/FTP upstream server node to router:  " << delay.GetMilliSeconds () << "ms");
      dashPtpHelper.SetChannelAttribute ("Delay", TimeValue (delay));
      NetDeviceContainer nodeC = dashPtpHelper.Install (routerDctcpServer);
      routerDctcpServerDevices.push_back (nodeC);
    }
  // Keep going with index, but start a new index j to reset the delay offset
  for (uint16_t j = 0; index < numDctcpServers; index++, j++)
    {
      PointToPointHelper dctcpPtpHelper;
      NodeContainer routerDctcpServer (router, dctcpServers.Get (index));
      dctcpPtpHelper.SetDeviceAttribute ("DataRate", DataRateValue (linkRateWan));
      dctcpPtpHelper.SetQueue ("ns3::DropTailQueue", "MaxSize", QueueSizeValue (queueSize));
      Time delay = tcpDelayStart + j * tcpDelayStep;
      NS_LOG_INFO ("Link delay from DCTCP FTP downstream client node to router: " << delay.GetMilliSeconds () << "ms");
      dctcpPtpHelper.SetChannelAttribute ("Delay", TimeValue (delay));
      NetDeviceContainer nodeC = dctcpPtpHelper.Install (routerDctcpServer);
      routerDctcpServerDevices.push_back (nodeC);
    }

////////////////////////////////////////////////////////////
// protocol stack, device, and queue configuration        //
////////////////////////////////////////////////////////////

  InternetStackHelper stackHelper;
  InternetStackHelper dctcpStackHelper;
  dctcpStackHelper.SetTcp ("ns3::TcpL4Protocol", "SocketType", TypeIdValue (TcpDctcp::GetTypeId ())); 
  stackHelper.Install (clientNs3Endpoints);
  stackHelper.Install (clientTcpEndpoints);
  dctcpStackHelper.Install (clientDctcpEndpoints);
  stackHelper.Install (router);

  stackHelper.Install (udpEfServer);
  stackHelper.Install (udpServer);
  for (uint16_t i = 0; i < numTcpServers; i++)
    {
      stackHelper.Install (tcpServers.Get (i));
    }
  for (uint16_t i = 0; i < numDctcpServers; i++)
    {
      dctcpStackHelper.Install (dctcpServers.Get (i));
    }
  stackHelper.Install (webServer);
  
  Ptr<DualQueueCoupledAqm> dualQueue;
  Ptr<QueueProtection> queueProtection;
  // Install Upstream AQM Queue
  dualQueue = docsis.InstallLldCoupledQueue (docsis.GetUpstream (linkDocsis), upstreamMaxSize, upstreamMsr.GetBitRate ());
  queueProtection = dualQueue->GetQueueProtection ();
  if (!fileNameCQueueUpstreamBytes.empty ())
    {
      g_fileCQueueUpstreamBytes.open (fileNameCQueueUpstreamBytes.c_str (), std::ofstream::out);
      dualQueue->TraceConnectWithoutContext ("ClassicBytes", MakeCallback (&CQueueUpstreamBytesTrace));
    }
  if (!fileNameLQueueUpstreamBytes.empty ())
    {
      g_fileLQueueUpstreamBytes.open (fileNameLQueueUpstreamBytes.c_str (), std::ofstream::out);
      dualQueue->TraceConnectWithoutContext ("LowLatencyBytes", MakeCallback (&LQueueUpstreamBytesTrace));
    }
  if (!fileNameCQueueDropProbability.empty ())
    {
      g_fileCQueueDropProbability.open (fileNameCQueueDropProbability.c_str (), std::ofstream::out);
      dualQueue->TraceConnectWithoutContext ("ClassicDropProbability", MakeCallback (&CQueueDropProbabilityTrace));
    }
  if (!fileNameLQueueMarkProbability.empty ())
    {
      g_fileLQueueMarkProbability.open (fileNameLQueueMarkProbability.c_str (), std::ofstream::out);
      dualQueue->TraceConnectWithoutContext ("ProbNative", MakeCallback (&LQueueMarkProbabilityTrace));
    }
  if (!fileNameCalculatePState.empty ())
    {
      g_fileCalculatePState.open (fileNameCalculatePState.c_str (), std::ofstream::out);
      dualQueue->TraceConnectWithoutContext ("CalculatePState", MakeCallback (&CalculatePStateTrace));
    }
  if (!fileNameCongestionBytes.empty ())
    {
      g_fileCongestionBytes.open (fileNameCongestionBytes.c_str (), std::ofstream::out);
      queueProtection->TraceConnectWithoutContext ("CongestionBytesUpdate", MakeCallback (&CongestionBytesTrace));
    }
  dualQueue->TraceConnectWithoutContext ("LowLatencyArrival", MakeCallback (&LowLatencyArrivalTrace));
  dualQueue->TraceConnectWithoutContext ("ClassicArrival", MakeCallback (&ClassicArrivalTrace));

  // Install Downstream AQM Queue
  dualQueue = docsis.InstallLldCoupledQueue (docsis.GetDownstream (linkDocsis), downstreamMaxSize, downstreamMsr.GetBitRate ());
  if (!fileNameCQueueDownstreamBytes.empty ())
    {
      g_fileCQueueDownstreamBytes.open (fileNameCQueueDownstreamBytes.c_str (), std::ofstream::out);
      dualQueue->TraceConnectWithoutContext ("ClassicBytes", MakeCallback (&CQueueDownstreamBytesTrace));
    }
  if (!fileNameLQueueDownstreamBytes.empty ())
    {
      g_fileLQueueDownstreamBytes.open (fileNameLQueueDownstreamBytes.c_str (), std::ofstream::out);
      dualQueue->TraceConnectWithoutContext ("LowLatencyBytes", MakeCallback (&LQueueDownstreamBytesTrace));
    }

  if (!fileNameTcpPacketTrace.empty ())
    {
      g_fileTcpPacketTrace.open (fileNameTcpPacketTrace.c_str (), std::ofstream::out);
      // Trace IP packets forwarded at router
      Ptr<Ipv4L3Protocol> routerIpv4 = router->GetObject<Ipv4L3Protocol> ();
      routerIpv4->TraceConnectWithoutContext ("UnicastForward", MakeCallback (&UnicastForwardTrace));
    }

  // Add service flow definitions
  if (numServiceFlows == 1)
    {
      NS_LOG_DEBUG ("Adding single upstream (classic) service flow");
      Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
      sf->m_maxSustainedRate = upstreamMsr;
      sf->m_peakRate = upstreamPeakRate;
      sf->m_maxTrafficBurst = static_cast<uint32_t> (upstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);
      docsis.GetUpstream (linkDocsis)->SetUpstreamSf (sf);
      NS_LOG_DEBUG ("Adding single downstream (classic) service flow");
      sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
      sf->m_maxSustainedRate = downstreamMsr;
      sf->m_peakRate = downstreamPeakRate;
      sf->m_maxTrafficBurst = static_cast<uint32_t> (downstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);
      docsis.GetDownstream (linkDocsis)->SetDownstreamSf (sf);
    }
  else
    {
      NS_LOG_DEBUG ("Adding upstream aggregate service flow");
      Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow> ();
      asf->m_maxSustainedRate = upstreamMsr;
      asf->m_peakRate = upstreamPeakRate;
      asf->m_maxTrafficBurst = static_cast<uint32_t> (upstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);
      Ptr<ServiceFlow> sf1 = CreateObject<ServiceFlow> (CLASSIC_SFID);
      asf->SetClassicServiceFlow (sf1);
      Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
      sf2->m_guaranteedGrantRate = guaranteedGrantRate;
      asf->SetLowLatencyServiceFlow (sf2);
      docsis.GetUpstream (linkDocsis)->SetUpstreamAsf (asf);
      NS_LOG_DEBUG ("Adding downstream aggregate service flow");
      asf = CreateObject<AggregateServiceFlow> ();
      asf->m_maxSustainedRate = downstreamMsr;
      asf->m_peakRate = downstreamPeakRate;
      asf->m_maxTrafficBurst = static_cast<uint32_t> (downstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);
      sf1 = CreateObject<ServiceFlow> (CLASSIC_SFID);
      asf->SetClassicServiceFlow (sf1);
      sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
      asf->SetLowLatencyServiceFlow (sf2);
      docsis.GetDownstream (linkDocsis)->SetDownstreamAsf (asf);
    }
    
// Configure DOCSIS Channel & System parameters

  // Upstream channel parameters
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsScSpacing", DoubleValue (50e3));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("NumUsSc", UintegerValue (1880));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("SymbolsPerFrame", UintegerValue (6));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsSpectralEfficiency", DoubleValue (10.0));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsCpLen", UintegerValue (256));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsMacHdrSize", UintegerValue (10));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsSegHdrSize", UintegerValue (8));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("MapInterval", TimeValue (MilliSeconds (1)));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("CmtsMapProcTime", TimeValue (MicroSeconds (200)));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("CmtsUsPipelineFactor", UintegerValue (1));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("CmUsPipelineFactor", UintegerValue (1));

  // Upstream parameters that affect downstream UCD and MAP message overhead
  docsis.GetUpstream (linkDocsis)->SetAttribute ("NumUsChannels", UintegerValue (1));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("AverageUsBurst", UintegerValue (150));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("AverageUsUtilization", DoubleValue (0.1));

  // Downstream channel parameters
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsScSpacing", DoubleValue (50e3));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("NumDsSc", UintegerValue (3745));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsSpectralEfficiency", DoubleValue (12.0));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsIntlvM", UintegerValue (3));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsCpLen", UintegerValue (512));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("CmtsDsPipelineFactor", UintegerValue (1));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("CmDsPipelineFactor", UintegerValue (1));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsMacHdrSize", UintegerValue (10));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("AverageCodewordFill", DoubleValue (0.99));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("NcpModulation", UintegerValue (4));

  // Plant distance (km)
  docsis.GetUpstream (linkDocsis)->SetAttribute ("MaximumDistance", DoubleValue (8.0));




  // Trace bytes granted, used, and unused
  bool connected;  // variable used to track whether trace was connected
  connected = docsis.GetUpstream (linkDocsis)->TraceConnectWithoutContext ("ClassicGrantState", MakeCallback (&ClassicGrantStateTrace));
  NS_ASSERT_MSG (connected, "Couldn't hook trace source");
  connected = docsis.GetUpstream (linkDocsis)->TraceConnectWithoutContext ("LowLatencyGrantState", MakeCallback (&LowLatencyGrantStateTrace));
      NS_ASSERT_MSG (connected, "Couldn't hook trace source");
  if (!fileNameGrantsUnused.empty ())
    {
      g_fileGrantsUnused.open (fileNameGrantsUnused.c_str (), std::ofstream::out);
    }
  if (!fileNameCGrantState.empty ())
    {
      g_fileCGrantState.open (fileNameCGrantState.c_str (), std::ofstream::out);
    }
  if (!fileNameLGrantState.empty ())
    {
      g_fileLGrantState.open (fileNameLGrantState.c_str (), std::ofstream::out);
    }
  if (!fileNameUnusedBandwidth.empty ())
    {
      g_fileUnusedBandwidth.open (fileNameUnusedBandwidth.c_str (), std::ofstream::out);
      // Schedule the polling to determine unused bandwidth
      Simulator::Schedule (g_grantsUnusedSamplingInterval, &GrantUnusedBandwidthTrace);
    }
  connected = docsis.GetUpstream (linkDocsis)->TraceConnectWithoutContext ("ClassicGrantReceived", MakeCallback (&GrantTrace));
  NS_ASSERT_MSG (connected, "Couldn't hook trace source");
  connected = docsis.GetUpstream (linkDocsis)->TraceConnectWithoutContext ("LowLatencyGrantReceived", MakeCallback (&GrantTrace));
  NS_ASSERT_MSG (connected, "Couldn't hook trace source");
  if (!fileNameLowLatencySojourn.empty ())
    {
      g_fileDualQLlSojourn.open (fileNameLowLatencySojourn.c_str(), std::ofstream::out);
      connected = docsis.GetUpstream (linkDocsis)->TraceConnectWithoutContext ("LowLatencySojournTime", MakeCallback (&LowLatencySojournTrace));
      NS_ABORT_MSG_UNLESS (connected, "couldn't connect L");
    }
  if (!fileNameClassicSojourn.empty ())
    {
      g_fileDualQClassicSojourn.open (fileNameClassicSojourn.c_str (), std::ofstream::out);
      connected = docsis.GetUpstream (linkDocsis)->TraceConnectWithoutContext ("ClassicSojournTime", MakeCallback (&ClassicSojournTrace));
      NS_ABORT_MSG_UNLESS (connected, "couldn't connect C");
    }
  NS_UNUSED (connected);  // avoid compiler warning in optimized build

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer clientLanIfaces = ipv4.Assign (clientLanDevices);
  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer routerUdpEfIfaces = ipv4.Assign (routerUdpEfDevices);
  ipv4.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer routerUdpIfaces = ipv4.Assign (routerUdpDevices);
  std::vector<Ipv4Address> tcpServerAddresses;
  ipv4.SetBase ("10.1.5.0", "255.255.255.0");
  for (uint16_t i = 0; i < numTcpServers; i++)
    {
      Ipv4InterfaceContainer ipv4Iface = ipv4.Assign (routerTcpServerDevices[i]);
      tcpServerAddresses.push_back (ipv4Iface.GetAddress (1));
      ipv4.NewNetwork ();
    }
  std::vector<Ipv4Address> dctcpServerAddresses;
  ipv4.SetBase ("10.2.5.0", "255.255.255.0");
  for (uint16_t i = 0; i < numDctcpServers; i++)
    {
      Ipv4InterfaceContainer ipv4Iface = ipv4.Assign (routerDctcpServerDevices[i]);
      dctcpServerAddresses.push_back (ipv4Iface.GetAddress (1));
      ipv4.NewNetwork ();
    }
  ipv4.SetBase ("10.5.1.0", "255.255.255.0");
  Ipv4InterfaceContainer routerWebServerIfaces = ipv4.Assign (routerWebServerDevices);

  // Add global routing tables after the topology is fully constructed
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

////////////////////////////////////////////////////////////
// application configuration                              //
////////////////////////////////////////////////////////////
  
  NS_LOG_INFO ("Number of TCP downloads = " << numTcpDownloads);
  NS_LOG_INFO ("Number of TCP uploads = " << numTcpUploads);
  NS_LOG_INFO ("Number of TCP DASH streams = " << numTcpDashStreams);

  Time dashStartTimeIncrement = Seconds (5);
  if (numTcpDashStreams > 0)
    {
      dashStartTimeIncrement = dashStartTimeIncrement / numTcpDashStreams;
    }
  for (uint16_t i = 0; i < numTcpDashStreams; i++)
    {
      Ptr<Node> destNode = tcpClient;
      uint16_t destPort = servPortDash + i;
      Time startTime = dashStartTime + i * dashStartTimeIncrement + TimeStep (1);
      NS_LOG_INFO ("Add DASH session to " << destNode->GetId () << " port " << destPort << " start time " << startTime.GetSeconds () << "s");
      docsis.AddDashSession (tcpServers.Get (i), destNode, destPort, startTime, simulationEndTime, tcpSocketFactoryType, maxDashBytes);
    }

  for (uint16_t i = numTcpDashStreams; i < totalTcpDownloads; i++)
    {
      Ptr<Node> destNode = tcpClient;
      uint16_t destPort = servPortDownstream + (i - numTcpDashStreams);
      Time startTime = ftpStartTime + TimeStep (1);
      if (fileModel == "empirical")
        {
          // Stagger the start times
          startTime = ftpStartTime + (i - numTcpDashStreams) * ftpStartStep + TimeStep (1);
        }
      NS_LOG_INFO ("Add downstream FTP session to " << destNode->GetId () << " port " << destPort << " start time " << startTime.GetSeconds () << "s");
      docsis.AddFtpSession (tcpServers.Get (i), destNode, destPort, startTime, simulationEndTime, tcpSocketFactoryType, fileModel);
    }

  for (uint16_t i = 0; i < numTcpUploads; i++)
    {
      Ptr<Node> srcNode = tcpClient;
      uint16_t destPort = servPortUpstream + i;
      Time startTime = ftpStartTime + TimeStep (1);
      if (fileModel == "empirical")
        {
          // Stagger the start times
          startTime = ftpStartTime + i * ftpStartStep + TimeStep (1);
        }
      Ptr<FileTransferApplication> fileTransferApp;
      NS_LOG_INFO ("Add upstream FTP session to " << tcpServers.Get (i)->GetId () << " port " << destPort << " start time " << startTime.GetSeconds () << "s");
      fileTransferApp = docsis.AddFtpSession (srcNode, tcpServers.Get (i), destPort, startTime, simulationEndTime, tcpSocketFactoryType, fileModel);
      // Hook each upstream file transfer client for file completion stats
      fileTransferApp->TraceConnectWithoutContext ("FileTransferCompletion", MakeCallback (&UpstreamFileTransferCompletionCallback));
    }

  NS_LOG_INFO ("Number of DCTCP downloads = " << numDctcpDownloads);
  NS_LOG_INFO ("Number of DCTCP uploads = " << numDctcpUploads);
  NS_LOG_INFO ("Number of DCTCP DASH streams = " << numDctcpDashStreams);

  dashStartTimeIncrement = Seconds (5);
  if (numDctcpDashStreams > 0)
    {
      dashStartTimeIncrement = dashStartTimeIncrement / numDctcpDashStreams;
    }
  for (uint16_t i = 0; i < numDctcpDashStreams; i++)
    {
      Ptr<Node> destNode = dctcpClient;
      uint16_t destPort = servPortDash + i;
      Time startTime = dashStartTime + i * dashStartTimeIncrement + TimeStep (1);
      NS_LOG_INFO ("Add DCTCP DASH session to " << destNode->GetId () << " port " << destPort << " start time " << startTime.GetSeconds () << "s");
      docsis.AddDashSession (dctcpServers.Get (i), destNode, destPort, startTime, simulationEndTime, dctcpSocketFactoryType, maxDashBytes);
    }

  for (uint16_t i = numDctcpDashStreams; i < totalDctcpDownloads; i++)
    {
      Ptr<Node> destNode = dctcpClient;
      uint16_t destPort = servPortDownstream + (i - numDctcpDashStreams);
      Time startTime = ftpStartTime + TimeStep (1);
      if (fileModel == "empirical")
        {
          // Stagger the start times
          startTime = ftpStartTime + (i - numDctcpDashStreams) * ftpStartStep + TimeStep (1);
        }
      NS_LOG_INFO ("Add downstream DCTCP FTP session to " << destNode->GetId () << " port " << destPort << " start time " << startTime.GetSeconds () << "s");
      docsis.AddFtpSession (dctcpServers.Get (i), destNode, destPort, startTime, simulationEndTime, dctcpSocketFactoryType, fileModel);
    }

  for (uint16_t i = 0; i < numDctcpUploads; i++)
    {
      Ptr<Node> srcNode = dctcpClient;
      uint16_t destPort = servPortUpstream + i;
      Time startTime = ftpStartTime + TimeStep (1);
      if (fileModel == "empirical")
        {
          // Stagger the start times
          startTime = ftpStartTime + i * ftpStartStep + TimeStep (1);
        }
      Ptr<FileTransferApplication> fileTransferApp;
      NS_LOG_INFO ("Add upstream DCTCP FTP session to " << dctcpServers.Get (i)->GetId () << " port " << destPort << " start time " << startTime.GetSeconds () << "s");
      fileTransferApp = docsis.AddFtpSession (srcNode, dctcpServers.Get (i), destPort, startTime, simulationEndTime, dctcpSocketFactoryType, fileModel);
      // Hook each upstream file transfer client for file completion stats
      fileTransferApp->TraceConnectWithoutContext ("FileTransferCompletion", MakeCallback (&UpstreamDctcpFileTransferCompletionCallback));
    }

  // Create UDP packet flows
  for (uint16_t i = 0; i < numUdpEfUp; i++)
    {
      uint16_t servPortUdp = servPortEfUp + i;
      NS_LOG_INFO ("Add upstream UDP EF game session from " << clientLanIfaces.GetAddress (0) << " to " << udpEfServer->GetId () << " port " << servPortUdp);
      Ptr<GameClient> game = docsis.AddUpstreamGameSession (udpEfClient, udpEfServer, servPortUdp, TimeStep (1), simulationEndTime); 
      streamIndex += game->AssignStreams (streamIndex);
    }
  for (uint16_t i = 0; i < numUdpEfDown; i++)
    {
      uint16_t servPortUdp = servPortEfDown + i;
      NS_LOG_INFO ("Add downstream UDP EF game session from " << routerUdpEfIfaces.GetAddress (1) << " to " << udpEfClient->GetId () << " port " << servPortUdp);
      Ptr<GameClient> game = docsis.AddDownstreamGameSession (udpEfServer, udpEfClient, servPortUdp, TimeStep (1), simulationEndTime); 
      streamIndex += game->AssignStreams (streamIndex);
    }
  for (uint16_t i = 0; i < numUdpBeUp; i++)
    {
      uint16_t servPortUdp = servPortBeUp + i;
      NS_LOG_INFO ("Add upstream UDP best effort session from " << clientLanIfaces.GetAddress (1) << " to " << udpServer->GetId () << " port " << servPortUdp);
      Ptr<GameClient> game = docsis.AddUpstreamGameSession (udpClient, udpServer, servPortUdp, TimeStep (1), simulationEndTime, Ipv4Header::DscpDefault); 
      streamIndex += game->AssignStreams (streamIndex);
    }
  for (uint16_t i = 0; i < numUdpBeDown; i++)
    {
      uint16_t servPortUdp = servPortBeDown + i;
      NS_LOG_INFO ("Add downstream UDP best effort session from " << routerUdpIfaces.GetAddress (1) << " to " << udpClient->GetId () << " port " << servPortUdp);
      Ptr<GameClient> game = docsis.AddDownstreamGameSession (udpServer, udpClient, servPortUdp, TimeStep (1), simulationEndTime, Ipv4Header::DscpDefault); 
      streamIndex += game->AssignStreams (streamIndex);
    }

  Ipv4Address serverAddress = routerWebServerIfaces.GetAddress (1);
  if (webClients.GetN () > 0)
    {
      NS_LOG_INFO ("Adding " << webClients.GetN () << " web clients");
      ThreeGppHttpServerHelper serverHelper (serverAddress);

      // Install HTTP server
      ApplicationContainer serverApps = serverHelper.Install (webServer);
      Ptr<ThreeGppHttpServer> httpServer = serverApps.Get (0)->GetObject<ThreeGppHttpServer> ();

      // Example of connecting to the trace sources
      httpServer->TraceConnectWithoutContext ("ConnectionEstablished", MakeCallback (&ServerConnectionEstablished));
      httpServer->TraceConnectWithoutContext ("MainObject", MakeCallback (&MainObjectGenerated));
      httpServer->TraceConnectWithoutContext ("EmbeddedObject", MakeCallback (&EmbeddedObjectGenerated));
      httpServer->TraceConnectWithoutContext ("Tx", MakeCallback (&ServerTx));

      // Setup HTTP variables for the server
      PointerValue ptrValue;
      httpServer->GetAttribute ("Variables", ptrValue);
      Ptr<ThreeGppHttpVariables> httpVariables = ptrValue.Get<ThreeGppHttpVariables> ();
      httpVariables->SetMainObjectSizeMean (102400); // 100kB
      httpVariables->SetMainObjectSizeStdDev (40960); // 40kB
    }

  // Create HTTP client helper
  ThreeGppHttpClientHelper clientHelper (serverAddress);

  // Install HTTP clients
  ApplicationContainer clientApps; 
  for (uint16_t i = 0; i < webClients.GetN (); i++)
    {
      ApplicationContainer a = clientHelper.Install (webClients.Get (i));
      clientApps.Add (a);
      Ptr<ThreeGppHttpClient> httpClient = a.Get (0)->GetObject<ThreeGppHttpClient> ();

      std::stringstream ss;
      ss << webClients.Get (i)->GetId ();

      // Example of connecting to the trace sources
      httpClient->TraceConnect ("RxMainObject", ss.str (), MakeCallback (&ClientMainObjectReceived));
      httpClient->TraceConnect ("RxEmbeddedObject", ss.str (), MakeCallback (&ClientEmbeddedObjectReceived));
      httpClient->TraceConnect ("Rx", ss.str (), MakeCallback (&ClientRx));
      httpClient->TraceConnect ("StateTransition", ss.str (), MakeCallback (&ClientStateTransitionCallback));
    }

  clientApps.Start (webClientStartTime + TimeStep (1));
  clientApps.Stop (simulationEndTime);

////////////////////////////////////////////////////////////
// additional tracing configuration                       //
////////////////////////////////////////////////////////////

  // Trace latency for node CM.  Traces on both sides of the link are hooked.
  docsis.GetUpstream (linkDocsis)->GetQueue ()->TraceConnect ("Enqueue", "CM", MakeCallback (&DocsisEnqueueCallback));
  docsis.GetDownstream (linkDocsis)->TraceConnect("DeviceEgress", "CM", MakeCallback (&DeviceEgressCallback));
  // Trace drop for node CM
  docsis.GetUpstream (linkDocsis)->GetQueue ()->TraceConnect ("Drop", "CM", MakeCallback (&DocsisDropCallback));

  // Repeat for node CMTS
  docsis.GetDownstream (linkDocsis)->GetQueue ()->TraceConnect ("Enqueue", "CMTS", MakeCallback (&DocsisEnqueueCallback));
  docsis.GetUpstream (linkDocsis)->TraceConnect("DeviceEgress", "CMTS", MakeCallback (&DeviceEgressCallback));
  docsis.GetDownstream (linkDocsis)->GetQueue ()->TraceConnect ("Drop", "CMTS", MakeCallback (&DocsisDropCallback));

  //Ask for ASCII and pcap traces of network traffic
  if (enableTrace)
    {
      AsciiTraceHelper ascii;
      docsis.EnableAscii (fileNameUpstreamTrace, linkDocsis.Get (0), true);
      docsis.EnableAscii (fileNameDownstreamTrace, linkDocsis.Get (1), true);
    }
  if (enablePcap)
    {
      // the cable modem device in the clientBridgeDevices container will be
      // the last one added; i.e. clientBridgeDevices.GetN () - 1
      uint32_t cableModemIndex = clientBridgeDevices.GetN () - 1;
      csma.EnablePcap (fileNamePcap.c_str (), clientBridgeDevices.Get (cableModemIndex), true);
    }


////////////////////////////////////////////////////////////
// Run simulation                                         //
////////////////////////////////////////////////////////////

  NS_LOG_INFO ("Run simulation for " << simulationEndTime.GetSeconds () << "s");
  Simulator::Stop (simulationEndTime);
  Simulator::Run ();
  NS_LOG_INFO ("Start post-processing");

////////////////////////////////////////////////////////////
// Data post-processing                                   //
////////////////////////////////////////////////////////////

  UintegerValue uVal;
  TimeValue tVal;
  DoubleValue dVal;

  std::cout << "=================" <<std::endl;
  std::cout << "Configured values" <<std::endl;
  std::cout << "=================" <<std::endl;
  std::cout << std::endl;

  std::cout << "upstream channel configuration parameters and defaults" <<std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("UsScSpacing", dVal);
  std::cout << "US_SC_spacing = " << dVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("NumUsSc", uVal);
  std::cout << "Num_US_SC = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("SymbolsPerFrame", uVal);
  std::cout << "Symbols_per_frame = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("UsSpectralEfficiency", dVal);
  std::cout << "US_Spectral_Efficiency = "  << dVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("UsCpLen", uVal);
  std::cout << "US_CP_len = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("UsMacHdrSize", uVal);
  std::cout << "US_MAC_HDR_SIZE = "  << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("UsSegHdrSize", uVal);
  std::cout << "US_SEG_HDR_SIZE = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("MapInterval", tVal);
  std::cout << "MAP_interval = " << tVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("CmtsMapProcTime", tVal);
  std::cout << "CMTS_MAP_Proc_Time = " << tVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("CmtsUsPipelineFactor", uVal);
  std::cout << "CM_US_pipeline_factor = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("CmUsPipelineFactor", uVal);
  std::cout << "CMTS_US_pipeline_factor = " << uVal.Get () << std::endl;
  std::cout << std::endl;

  std::cout << "downstream channel configuration parameters and defaults" << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsScSpacing", dVal);
  std::cout << "DS_SC_spacing = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("NumDsSc", uVal);
  std::cout << "Num_DS_SC = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsSpectralEfficiency", dVal);
  std::cout << "DS_Spectral_Efficiency = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsIntlvM", uVal);
  std::cout << "DS_intlv_M = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsCpLen", uVal);
  std::cout << "DS_CP_len = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("CmtsDsPipelineFactor", uVal);
  std::cout << "CMTS_DS_pipeline_factor = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("CmDsPipelineFactor", uVal);
  std::cout << "CM_DS_pipeline_factor = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsMacHdrSize", uVal);
  std::cout << "DS_MAC_HDR_SIZE = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("AverageCodewordFill", dVal);
  std::cout << "Average_Codeword_Fill = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("NcpModulation", uVal);
  std::cout << "NCP_modulation = " << uVal.Get () << std::endl;
  std::cout << std::endl;

/*
  docsis.GetDownstream (linkDocsis)->GetAttribute ("", uVal);
  std::cout << "ProfileA_modulation = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("", uVal);
  std::cout << "Num_DS_profiles = " << uVal.Get () << std::endl;
  std::cout << std::endl;
*/
  std::cout << "System configuration & other assumptions" <<std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("NumUsChannels", uVal);
  std::cout << "Num_US_channels = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("AverageUsBurst", uVal);
  std::cout << "Average_US_burst = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("AverageUsUtilization", dVal);
  std::cout << "Average_US_utilization = " << dVal.Get () << std::endl;
  std::cout << std::endl;

  std::cout << "HFC plant" <<std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("MaximumDistance", dVal);
  std::cout << "Maximum_distance = " << dVal.Get () << std::endl;
  std::cout << std::endl;

  std::cout << "=================" <<std::endl;
  std::cout << "Calculated values" <<std::endl;
  std::cout << "=================" <<std::endl;
  std::cout << std::endl;

  std::cout << "calculated DOCSIS MAC params" <<std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("ScPerMinislot", uVal);
  std::cout << "SC_per_minislot = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("FrameDuration", tVal);
  std::cout << "Frame_Duration = " << tVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("MinislotsPerFrame", uVal);
  std::cout << "minislots_per_frame = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("MinislotCapacity", uVal);
  std::cout << "MinislotCapacity = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("UsCapacity", dVal);
  std::cout << "US_capacity = " << dVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("CmMapProcTime", tVal);
  std::cout << "CM_MAP_Proc_Time = " << tVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsSymbolTime", tVal);
  std::cout << "DS_Symbol_time = " << tVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsIntlvDelay", tVal);
  std::cout << "DS_intlv_delay = " << tVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("Rtt", tVal);
  std::cout << "RTT = " << tVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("MinReqGntDelay", uVal);
  std::cout << "Min_REQ_GNT_delay = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("FramesPerMap", uVal);
  std::cout << "frames_per_MAP = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("MinislotsPerMap", uVal);
  std::cout << "minislots_per_MAP = " << uVal.Get () << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("ActualMapInterval", tVal);
  std::cout << "actual_MAP_interval = " << tVal.Get () << std::endl;
  std::cout << std::endl;


  std::cout << "MAP message downstream overhead" <<std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("UsGrantsPerSecond", dVal);
  std::cout << "US_grants_per_second = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("AvgIesPerMap", dVal);
  std::cout << "Ave_IEs_per_MAP = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("AvgMapSize", dVal);
  std::cout << "Ave_MAP_Size = " << dVal.Get () << std::endl;
  // docsis.GetDownstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_MDD_Size = " << uVal.Get () << std::endl;
  // docsis.GetDownstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_UCD_Size = " << uVal.Get () << std::endl;
  // docsis.GetDownstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_DPD_Size = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("AvgMapDatarate", dVal);
  std::cout << "Ave_MAP_datarate = " << dVal.Get () << std::endl;
  // docsis.GetUpstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_MDD_datarate = " << uVal.Get () << std::endl;
  // docsis.GetUpstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_UCD_datarate = " << uVal.Get () << std::endl;
  // docsis.GetUpstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_DPD_datarate = " << uVal.Get () << std::endl;
  // docsis.GetUpstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_ProfA_datarate  = " << uVal.Get () << std::endl;
  // docsis.GetUpstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_ProfA_CW_rate = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("ScPerNcp", dVal);
  std::cout << "SCs_per_NCP = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("ScPerCw", dVal);
  std::cout << "SCs_per_CW = " << dVal.Get () << std::endl;
  // docsis.GetUpstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_ProfA_SC_rate = " << uVal.Get () << std::endl;
  // docsis.GetUpstream (linkDocsis)->GetAttribute ("", uVal);
  // std::cout << "Ave_ProfA_SCs_per_symbol = " << uVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsCodewordsPerSymbol", dVal);
  std::cout << "DS_codewords_per_symbol = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsSymbolCapacity", dVal);
  std::cout << "DS_symbol_capacity = " << dVal.Get () << std::endl;
  docsis.GetDownstream (linkDocsis)->GetAttribute ("DsCapacity", dVal);
  std::cout << "DS_capacity = " << dVal.Get () << std::endl;
  std::cout << std::endl;



  std::cout << "================" <<std::endl;
  std::cout << "Simulation Stats" <<std::endl;
  std::cout << "================" <<std::endl;

  docsis.GetUpstream (linkDocsis)->GetAttribute ("MinislotsPerMap", uVal);
  uint32_t minislotsPerMap = uVal.Get ();
  docsis.GetUpstream (linkDocsis)->GetAttribute ("ActualMapInterval", tVal);
  Time actualMapInterval = tVal.Get ();
  std::cout << "Simulation duration = " << Simulator::Now ().As (Time::S) << std::endl;
  double simulationMinislots = (minislotsPerMap / actualMapInterval.GetSeconds ()) * simulationEndTime.GetSeconds ();
  std::cout << "Simulation minislots = " << simulationMinislots << std::endl;
  docsis.GetUpstream (linkDocsis)->GetAttribute ("MinislotCapacity", uVal);
  std::cout << "Minislots granted = " << (g_grantsCounter / uVal.Get ()) << std::endl;
  std::cout << "Simulation minislots/minislots granted (est. number of users) = " << simulationMinislots / (g_grantsCounter / uVal.Get ()) << std::endl;
  std::cout << "Bytes granted = " << g_grantsCounter << std::endl;
  std::cout << "Bytes sent = " << g_grantsUsedCounter << std::endl;
  std::cout << "Bytes sent/granted (efficiency) = " << static_cast<double> (g_grantsUsedCounter)/g_grantsCounter << std::endl;

  uint32_t wastedBytes = g_grantsCounter - g_grantsUsedCounter;
  if (!fileNameSummary.empty ())
    {
      g_fileSummary.open (fileNameSummary.c_str (), std::ofstream::out);

      g_fileSummary << std::setprecision (3) << 
                   simulationEndTime.GetSeconds () << " " << 
                   (g_grantsCounter * 8.0 / simulationEndTime.GetSeconds ())/1e6 << " " <<
                   (g_grantsUsedCounter * 8.0 / simulationEndTime.GetSeconds ())/1e6 << " " <<
                   (wastedBytes * 8.0 / simulationEndTime.GetSeconds ())/1e6 << " " <<
                   (g_grantsUsedCounter / double (g_grantsCounter));
      g_fileSummary.close ();
    }

  Simulator::Destroy ();
 
  // Close all open file descriptors
  if (g_fileCm.is_open ()) g_fileCm.close ();
  if (g_fileCmts.is_open ()) g_fileCmts.close ();
  if (g_fileCmDrop.is_open ()) g_fileCmDrop.close ();
  if (g_fileCmtsDrop.is_open ()) g_fileCmtsDrop.close ();
  if (g_fileTcpFileCompletion.is_open ()) g_fileTcpFileCompletion.close ();
  if (g_fileDctcpFileCompletion.is_open ()) g_fileDctcpFileCompletion.close ();
  if (g_fileGrantsUnused.is_open ()) g_fileGrantsUnused.close ();
  if (g_fileUnusedBandwidth.is_open ()) g_fileUnusedBandwidth.close ();
  if (g_fileCGrantState.is_open ()) g_fileCGrantState.close ();
  if (g_fileLGrantState.is_open ()) g_fileLGrantState.close ();
  if (g_fileTcpPacketTrace.is_open ()) g_fileTcpPacketTrace.close ();
  if (g_filePageLoadTime.is_open ()) g_filePageLoadTime.close ();
  if (g_fileCQueueUpstreamBytes.is_open ()) g_fileCQueueUpstreamBytes.close ();
  if (g_fileCQueueDownstreamBytes.is_open ()) g_fileCQueueDownstreamBytes.close ();
  if (g_fileLQueueDownstreamBytes.is_open ()) g_fileLQueueUpstreamBytes.close ();
  if (g_fileCQueueDropProbability.is_open ()) g_fileCQueueDropProbability.close ();
  if (g_fileLQueueMarkProbability.is_open ()) g_fileLQueueMarkProbability.close ();
  if (g_fileDualQLlSojourn.is_open ()) g_fileDualQLlSojourn.close ();
  if (g_fileDualQClassicSojourn.is_open ()) g_fileDualQClassicSojourn.close ();
  if (g_fileCalculatePState.is_open ()) g_fileCalculatePState.close ();
  if (g_fileCongestionBytes.is_open ()) g_fileCongestionBytes.close ();
  if (g_fileLQueueDownstreamBytes.is_open ()) g_fileLQueueDownstreamBytes.close ();
}
