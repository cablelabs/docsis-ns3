/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Tom Henderson <tomh@tomh.org> (Extensions by CableLabs LLD project)
 */

#include <algorithm>
#include <iomanip>
#include "ns3/abort.h"
#include "ns3/application-container.h"
#include "ns3/arp-cache.h"
#include "ns3/bridge-helper.h"
#include "ns3/cm-net-device.h"
#include "ns3/cmts-net-device.h"
#include "ns3/cmts-upstream-scheduler.h"
#include "ns3/config.h"
#include "ns3/csma-helper.h"
#include "ns3/csma-net-device.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/docsis-channel.h"
#include "ns3/docsis-configuration.h"
#include "ns3/docsis-l4s-packet-filter.h"
#include "ns3/docsis-net-device.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/fifo-queue-disc.h"
#include "ns3/file-transfer-application.h"
#include "ns3/file-transfer-helper.h"
#include "ns3/fq-codel-queue-disc.h"
#include "ns3/game-client.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ipv4-interface.h"
#include "ns3/log.h"
#include "ns3/names.h"
#include "ns3/onoff-application.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/prio-queue-disc.h"
#include "ns3/queue-disc-container.h"
#include "ns3/queue-protection.h"
#include "ns3/queue-size.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/trace-helper.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/udp-client.h"
#include "ns3/udp-server.h"
#include "docsis-helper.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DocsisHelper");

namespace docsis {

DocsisHelper::DocsisHelper ()
{
  m_upstreamDeviceFactory.SetTypeId ("ns3::docsis::CmNetDevice");
  m_downstreamDeviceFactory.SetTypeId ("ns3::docsis::CmtsNetDevice");
  m_channelFactory.SetTypeId ("ns3::docsis::DocsisChannel");
}
void 
DocsisHelper::SetCmAttribute (std::string n1, const AttributeValue &v1)
{
  m_upstreamDeviceFactory.Set (n1, v1);
}

void 
DocsisHelper::SetCmtsAttribute (std::string n1, const AttributeValue &v1)
{
  m_downstreamDeviceFactory.Set (n1, v1);
}

void 
DocsisHelper::SetChannelAttribute (std::string n1, const AttributeValue &v1)
{
  m_channelFactory.Set (n1, v1);
}

void 
DocsisHelper::EnablePcapInternal (std::string prefix, Ptr<NetDevice> nd, bool promiscuous, bool explicitFilename)
{
  NS_LOG_FUNCTION (this << prefix << nd << promiscuous << explicitFilename);
  //
  // All of the Pcap enable functions vector through here including the ones
  // that are wandering through all of devices on perhaps all of the nodes in
  // the system.  We can only deal with devices of type DocsisNetDevice.
  //
  Ptr<DocsisNetDevice> device = nd->GetObject<DocsisNetDevice> ();
  if (device == 0)
    {
      NS_LOG_INFO ("DocsisHelper::EnablePcapInternal(): Device " << device << " not of type ns3::docsis::DocsisNetDevice");
      return;
    }

  PcapHelper pcapHelper;

  std::string filename;
  if (explicitFilename)
    {
      filename = prefix;
    }
  else
    {
      filename = pcapHelper.GetFilenameFromDevice (prefix, device);
    }

  Ptr<PcapFileWrapper> file = pcapHelper.CreateFile (filename, std::ios::out, 
                                                     PcapHelper::DLT_EN10MB);
  pcapHelper.HookDefaultSink<DocsisNetDevice> (device, "PromiscSniffer", file);
}

void 
DocsisHelper::EnableAsciiInternal (
  Ptr<OutputStreamWrapper> stream, 
  std::string prefix, 
  Ptr<NetDevice> nd,
  bool explicitFilename)
{
  NS_LOG_FUNCTION (this << stream << prefix << nd << explicitFilename);
  //
  // All of the ascii enable functions vector through here including the ones
  // that are wandering through all of devices on perhaps all of the nodes in
  // the system.  We can only deal with devices of type DocsisNetDevice.
  //
  Ptr<DocsisNetDevice> device = nd->GetObject<DocsisNetDevice> ();
  if (device == 0)
    {
      NS_LOG_INFO ("DocsisHelper::EnableAsciiInternal(): Device " << device << 
                   " not of type ns3::docsis::DocsisNetDevice");
      return;
    }

  //
  // Our default trace sinks are going to use packet printing, so we have to 
  // make sure that is turned on.
  //
  Packet::EnablePrinting ();

  //
  // If we are not provided an OutputStreamWrapper, we are expected to create 
  // one using the usual trace filename conventions and do a Hook*WithoutContext
  // since there will be one file per context and therefore the context would
  // be redundant.
  //
  if (stream == 0)
    {
      //
      // Set up an output stream object to deal with private ofstream copy 
      // constructor and lifetime issues.  Let the helper decide the actual
      // name of the file given the prefix.
      //
      AsciiTraceHelper asciiTraceHelper;

      std::string filename;
      if (explicitFilename)
        {
          filename = prefix;
        }
      else
        {
          filename = asciiTraceHelper.GetFilenameFromDevice (prefix, device);
        }

      Ptr<OutputStreamWrapper> theStream = asciiTraceHelper.CreateFileStream (filename);

      //
      // The MacRx trace source provides our "r" event.
      //
      asciiTraceHelper.HookDefaultReceiveSinkWithoutContext<DocsisNetDevice> (device, "MacRx", theStream);

      asciiTraceHelper.HookDefaultEnqueueSinkWithoutContext<DocsisNetDevice> (device, "MacTx", theStream);
      asciiTraceHelper.HookDefaultDequeueSinkWithoutContext<DocsisNetDevice> (device, "PhyTxBegin", theStream);
      asciiTraceHelper.HookDefaultDropSinkWithoutContext<DocsisNetDevice> (device, "MacTxDrop", theStream);

      // PhyRxDrop trace source for "d" event
      asciiTraceHelper.HookDefaultDropSinkWithoutContext<DocsisNetDevice> (device, "PhyRxDrop", theStream);

      return;
    }

  //
  // If we are provided an OutputStreamWrapper, we are expected to use it, and
  // to providd a context.  We are free to come up with our own context if we
  // want, and use the AsciiTraceHelper Hook*WithContext functions, but for 
  // compatibility and simplicity, we just use Config::Connect and let it deal
  // with the context.
  //
  // Note that we are going to use the default trace sinks provided by the 
  // ascii trace helper.  There is actually no AsciiTraceHelper in sight here,
  // but the default trace sinks are actually publicly available static 
  // functions that are always there waiting for just such a case.
  //
  uint32_t nodeid = nd->GetNode ()->GetId ();
  uint32_t deviceid = nd->GetIfIndex ();
  std::ostringstream oss;

  oss << "/NodeList/" << nd->GetNode ()->GetId () << "/DeviceList/" << deviceid << "/$ns3::docsis::DocsisNetDevice/MacRx";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultReceiveSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::docsis::DocsisNetDevice/TxQueue/Enqueue";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultEnqueueSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::docsis::DocsisNetDevice/TxQueue/Dequeue";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultDequeueSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::docsis::DocsisNetDevice/TxQueue/Drop";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultDropSinkWithContext, stream));

  oss.str ("");
  oss << "/NodeList/" << nodeid << "/DeviceList/" << deviceid << "/$ns3::docsis::DocsisNetDevice/PhyRxDrop";
  Config::Connect (oss.str (), MakeBoundCallback (&AsciiTraceHelper::DefaultDropSinkWithContext, stream));
}

NetDeviceContainer 
DocsisHelper::Install (Ptr<Node> cmts, Ptr<Node> cm) 
{
  NS_LOG_FUNCTION (this << cmts << cm);
  NetDeviceContainer container;

  Ptr<CmtsNetDevice> devA = m_downstreamDeviceFactory.Create<CmtsNetDevice> ();
  devA->SetAddress (Mac48Address::Allocate ());
  cmts->AddDevice (devA);
  Ptr<DualQueueCoupledAqm> dual = CreateObject<DualQueueCoupledAqm> ();
  dual->AddPacketFilter (CreateObject<DocsisLowLatencyPacketFilter> ());
  devA->SetQueue (dual);
  dual->SetQDelaySingleCallback (MakeCallback (&DocsisNetDevice::ExpectedDelay, devA));
  Ptr<QueueProtection> queueProtection = CreateObject<QueueProtection> ();
  queueProtection->SetHashCallback (MakeCallback(&DocsisLowLatencyPacketFilter::GenerateHash32));
  queueProtection->SetQueue (dual);
  dual->SetQueueProtection (queueProtection);

  Ptr<CmNetDevice> devB = m_upstreamDeviceFactory.Create<CmNetDevice> ();
  devB->SetAddress (Mac48Address::Allocate ());
  cm->AddDevice (devB);
  dual = CreateObject<DualQueueCoupledAqm> ();
  dual->AddPacketFilter (CreateObject<DocsisLowLatencyPacketFilter> ());
  devB->SetQueue (dual);
  dual->SetQDelaySingleCallback (MakeCallback (&DocsisNetDevice::ExpectedDelay, devB));
  queueProtection = CreateObject<QueueProtection> ();
  queueProtection->SetHashCallback (MakeCallback(&DocsisLowLatencyPacketFilter::GenerateHash32));
  queueProtection->SetQueue (dual);
  dual->SetQueueProtection (queueProtection);

  Ptr<CmtsUpstreamScheduler> scheduler = CreateObject<CmtsUpstreamScheduler> ();
  scheduler->SetUpstream (DynamicCast<CmNetDevice> (devB));
  devB->SetCmtsUpstreamScheduler (scheduler);
  bool connected = devB->TraceConnectWithoutContext ("LowLatencyGrantState", MakeCallback (&CmtsUpstreamScheduler::ReceiveUnusedLGrantUpdate, scheduler));
  NS_ASSERT_MSG (connected, "Couldn't hook trace source");
  connected = devB->TraceConnectWithoutContext ("ClassicGrantState", MakeCallback (&CmtsUpstreamScheduler::ReceiveUnusedCGrantUpdate, scheduler));
  NS_ASSERT_MSG (connected, "Couldn't hook trace source");
  NS_UNUSED (connected);

  Ptr<DocsisChannel> channel = m_channelFactory.Create<DocsisChannel> ();

  devA->Attach (channel);
  devB->Attach (channel);
  container.Add (devA);
  container.Add (devB);

  return container;
}

NetDeviceContainer 
DocsisHelper::Install (Ptr<Node> cmts, Ptr<Node> cm, Ptr<AggregateServiceFlow> upstreamAsf, Ptr<AggregateServiceFlow> downstreamAsf) 
{
  NS_LOG_FUNCTION (this << cmts << cm << upstreamAsf << downstreamAsf);
  NetDeviceContainer container = Install (cmts, cm);
  Ptr<CmtsNetDevice> cmtsDevice = container.Get (0)->GetObject<CmtsNetDevice> ();
  cmtsDevice->SetDownstreamAsf (downstreamAsf);
  Ptr<CmNetDevice> cmDevice = container.Get (1)->GetObject<CmNetDevice> ();
  cmDevice->SetUpstreamAsf (upstreamAsf);
  return container;
}

NetDeviceContainer 
DocsisHelper::Install (Ptr<Node> cmts, Ptr<Node> cm, Ptr<AggregateServiceFlow> upstreamAsf, Ptr<ServiceFlow> downstreamSf) 
{
  NS_LOG_FUNCTION (this << cmts << cm << upstreamAsf << downstreamSf);
  NetDeviceContainer container = Install (cmts, cm);
  Ptr<CmtsNetDevice> cmtsDevice = container.Get (0)->GetObject<CmtsNetDevice> ();
  cmtsDevice->SetDownstreamSf (downstreamSf);
  Ptr<CmNetDevice> cmDevice = container.Get (1)->GetObject<CmNetDevice> ();
  cmDevice->SetUpstreamAsf (upstreamAsf);
  return container;
}

NetDeviceContainer 
DocsisHelper::Install (Ptr<Node> cmts, Ptr<Node> cm, Ptr<ServiceFlow> upstreamSf, Ptr<AggregateServiceFlow> downstreamAsf) 
{
  NS_LOG_FUNCTION (this << cmts << cm << upstreamSf << downstreamAsf);
  NetDeviceContainer container = Install (cmts, cm);
  Ptr<CmtsNetDevice> cmtsDevice = container.Get (0)->GetObject<CmtsNetDevice> ();
  cmtsDevice->SetDownstreamAsf (downstreamAsf);
  Ptr<CmNetDevice> cmDevice = container.Get (1)->GetObject<CmNetDevice> ();
  cmDevice->SetUpstreamSf (upstreamSf);
  return container;
}

NetDeviceContainer 
DocsisHelper::Install (Ptr<Node> cmts, Ptr<Node> cm, Ptr<ServiceFlow> upstreamSf, Ptr<ServiceFlow> downstreamSf) 
{
  NS_LOG_FUNCTION (this << cmts << cm << upstreamSf << downstreamSf);
  NetDeviceContainer container = Install (cmts, cm);
  Ptr<CmtsNetDevice> cmtsDevice = container.Get (0)->GetObject<CmtsNetDevice> ();
  cmtsDevice->SetDownstreamSf (downstreamSf);
  Ptr<CmNetDevice> cmDevice = container.Get (1)->GetObject<CmNetDevice> ();
  cmDevice->SetUpstreamSf (upstreamSf);
  return container;
}

Ptr<CmNetDevice> 
DocsisHelper::GetUpstream (const NetDeviceContainer& device) const
{
  Ptr<CmNetDevice> p = device.Get (1)->GetObject<CmNetDevice> ();
  NS_ABORT_MSG_UNLESS (p, "No upstream device found");
  return p;
}

Ptr<CmtsNetDevice> 
DocsisHelper::GetDownstream (const NetDeviceContainer& device) const
{
  Ptr<CmtsNetDevice> p = device.Get (0)->GetObject<CmtsNetDevice> ();
  NS_ABORT_MSG_UNLESS (p, "No downstream device found");
  return p;
}

Ptr<DocsisChannel> 
DocsisHelper::GetChannel (const NetDeviceContainer& device) const
{
  Ptr<Channel> ch = device.Get (1)->GetChannel ();
  Ptr<DocsisChannel> p = ch->GetObject<DocsisChannel> ();
  NS_ASSERT_MSG (p, "GetObject () failed");
  return p;
}

int64_t
DocsisHelper::AssignStreams (const NetDeviceContainer& docsisDevices, int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  Ptr<CmtsNetDevice> cmts = GetDownstream (docsisDevices);
  Ptr<CmNetDevice> cm = GetUpstream (docsisDevices);
  Ptr<DualQueueCoupledAqm> cmDualQueue = cm->GetQueue ();
  Ptr<DualQueueCoupledAqm> cmtsDualQueue = cmts->GetQueue ();
  Ptr<CmtsUpstreamScheduler> cmtsScheduler = cm->GetCmtsUpstreamScheduler ();
  NS_ABORT_MSG_UNLESS (cmDualQueue && cmtsDualQueue && cmtsScheduler, "Error: Devices not fully instantiated");
  int64_t currentStream = stream;
  currentStream += cmts->AssignStreams (currentStream);
  currentStream += cm->AssignStreams (currentStream);
  currentStream += cmDualQueue->AssignStreams (currentStream);
  currentStream += cmtsDualQueue->AssignStreams (currentStream);
  currentStream += cmtsScheduler->AssignStreams (currentStream);
  return (currentStream - stream);
}


Ptr<GameClient>
DocsisHelper::AddUpstreamGameSession (Ptr<Node> client, Ptr<Node> server,
  uint16_t serverPort, Time startTime, Time stopTime,
  Ipv4Header::DscpType dscpValue) const
{
  // Upstream model is mean of 128 bytes (Ethernet) and std. dev. of 20 bytes
  // so back off the mean application size, due to headers and trailers,
  // to (128 - 14 - 4 - 20 - 8 = ) 82.  
  // Interarrival time should have a mean of 33 ms and std. dev. of 3 ms.
  return AddGameSession (client, server, serverPort, 
    MilliSeconds (33), MilliSeconds (0), 82, 20, startTime, stopTime, 
    dscpValue);
}

Ptr<GameClient>
DocsisHelper::AddDownstreamGameSession (Ptr<Node> client, Ptr<Node> server,
  uint16_t serverPort, Time startTime, Time stopTime,
  Ipv4Header::DscpType dscpValue) const
{
  // Downstream model is mean of 450 bytes (Ethernet) and std. dev. of 120 bytes
  // so back off the mean application size, due to headers and trailers,
  // to (450 - 14 - 4 - 20 - 8 = ) 404.  
  // Interarrival time should have a mean of 33 ms and std. dev. of 5 ms.
  return AddGameSession (client, server, serverPort, 
    MilliSeconds (33), MilliSeconds (5), 404, 120, startTime, stopTime, 
    dscpValue);
}

Ptr<GameClient>
DocsisHelper::AddGameSession (Ptr<Node> client, Ptr<Node> server,
  uint16_t serverPort, Time iaTime,
  Time iaStdDev, uint32_t pktSize, uint32_t pktStdDev, Time startTime,
  Time stopTime, Ipv4Header::DscpType dscpValue) const
{
  NS_LOG_FUNCTION (client << server << serverPort 
    << iaTime.GetSeconds () << iaStdDev.GetSeconds () << pktSize << pktStdDev
    << startTime.GetSeconds () << stopTime.GetSeconds () << dscpValue);

  Ipv4Address serverAddr = GetEndpointIpv4Address (server);
  // DSCP configuration
  InetSocketAddress serverSocketAddr (serverAddr, serverPort);
  uint16_t tos = dscpValue << 2;
  serverSocketAddr.SetTos (tos);
  
  Ptr<GameClient> gameClient = CreateObject<GameClient> ();
  gameClient->SetRemote (serverSocketAddr);
  gameClient->SetAttribute ("InterarrivalTime", TimeValue (iaTime));
  gameClient->SetAttribute ("InterarrivalDeviation", TimeValue (iaStdDev));
  gameClient->SetAttribute ("Size", UintegerValue (pktSize));
  gameClient->SetAttribute ("SizeDeviation", UintegerValue (pktStdDev));
  gameClient->SetStartTime (startTime);
  gameClient->SetStopTime (stopTime);
  client->AddApplication (gameClient);

  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (InetSocketAddress (Ipv4Address::GetAny (), serverPort)));
  ApplicationContainer sinkContainer = sinkHelper.Install (server);
  sinkContainer.Start (startTime);
  sinkContainer.Stop (stopTime);
  
  return gameClient;
}

void
DocsisHelper::AddDashSession (Ptr<Node> client, Ptr<Node> server,
  uint16_t serverPort, Time startTime, Time stopTime,
  std::string tcpSocketFactoryType, uint32_t maxDashBytes) const
{
  NS_LOG_FUNCTION (client << server << serverPort
    << startTime.GetSeconds () << startTime.GetSeconds ()
    << stopTime.GetSeconds () << tcpSocketFactoryType);

  Ipv4Address serverAddr = GetEndpointIpv4Address (server);
  FileTransferHelper sourceDash (tcpSocketFactoryType, InetSocketAddress (serverAddr, serverPort));
  sourceDash.SetAttribute ("FileSize", UintegerValue (maxDashBytes));
  // Use reading time (FTP model 2).  The DASH model here allows for
  // a connection 'on time' of 2.5 seconds followed by a 'reading time' of
  // 2.485 seconds, with the goal of having DASH obtain about 6 Mb/s
  // throughput but with variability (rate adaptation).  The configured
  // RTT and competing connections will cause this throughput to vary.
  // 2.485 seconds read time allows 15 ms for the connection to close
  sourceDash.SetAttribute ("UseReadingTime", BooleanValue (true));
  sourceDash.SetAttribute ("ReadingTime", StringValue ("ns3::ConstantRandomVariable[Constant=2.485]"));
  sourceDash.SetAttribute ("FileTransferDuration", TimeValue (Seconds (2.5)));
  // TCP configuration -- limit the send socket buffer size so that file transfers
  // that are closed at a specified duration time have less unsent data
  sourceDash.SetAttribute ("SndBufSize", UintegerValue (32000));
  ApplicationContainer sourceDashApps = sourceDash.Install (client);
  sourceDashApps.Start (startTime);
  sourceDashApps.Stop (stopTime);

  PacketSinkHelper sinkDash (tcpSocketFactoryType, InetSocketAddress (Ipv4Address::GetAny (), serverPort));
  ApplicationContainer sinkDashApps = sinkDash.Install (server);
  sinkDashApps.Start (startTime);
  sinkDashApps.Stop (stopTime);
}

Ptr<FileTransferApplication>
DocsisHelper::AddFtpSession (Ptr<Node> client, Ptr<Node> server,
  uint16_t serverPort, Time startTime, Time stopTime,
  std::string tcpSocketFactoryType, std::string fileModel) const
{
  NS_LOG_FUNCTION (client << server << serverPort
    << startTime.GetSeconds () << startTime.GetSeconds ()
    << stopTime.GetSeconds () << tcpSocketFactoryType << fileModel);

  Ipv4Address serverAddr = GetEndpointIpv4Address (server);
  // Create File transfer application on Server
  InetSocketAddress destFtpAddress (serverAddr, serverPort);
  FileTransferHelper sourceFtp (tcpSocketFactoryType, destFtpAddress);
  if (fileModel == "unlimited")
    {
      sourceFtp.SetAttribute ("UseFileTransferDuration", BooleanValue (false));
      sourceFtp.SetAttribute ("UseRandomFileSize", BooleanValue (false));
      sourceFtp.SetAttribute ("FileSize", UintegerValue (0));
    }
  else if (fileModel == "empirical")
    {
      sourceFtp.SetAttribute ("UseFileTransferDuration", BooleanValue (false));
      sourceFtp.SetAttribute ("UseRandomFileSize", BooleanValue (true));
      sourceFtp.SetAttribute ("RandomFileSize", StringValue ("ns3::LogNormalRandomVariable[Mu=14.8|Sigma=2.0]"));
    }
  else if (fileModel == "speedtest")
    {
      sourceFtp.SetAttribute ("UseFileTransferDuration", BooleanValue (true));
      sourceFtp.SetAttribute ("FileTransferDuration", TimeValue (Seconds (15)));
      sourceFtp.SetAttribute ("UseRandomFileSize", BooleanValue (false));
      sourceFtp.SetAttribute ("FileSize", UintegerValue (0));
    }
  else
    {
      NS_FATAL_ERROR ("Unknown model " << fileModel);
    }
  // Use reading time (FTP model 2) with 100ms reading time
  sourceFtp.SetAttribute ("UseReadingTime", BooleanValue (true));
  sourceFtp.SetAttribute ("ReadingTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.1]"));
  ApplicationContainer sourceFtpApp = sourceFtp.Install (client);
  sourceFtpApp.Start (startTime);
  sourceFtpApp.Stop (stopTime);
  //
  // Create a PacketSinkApplication for tcpClient0 or tcpClient1
  //
  PacketSinkHelper sinkFtp (tcpSocketFactoryType, InetSocketAddress (Ipv4Address::GetAny (), serverPort));
  ApplicationContainer sinkFtpApp = sinkFtp.Install (server);
  sinkFtpApp.Start (startTime);
  sinkFtpApp.Stop (stopTime);

  return (DynamicCast<FileTransferApplication> (sourceFtpApp.Get (0)));
}

// This helper method relies on the default ns-3 practice of installing
// as a first interface a loopback interface, and the second interface
// corresponding to the LAN interface.
Ipv4Address
DocsisHelper::GetEndpointIpv4Address (Ptr<Node> node) const
{
  NS_LOG_FUNCTION (this << node);
  Ptr<Ipv4L3Protocol> ipv4 = node->GetObject<Ipv4L3Protocol> ();
  NS_ABORT_MSG_UNLESS (ipv4, "Ipv4L3Protocol not found");
  NS_ABORT_MSG_UNLESS (ipv4->GetNInterfaces () == 2, "Unexpected number of interfaces: " << ipv4->GetNInterfaces ());
  Ipv4Address addr; 
  addr = ipv4->GetAddress (0, 0).GetLocal ();
  NS_ABORT_MSG_UNLESS (addr == Ipv4Address::GetLoopback (), "Loopback address not found on first interface");
  addr = ipv4->GetAddress (1, 0).GetLocal ();
  NS_LOG_DEBUG ("Returning IPv4 address: " << addr);
  return addr;
}

std::pair<Ptr<UdpClient>, Ptr<UdpServer> >
DocsisHelper::AddPacketBurstDuration (Ptr<Node> client, Ptr<Node> server, uint16_t serverPort, uint16_t clientPort, DataRate ethernetRate, uint16_t frameSize, Time startTime, Time duration, Ipv4Header::DscpType dscpValue, Ipv4Header::EcnType ecnValue) const
{
  NS_LOG_FUNCTION (this << client << server << serverPort << clientPort << ethernetRate << frameSize << startTime << duration << dscpValue << ecnValue);
  Time interval = Seconds (frameSize * 8.0 / ethernetRate.GetBitRate ());
  // Convert duration into a frame count
  uint32_t frameCount = static_cast<uint32_t> (duration.GetSeconds () / interval.GetSeconds ());
  return AddPacketBurstCount (client, server, serverPort, clientPort, ethernetRate, frameSize, startTime, frameCount, dscpValue, ecnValue);
}

std::pair<Ptr<UdpClient>, Ptr<UdpServer> >
DocsisHelper::AddPacketBurstCount (Ptr<Node> client, Ptr<Node> server, uint16_t serverPort, uint16_t clientPort, DataRate ethernetRate, uint16_t frameSize, Time startTime, uint32_t frameCount, Ipv4Header::DscpType dscpValue, Ipv4Header::EcnType ecnValue) const
{
  NS_LOG_FUNCTION (this << client << server << serverPort << ethernetRate << frameSize << startTime << frameCount << dscpValue << ecnValue);
  NS_ABORT_MSG_UNLESS (frameSize >= 64, "Frame size too small: " << frameSize);
  Ipv4Address serverAddr = GetEndpointIpv4Address (server);
//m_serverIpAddresses.find (server)->second;
  Time interval = Seconds (frameSize * 8.0 / ethernetRate.GetBitRate ());
  uint16_t udpSize = frameSize - 18 - 20 - 8;
  NS_LOG_DEBUG ("Send UDP payloads size " << udpSize << " at interval " << interval.GetSeconds ());
  Ptr<UdpClient> udpClient = CreateObject<UdpClient> ();
  udpClient->SetAttribute ("Interval", TimeValue (interval));
  udpClient->SetAttribute ("PacketSize", UintegerValue (udpSize));
  InetSocketAddress serverSocketAddr (serverAddr, serverPort);
  uint16_t tos = (dscpValue << 2) | ecnValue;
  serverSocketAddr.SetTos (tos);
  udpClient->SetAttribute ("RemoteAddress", AddressValue (serverSocketAddr));
  udpClient->SetAttribute ("MaxPackets", UintegerValue (frameCount));
  udpClient->SetAttribute ("LocalPort", UintegerValue (clientPort));
  udpClient->SetStartTime (startTime);
  udpClient->SetStopTime (startTime + frameCount * interval);
  client->AddApplication (udpClient);

  Ptr<UdpServer> udpServer = CreateObject<UdpServer> ();
  udpServer->SetAttribute ("Port", UintegerValue (serverPort));
  udpServer->SetStartTime (startTime);
  udpServer->SetStopTime (startTime + frameCount * interval + Seconds (1));
  server->AddApplication (udpServer);

  auto apps = std::make_pair (udpClient, udpServer);
  return apps;
}

Ptr<OnOffApplication>
DocsisHelper::AddDutyCycleUdpStream (Ptr<Node> client, Ptr<Node> server,
  uint16_t serverPort, uint16_t clientPort, DataRate rate, uint16_t packetSize,
  Time onTime, Time offTime, Time startTime, Time stopTime, Ipv4Header::DscpType dscpValue, Ipv4Header::EcnType ecnValue) const
{
  NS_LOG_FUNCTION (client << server << serverPort << clientPort << rate
    << packetSize << onTime << offTime << startTime << stopTime);
  NS_ABORT_MSG_IF (packetSize < 4 || packetSize > 1472, "Unsupported pkt size");
  Ipv4Address serverAddr = GetEndpointIpv4Address (server);

  // DSCP configuration
  InetSocketAddress serverSocketAddr (serverAddr, serverPort);
  uint16_t tos = (dscpValue << 2) | ecnValue;
  serverSocketAddr.SetTos (tos);

  InetSocketAddress clientSocketAddr (Ipv4Address::GetAny (), clientPort);
  
  // Back off the frame size due to headers and trailers.
  OnOffHelper onOffHelper ("ns3::UdpSocketFactory", serverSocketAddr);
  onOffHelper.SetAttribute ("PacketSize", UintegerValue (packetSize));
  onOffHelper.SetAttribute ("DataRate", DataRateValue (rate));
  onOffHelper.SetAttribute ("Local", AddressValue (clientSocketAddr));
  std::ostringstream oss;
  oss << "ns3::ConstantRandomVariable[Constant=" << onTime.GetSeconds () << "]";
  onOffHelper.SetAttribute ("OnTime", StringValue (oss.str ()));
  std::ostringstream oss2;
  oss2 << "ns3::ConstantRandomVariable[Constant=" << std::fixed
       << std::setprecision (9) << offTime.GetSeconds () << "]";
  onOffHelper.SetAttribute ("OffTime", StringValue (oss2.str ()));
  NS_LOG_DEBUG ("On time " << oss.str () << " off time " << oss2.str ());

  ApplicationContainer onOffContainer = onOffHelper.Install (client);
  // Start the application
  onOffContainer.Start (startTime);
  onOffContainer.Stop (stopTime);
  Ptr<OnOffApplication> onOffApplication = onOffContainer.Get (0)->GetObject <OnOffApplication> ();

  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (InetSocketAddress (Ipv4Address::GetAny (), serverPort)));
  ApplicationContainer sinkContainer = sinkHelper.Install (server);
  sinkContainer.Start (startTime);
  // Add 100 ms to the sink stop time to avoid ICMP port unreachable messages
  sinkContainer.Stop (stopTime + MilliSeconds (100));

  return onOffApplication;
}

Ptr<FileTransferApplication>
DocsisHelper::AddFileTransfer (Ptr<Node> client, Ptr<Node> server,
  uint16_t serverPort, Time startTime,
  Time stopTime, uint32_t fileSize, bool useReadingTime,
  Ipv4Header::DscpType dscpValue) const
{
  NS_LOG_FUNCTION (client << server << serverPort << startTime << stopTime << fileSize << useReadingTime << (uint16_t) dscpValue);
  // DSCP configuration
  Ipv4Address serverAddr = GetEndpointIpv4Address (server);
  InetSocketAddress serverSocketAddr (serverAddr, serverPort);
  uint16_t tos = dscpValue << 2;
  serverSocketAddr.SetTos (tos);
  FileTransferHelper source ("ns3::TcpSocketFactory", serverSocketAddr);
  // A conventional file transfer, fixed size, not time limited
  source.SetAttribute ("FileSize", UintegerValue (fileSize));
  source.SetAttribute ("UseFileTransferDuration", BooleanValue (false));
  if (useReadingTime)
    {
      // Make the transfer recurring
      source.SetAttribute ("UseReadingTime", BooleanValue (true));
      source.SetAttribute ("ReadingTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    }

  ApplicationContainer sourceApps = source.Install (client);
  sourceApps.Start (startTime);
  sourceApps.Stop (stopTime);
  Ptr<FileTransferApplication> ftApplication = sourceApps.Get (0)->GetObject<FileTransferApplication> ();

  PacketSinkHelper sink ("ns3::TcpSocketFactory",InetSocketAddress (Ipv4Address::GetAny (), serverPort));
  ApplicationContainer sinkApps = sink.Install (server);
  sinkApps.Start (startTime);
  sinkApps.Stop (stopTime);

  return ftApplication;
}

void
DocsisHelper::PrintFlowSummaries (std::ostream& ostr, FlowMonitorHelper& flowHelper, Ptr<FlowMonitor> flowMonitor)
{
  NS_LOG_FUNCTION (this << &flowHelper << flowMonitor);
  flowMonitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats ();
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      std::stringstream protoStream;
      protoStream << (uint16_t) t.protocol;
      if (t.protocol == 6)
        {
          protoStream.str ("TCP");
        }
      if (t.protocol == 17)
        {
          protoStream.str ("UDP");
        }
      if (t.sourcePort == 50000)
        {
          continue; // This is just the TCP ack stream
        }
      ostr << "Flow " << protoStream.str () << " (" << t.sourceAddress << ":" << t.sourcePort << " -> " << t.destinationAddress << ":" << t.destinationPort << ")\n";
      ostr << "  Tx Packets: " << i->second.txPackets << "\n";
      ostr << "  Tx Bytes:   " << i->second.txBytes << "\n";
      ostr << "  Rx Packets: " << i->second.rxPackets << "\n";
      ostr << "  Rx Bytes:   " << i->second.rxBytes << "\n";
      if (i->second.rxPackets > 0)
        {
          // Measure the duration of the flow from receiver's perspective
          double rxDuration = i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ();
          ostr << "  Rx throughput: " << i->second.rxBytes * 8.0 / rxDuration / 1000 / 1000  << " Mbps\n";
          ostr << "  Mean delay:  " << 1000 * i->second.delaySum.GetSeconds () / i->second.rxPackets << " ms\n";
          ostr << "  Mean jitter:  " << 1000 * i->second.jitterSum.GetSeconds () / i->second.rxPackets  << " ms\n";
        }
      else
        {
          ostr << "  Throughput:  0 Mbps\n";
          ostr << "  Mean delay:  0 ms\n";
          ostr << "  Mean jitter: 0 ms\n";
        }
    }
}

void
DocsisHelper::PrintConfiguration (std::ostream& ostr, const NetDeviceContainer& c) const
{
  Ptr<CmNetDevice> upstream = GetUpstream (c);
  Ptr<CmtsNetDevice> downstream = GetDownstream (c);
  Ptr<CmtsUpstreamScheduler> scheduler = upstream->GetCmtsUpstreamScheduler ();
  Ptr<DocsisChannel> channel = upstream->GetChannel ()->GetObject<DocsisChannel> ();

  DoubleValue dVal;
  UintegerValue uVal;
  TimeValue tVal;
  DataRateValue rVal;

  std::streamsize oldPrecision = ostr.precision ();

  ostr << "Input parameters upstream" << std::endl;
  ostr << "-----------------------" << std::endl;
  scheduler->GetAttribute ("FreeCapacityMean", rVal);
  ostr << "FreeCapacityMean: Average upstream free capacity (bits/sec) = " << rVal.Get ().GetBitRate () << std::endl;
  scheduler->GetAttribute ("FreeCapacityVariation", dVal);
  ostr << "FreeCapacityVariation: Bound (percent) on the variation of upstream free capacity = " << dVal.Get () << std::endl;
  upstream->GetAttribute ("BurstPreparation", tVal);
  ostr << "The burst preparation time = " << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("UsScSpacing", dVal);
  ostr << "UsScSpacing: Upstream subcarrier spacing = " << dVal.Get () << std::endl;
  upstream->GetAttribute ("NumUsSc", uVal);
  ostr << "NumUsSc: Number of upstream subcarriers = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("SymbolsPerFrame", uVal);
  ostr << "SymbolsPerFrame: Number of symbols per frame = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("UsSpectralEfficiency", dVal);
  ostr << "UsSpectralEfficiency: Upstream spectral efficiency (bps/Hz) = " << dVal.Get () << std::endl;
  upstream->GetAttribute ("UsCpLen", uVal);
  ostr << "UsCpLen: Upstream cyclic prefix length = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("UsMacHdrSize", uVal);
  ostr << "UsMacHdrSize: Upstream Mac header size = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("UsSegHdrSize", uVal);
  ostr << "UsSegHdrSize: Upstream segment header size = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("MapInterval", tVal);
  ostr << "MapInterval: MAP interval = " << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("CmtsMapProcTime", tVal);
  ostr << "CmtsMapProcTime: CMTS MAP processing time = " << tVal.Get ().As (Time::US) << std::endl;
  upstream->GetAttribute ("CmUsPipelineFactor", uVal);
  ostr << "CmUsPipelineFactor: upstream pipeline factor at CM = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("CmtsUsPipelineFactor", uVal);
  ostr << "CmtsUsPipelineFactor: upstream pipeline factor at CMTS = " << uVal.Get () << std::endl;
  ostr << std::endl;

  ostr << "Input parameters downstream" << std::endl;
  ostr << "-------------------------" << std::endl;
  // Downstream
  downstream->GetAttribute ("FreeCapacityMean", rVal);
  ostr << "FreeCapacityMean: Average upstream free capacity (bits/sec) = " << rVal.Get ().GetBitRate () << std::endl;
  downstream->GetAttribute ("FreeCapacityVariation", uVal);
  ostr << "FreeCapacityVariation: Bound (percent) on the variation of upstream free capacity = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("MaxPdu", uVal);
  ostr << "MaxPdu: Peak rate token bucket maximum size (bytes) = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("DsScSpacing", dVal);
  ostr << "DsScSpacing: Downstream subcarrier spacing = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("NumDsSc", uVal);
  ostr << "NumDsSc: Number of downstream subcarriers = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("DsSpectralEfficiency", dVal);
  ostr << "DsSpectralEfficiency: Downstream spectral efficiency (bps/Hz) = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("DsCpLen", uVal);
  ostr << "DsCpLen: Downstream cyclic prefix length = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("DsIntlvM", uVal);
  ostr << "DsIntlvM: Downstream interleaver M = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("CmtsDsPipelineFactor", uVal);
  ostr << "CmtsDsPipelineFactor: downstream pipeline factor at CMTS = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("CmDsPipelineFactor", uVal);
  ostr << "CmDsPipelineFactor: downstream pipeline factor at CM = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("DsMacHdrSize", uVal);
  ostr << "DsMacHdrSize: Downstream Mac header size = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("AverageCodewordFill", dVal);
  ostr << "AverageCodewordFill: Average codeword fill = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("NcpModulation", uVal);
  ostr << "NcpModulation: Downstream NCP modulation = " << uVal.Get () << std::endl;
  ostr << std::endl;
  ostr << "System configuration and other assumptions" << std::endl;
  ostr << "------------------------------------------" << std::endl;
  upstream->GetAttribute ("NumUsChannels", uVal);
  ostr << "NumUsChannels: Number upstream channels = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("AverageUsBurst", uVal);
  ostr << "AverageUsBurst: Average size of upstream burst = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("AverageUsUtilization", dVal);
  ostr << "AverageUsUtilization: Average upstream utilization = " << dVal.Get () << std::endl;
  ostr << std::endl;
  ostr << "HFC plant" << std::endl;
  ostr << "---------" << std::endl;
  downstream->GetAttribute ("MaximumDistance", dVal);
  ostr << "MaximumDistance: (in km) = " << dVal.Get () << std::endl;
  ostr << std::endl;

  ostr << "Calculated parameters:  DOCSIS MAC" << std::endl;
  ostr << "----------------------------------" << std::endl;
  upstream->GetAttribute ("ScPerMinislot", uVal);
  ostr << "ScPerMinislot: Subcarriers per minislot = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("FrameDuration", tVal);
  ostr << "FrameDuration: Frame duration = " << std::fixed << std::setprecision (3) << tVal.Get ().As (Time::MS) << std::endl;
  ostr << std::setprecision (oldPrecision);
  ostr.unsetf (std::ios_base::fixed);
  upstream->GetAttribute ("MinislotsPerFrame", uVal);
  ostr << "MinislotsPerFrame: Minislots per frame = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("MinislotCapacity", uVal);
  ostr << "MinislotCapacity: Minislot capacity (bytes) = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("UsCapacity", dVal);
  ostr << "UsCapacity: Upstream capacity (bps) = " << dVal.Get () << std::endl;
  upstream->GetAttribute ("CmMapProcTime", tVal);
  ostr << "CmMapProcTime: CM MAP processing time = " << std::fixed << std::setprecision (3) << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("DsSymbolTime", tVal);
  ostr << "DsSymbolTime: Downstream symbol time = " << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("DsIntlvDelay", tVal);
  ostr << "DsIntlvDelay: Downstream interleaver delay = " << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("Rtt", tVal);
  ostr << "Rtt: Round trip time = " << tVal.Get ().As (Time::MS) << std::endl;
  ostr << std::setprecision (oldPrecision);
  ostr.unsetf (std::ios_base::fixed);
  upstream->GetAttribute ("MinReqGntDelay", uVal);
  ostr << "MinReqGntDelay: (frames) = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("FramesPerMap", uVal);
  ostr << "FramesPerMap: (frames) = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("MinislotsPerMap", uVal);
  ostr << "MinislotsPerMap: minislots per MAP = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("ActualMapInterval", tVal);
  ostr << "ActualMapInterval: actual MAP interval = " << std::fixed << std::setprecision (3) << tVal.Get ().As (Time::MS) << std::endl;
  ostr << std::endl;

  ostr << "Calculated parameters:  MAP message downstream overhead" << std::endl;
  ostr << "-------------------------------------------------------" << std::endl;
  downstream->GetAttribute ("UsGrantsPerSecond", dVal);
  ostr << "UsGrantsPerSecond: average # of grants scheduled on each US channel = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgIesPerMap", dVal);
  ostr << "AvgIesPerMap: average # of grants scheduled in each MAP = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgMapSize", dVal);
  ostr << "AvgMapSize: average size (bytes) of a MAP message = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgMapDatarate", dVal);
  ostr << "AvgMapDatarate: average MAP datarate (bits/sec) = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgMapOhPerSymbol", dVal);
  ostr << "AvgMapOhPerSymbol: average MAP overhead per symbol (bytes) = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("ScPerNcp", dVal);
  ostr << "ScPerNcp: subcarriers per NCP message block = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("ScPerCw", dVal);
  ostr << "ScPerCw: subcarriers per codeword = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("DsCodewordsPerSymbol", dVal);
  ostr << "DsCodewordsPerSymbol: Downstream codewords per symbol = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("DsSymbolCapacity", dVal);
  ostr << "DsSymbolCapacity: Downstream symbol capacity (bytes) = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("DsCapacity", dVal);
  ostr << "DsCapacity: Downstream capacity (bits/sec) = " << dVal.Get () << std::endl;
}

} // namespace docsis
} // namespace ns3
