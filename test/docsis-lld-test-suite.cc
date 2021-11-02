/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#include "ns3/test.h"
#include "ns3/log.h"
#include "ns3/object.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include "ns3/docsis-helper.h"
#include "ns3/cm-net-device.h"
#include "ns3/cmts-net-device.h"
#include "ns3/cmts-upstream-scheduler.h"
#include "ns3/docsis-channel.h"
#include "ns3/data-rate.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/net-device-container.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/udp-client.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ipv4-interface.h"
#include "ns3/arp-cache.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/queue-disc-container.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/queue-protection.h"
#include "ns3/simple-net-device-helper.h"
#include "ns3/simple-net-device.h"
#include "ns3/bridge-helper.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE ("DocsisLldTestSuite");

// Setup a small test network to inject traffic 
//
//  n0 <-----> n1 <----> n2 <------> n3
//  10.1.1.1                        10.1.1.2
//             CM       CMTS
//  traffic                          traffic
//  endpoint                         endpoint
//  (LAN node)                       (WAN node)
//
class DocsisLldTestCase : public TestCase
{
public:
  DocsisLldTestCase (std::string name);
  virtual ~DocsisLldTestCase ();

  void TraceBytesInQueue (std::string context, uint32_t oldValue, uint32_t newValue);
protected:
  virtual void SetupFourNodeTopology (uint32_t numServiceFlows);
  void CheckUpstreamLQueueSize (uint32_t expected);
  void CheckUpstreamCQueueSize (uint32_t expected);
  Ptr<CmNetDevice> m_upstream;
  Ptr<CmtsNetDevice> m_downstream;
  Ptr<DocsisChannel> m_channel;
  Ptr<Node> m_cmtsNode;
  Ptr<Node> m_cmNode;
  Ptr<Node> m_lanNode;
  Ptr<Node> m_wanNode;
  uint32_t m_upstreamBytesInQueue;
  uint32_t m_downstreamBytesInQueue;
  DataRate m_upstreamRate;
  DataRate m_downstreamRate;
  DataRate m_ggr;
  uint16_t m_ggi;
private:
};

DocsisLldTestCase::DocsisLldTestCase (std::string name)
  : TestCase (name),
    m_upstreamBytesInQueue (0),
    m_downstreamBytesInQueue (0),
    m_upstreamRate (DataRate (50000000)),
    m_downstreamRate (DataRate (200000000)),
    m_ggr (DataRate (0)),
    m_ggi (0)
{
}

DocsisLldTestCase::~DocsisLldTestCase ()
{
}

void
DocsisLldTestCase::TraceBytesInQueue (std::string context, uint32_t oldValue, uint32_t newValue)
{
  if (context == "upstream")
    {
      m_upstreamBytesInQueue = newValue;
    }
  else if (context == "downstream")
    {
      m_downstreamBytesInQueue = newValue;
    }
}

void
DocsisLldTestCase::SetupFourNodeTopology (uint32_t numServiceFlows)
{
  m_lanNode = CreateObject<Node> (); // Node ID 0
  m_cmNode = CreateObject<Node> (); // Node ID 1
  m_cmtsNode = CreateObject<Node> (); // Node ID 2
  m_wanNode = CreateObject<Node> (); // Node ID 3

  // Use SimpleNetDevice (to avoid csma module dependency)
  SimpleNetDeviceHelper simple;
  NodeContainer simple1 (m_lanNode);
  simple1.Add (m_cmNode);
  NodeContainer simple2 (m_cmtsNode);
  simple2.Add (m_wanNode);
  NetDeviceContainer n0n1Device = simple.Install (simple1);
  NetDeviceContainer n2n3Device = simple.Install (simple2);

  // Configure a small amount of channel delay to improve trace readability
  Ptr<SimpleNetDevice> simpleDevice = n0n1Device.Get (0)->GetObject<SimpleNetDevice> ();
  simpleDevice->GetChannel ()->SetAttribute ("Delay", TimeValue (MicroSeconds (1)));
  simpleDevice = n2n3Device.Get (0)->GetObject<SimpleNetDevice> ();
  simpleDevice->GetChannel ()->SetAttribute ("Delay", TimeValue (MicroSeconds (1)));

  Ptr<AggregateServiceFlow> upstreamAsf = CreateObject<AggregateServiceFlow> ();
  upstreamAsf->m_asfid = 1;
  upstreamAsf->m_maxSustainedRate = m_upstreamRate;
  upstreamAsf->m_peakRate = DataRate (4 * m_upstreamRate.GetBitRate ());
  upstreamAsf->m_maxTrafficBurst = 3064;
  Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  sf->m_targetBuffer = static_cast<uint32_t> (m_upstreamRate.GetBitRate () * 0.25 /8); // 250ms at MSR
  upstreamAsf->SetClassicServiceFlow (sf);
  if (numServiceFlows == 2)
    {
      Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
      sf2->m_guaranteedGrantRate = m_ggr;
      sf2->m_guaranteedGrantInterval = m_ggi;
      upstreamAsf->SetLowLatencyServiceFlow (sf2);
    }
  // Downstream
  Ptr<AggregateServiceFlow> downstreamAsf = CreateObject<AggregateServiceFlow> ();
  downstreamAsf->m_asfid = 1;
  downstreamAsf->m_maxSustainedRate = m_downstreamRate;
  downstreamAsf->m_peakRate = DataRate (4 * m_downstreamRate.GetBitRate ());
  downstreamAsf->m_maxTrafficBurst = 3064;
  downstreamAsf->SetClassicServiceFlow (sf);
  sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  sf->m_targetBuffer = static_cast<uint32_t> (m_downstreamRate.GetBitRate () * 0.25 /8); // 250ms at MSR
  if (numServiceFlows == 2)
    {
      Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
      downstreamAsf->SetLowLatencyServiceFlow (sf2);
    }

  DocsisHelper docsis;
  docsis.SetChannelAttribute ("Delay", StringValue ("1ms"));
  // Install arguments for DocsisHelper: (downstream (cmts), upstream (cm)). 
  NetDeviceContainer n2n1Device = docsis.Install (m_cmtsNode, m_cmNode, upstreamAsf, downstreamAsf);
  m_upstream = docsis.GetUpstream (n2n1Device);
  m_downstream = docsis.GetDownstream (n2n1Device);
  m_channel = docsis.GetChannel (n2n1Device);
  m_upstream->SetAttribute ("MapInterval", TimeValue (MilliSeconds (1)));

  // Set the random variables to avoid unwanted perturbations of random values
  m_upstream->AssignStreams (1);
  m_downstream->AssignStreams (11);
  m_upstream->GetCmtsUpstreamScheduler ()->AssignStreams (21);

  // Add an IP stack
  InternetStackHelper internet;
  internet.Install (m_lanNode);
  internet.Install (m_wanNode);

  // bridge the SimpleNetDevice and DocsisDevice on the two nodes
  BridgeHelper bridge;
  NetDeviceContainer cmDevices (n0n1Device.Get (1));
  cmDevices.Add (n2n1Device.Get (1));
  bridge.Install (m_cmNode, cmDevices);
  NetDeviceContainer cmtsDevices (n2n1Device.Get (0));
  cmtsDevices.Add (n2n3Device.Get (0));
  bridge.Install (m_cmtsNode, cmtsDevices);

  // Create a special device container with the NetDevices from node 0 and 3
  // for installing IP addresses
  NetDeviceContainer endpointDevices;
  endpointDevices.Add (n0n1Device.Get (0));
  endpointDevices.Add (n2n3Device.Get (1));

  // Add IP addresses to the endpoints
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.252");
  Ipv4InterfaceContainer ipv4Interfaces = ipv4.Assign (endpointDevices);

  // Configure static entries in ARP table; avoids address resolution delay
  std::pair<Ptr<Ipv4>, uint32_t> ifacePair0 = ipv4Interfaces.Get (0);
  std::pair<Ptr<Ipv4>, uint32_t> ifacePair1 = ipv4Interfaces.Get (1);
  Ptr<Ipv4Interface> iface0 = ifacePair0.first->GetObject<Ipv4L3Protocol> ()->GetInterface (ifacePair0.second);
  Ptr<Ipv4Interface> iface1 = ifacePair1.first->GetObject<Ipv4L3Protocol> ()->GetInterface (ifacePair1.second);
  Ptr<ArpCache> cache0 = iface0->GetArpCache ();
  Ptr<ArpCache> cache1 = iface1->GetArpCache ();
  ArpCache::Entry* entry0 = cache0->Add (Ipv4Address ("10.1.1.2"));
  entry0->SetMacAddress (iface1->GetDevice ()->GetAddress ());
  entry0->MarkPermanent ();
  ArpCache::Entry* entry1 = cache1->Add (Ipv4Address ("10.1.1.1"));
  entry1->SetMacAddress (iface0->GetDevice ()->GetAddress ());
  entry1->MarkPermanent ();

}

void 
DocsisLldTestCase::CheckUpstreamLQueueSize (uint32_t expected)
{
  Ptr<DualQueueCoupledAqm> qdisc = m_upstream->GetQueue ();
  NS_TEST_ASSERT_MSG_NE (qdisc, 0, "DualQ queue disc not found");
  NS_TEST_ASSERT_MSG_EQ (qdisc->GetLowLatencyQueueSize (), expected, "Expected L-queue length test failure");
}

void 
DocsisLldTestCase::CheckUpstreamCQueueSize (uint32_t expected)
{
  Ptr<DualQueueCoupledAqm> qdisc = m_upstream->GetQueue ();
  NS_TEST_ASSERT_MSG_NE (qdisc, 0, "DualQ queue disc not found");
  NS_TEST_ASSERT_MSG_EQ (qdisc->GetClassicQueueSize (), expected, "Expected C-queue length test failure");
}

class DocsisLldSingleUpstreamPacketTest : public DocsisLldTestCase
{
public:
  DocsisLldSingleUpstreamPacketTest ();
  virtual ~DocsisLldSingleUpstreamPacketTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  Time m_simulationTime;
  uint32_t m_txCount;
  Time m_lastRxTime;
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;

  void DeviceTxCallback (Ptr<const Packet> p);
  void MacRxCallback (Ptr<const Packet> p);
#ifdef NOTYET
  void GrantCallback (uint32_t value);
  void GrantUsedCallback (uint32_t value);
  void GrantUnusedCallback (uint32_t value);
#endif
};

DocsisLldSingleUpstreamPacketTest::DocsisLldSingleUpstreamPacketTest ()
  : DocsisLldTestCase ("Docsis LLD:  one upstream packet"),
    m_txCount (0)
{
}

DocsisLldSingleUpstreamPacketTest::~DocsisLldSingleUpstreamPacketTest ()
{
}

#ifdef NOTYET
void
DocsisLldSingleUpstreamPacketTest::GrantCallback (uint32_t value)
{
  NS_TEST_ASSERT_MSG_EQ (value, 1056, "Expected grant of 1056 bytes");
}

void
DocsisLldSingleUpstreamPacketTest::GrantUsedCallback (uint32_t value)
{
  NS_TEST_ASSERT_MSG_EQ (value, 1028, "Expected 1028 bytes of grant used");
}

void
DocsisLldSingleUpstreamPacketTest::GrantUnusedCallback (uint32_t value)
{
  NS_TEST_ASSERT_MSG_EQ (value, 28, "Expected 28 bytes of grant unused");
}
#endif

void
DocsisLldSingleUpstreamPacketTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLldSingleUpstreamPacketTest::MacRxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_lastRxTime = Simulator::Now ();
}

void
DocsisLldSingleUpstreamPacketTest::DoSetup (void)
{
  SetupFourNodeTopology (1);

  UdpServerHelper myServer (9);
  ApplicationContainer serverApp = myServer.Install (m_wanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper myClient (Ipv4Address ("10.1.1.2"), 9);
  myClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = myClient.Install (m_lanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();
  m_simulationTime = MilliSeconds (150);

  m_server->SetStartTime (Seconds (0));
  m_server->SetStopTime (m_simulationTime);

  m_client->SetStartTime (Seconds (0));
  m_client->SetStopTime (m_simulationTime);
}

void
DocsisLldSingleUpstreamPacketTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisLldSingleUpstreamPacketTest::DoRun ()");

  bool connected;
  // Capture trace of Tx events
  connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLldSingleUpstreamPacketTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_downstream->TraceConnectWithoutContext ("MacPromiscRx", MakeCallback (&DocsisLldSingleUpstreamPacketTest::MacRxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

#ifdef NOTYET
  // Capture traces of Grant events
  connected = m_upstream->TraceConnectWithoutContext ("Grant", MakeCallback (&DocsisLldSingleUpstreamPacketTest::GrantCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_upstream->TraceConnectWithoutContext ("GrantUsed", MakeCallback (&DocsisLldSingleUpstreamPacketTest::GrantUsedCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_upstream->TraceConnectWithoutContext ("GrantUnused", MakeCallback (&DocsisLldSingleUpstreamPacketTest::GrantUnusedCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
#endif
 
  // 972 + 20 bytes IPv4 + 8 bytes UDP will be 1000 bytes Ethernet payload
  m_client->SendPacketAt (MilliSeconds (100), 972);

  Simulator::Stop (m_simulationTime);
  Simulator::Run ();

  // First, did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, 1*(1000), "Did not see transmitted packet");

  // The packet arrives at time       0.100001 (100ms plus 1us)
  // The request is sent at           0.107100
  // The MAP message is received at   0.1022475
  // Frame preparation  is at time    0.101925
  // Sending time is at time          0.10287
  // Receive time end decoding is at  0.10331499
  //
  Time expectedTime = Seconds (0.10331499);

  // Check receive time is expected value within a tolerance of 2 us
  NS_TEST_ASSERT_MSG_EQ_TOL (m_lastRxTime, expectedTime, MicroSeconds (2), "Did not see packet at expected time");

}

void
DocsisLldSingleUpstreamPacketTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisLldSingleDownstreamPacketTest : public DocsisLldTestCase
{
public:
  DocsisLldSingleDownstreamPacketTest ();
  virtual ~DocsisLldSingleDownstreamPacketTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  Time m_simulationTime;
  uint32_t m_txCount;
  Time m_lastRxTime;
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;

  void DeviceTxCallback (Ptr<const Packet> p);
  void MacRxCallback (Ptr<const Packet> p);
};

DocsisLldSingleDownstreamPacketTest::DocsisLldSingleDownstreamPacketTest ()
  : DocsisLldTestCase ("Docsis LLD:  one downstream packet"),
    m_txCount (0)
{
}

DocsisLldSingleDownstreamPacketTest::~DocsisLldSingleDownstreamPacketTest ()
{
}

void
DocsisLldSingleDownstreamPacketTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLldSingleDownstreamPacketTest::MacRxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_lastRxTime = Simulator::Now ();
}

void
DocsisLldSingleDownstreamPacketTest::DoSetup (void)
{
  SetupFourNodeTopology (1);

  UdpServerHelper myServer (9);
  ApplicationContainer serverApp = myServer.Install (m_lanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper myClient (Ipv4Address ("10.1.1.1"), 9);
  myClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = myClient.Install (m_wanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();
  m_simulationTime = MilliSeconds (150);

  m_server->SetStartTime (Seconds (0));
  m_server->SetStopTime (m_simulationTime);

  m_client->SetStartTime (Seconds (0));
  m_client->SetStopTime (m_simulationTime);
}

void
DocsisLldSingleDownstreamPacketTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisLldSingleDownstreamPacketTest::DoRun ()");

  // Set to large value to permit one service flow to use entire symbol
  m_downstream->SetAttribute ("MaxPdu", UintegerValue (8000));

  // Capture trace of Tx events
  bool connected = m_downstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLldSingleDownstreamPacketTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_upstream->TraceConnectWithoutContext ("MacPromiscRx", MakeCallback (&DocsisLldSingleDownstreamPacketTest::MacRxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  // 972 + 20 bytes IPv4 + 8 bytes UDP will be 1000 bytes Ethernet payload
  m_client->SendPacketAt (MilliSeconds (100), 972);

  Simulator::Stop (m_simulationTime);
  Simulator::Run ();

  // First, did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, 1*(1000), "Did not see transmitted packet");

  // The packet arrives at time 0.100001  (100 ms plus 1 us)
  // At next symbol boundary, we push 1028 bytes to pipeline (0.1000125 s)
  // At next symbol boundary, we send 1028 bytes (0.100035)
  // The remote upstream starts decoding at 0.1000975
  // The remote upstream ends decoding at 0.100165
  //
  Time expectedTime = Seconds (0.100165);

  // Check receive time is expected value within a tolerance of 2 us
  NS_TEST_ASSERT_MSG_EQ_TOL (m_lastRxTime, expectedTime, MicroSeconds (2), "Did not see packet at expected time");

}

void
DocsisLldSingleDownstreamPacketTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisLldUpstreamPacketTest : public DocsisLldTestCase
{
public:
  DocsisLldUpstreamPacketTest (bool stepPacketSizes, uint32_t numPackets);
  virtual ~DocsisLldUpstreamPacketTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  uint32_t m_txCount {0};
  uint32_t m_udpReceived {0};
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  bool m_stepPacketSizes {false};
  uint32_t m_nextPacketSize {18}; // 18 bytes avoids padding
  uint32_t m_numPackets {0};
  Ptr<UniformRandomVariable> m_payloadRv;
  Ptr<UniformRandomVariable> m_intervalRv;
  uint32_t m_expectedTxCount {0};
  uint32_t m_expectedRxCount {0};
  uint32_t m_dropCount {0};

  void DeviceTxCallback (Ptr<const Packet> p);
  void DeviceTxDropCallback (Ptr<const Packet> p);
  void UdpServerCallback (Ptr<const Packet> p);
  uint32_t GetNextPacketSize (void);
};

DocsisLldUpstreamPacketTest::DocsisLldUpstreamPacketTest (bool stepPacketSizes, uint32_t numPackets)
  : DocsisLldTestCase ("Docsis LLD:  multiple upstream packets"),
    m_stepPacketSizes (stepPacketSizes),
    m_numPackets (numPackets)
{
  m_payloadRv = CreateObject<UniformRandomVariable> ();
  m_payloadRv->SetStream (100);
  m_intervalRv = CreateObject<UniformRandomVariable> ();
  m_intervalRv->SetStream (101);
}

DocsisLldUpstreamPacketTest::~DocsisLldUpstreamPacketTest ()
{
}

uint32_t
DocsisLldUpstreamPacketTest::GetNextPacketSize (void)
{
  if (m_stepPacketSizes)
    {
      m_nextPacketSize++;
      if (m_nextPacketSize > 1472)
        {
          m_nextPacketSize = 18;
        }
      return m_nextPacketSize;
    }
  else
    {
      // A value below 18 bytes in the next statement will force some padding
      // to occur (minimum Ethernet frame size is 64 bytes)
      return m_payloadRv->GetInteger (18, 1472);
    }
}

void
DocsisLldUpstreamPacketTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLldUpstreamPacketTest::DeviceTxDropCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx drop callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  // This packet has been accounted for in the txCount (MacTx is hit before
  // the drop) and both expected counts, so decrement all three counters
  m_expectedTxCount -= p->GetSize ();
  m_txCount -= p->GetSize ();
  m_expectedRxCount -= p->GetSize () - 28;
  m_dropCount++;
}

void
DocsisLldUpstreamPacketTest::UdpServerCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_udpReceived += p->GetSize ();
}

void
DocsisLldUpstreamPacketTest::DoSetup (void)
{
  SetupFourNodeTopology (1);

  UdpServerHelper myServer (9);
  ApplicationContainer serverApp = myServer.Install (m_wanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper myClient (Ipv4Address ("10.1.1.2"), 9);
  myClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = myClient.Install (m_lanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();

  m_server->SetStartTime (Seconds (0));
  m_client->SetStartTime (Seconds (0));
}

void
DocsisLldUpstreamPacketTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisLldUpstreamPacketTest::DoRun ()");

  // Capture trace of Tx events
  bool connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLldUpstreamPacketTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_upstream->TraceConnectWithoutContext ("MacTxDrop", MakeCallback (&DocsisLldUpstreamPacketTest::DeviceTxDropCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_server->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisLldUpstreamPacketTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  // Payload will range between ~40 and ~1500 bytes
  // Send roughly 700 bytes every 0.0015 seconds = a little less than 4 Mb/s
  Time nextTime = MilliSeconds (100);
  for (uint32_t i = 0; i < m_numPackets; i++)
    {
      // The below range will induce some drops for the stepped packet size
      nextTime += Seconds (m_intervalRv->GetValue (0.001, 0.0025));
      uint32_t payload = GetNextPacketSize ();
      m_expectedRxCount += payload;
      // Account for padding and headers
      m_expectedTxCount += std::max<uint32_t> (46, payload + 28);
      NS_LOG_DEBUG ("Send time: " << nextTime.GetSeconds () << "s; size " << payload << " bytes");
      m_client->SendPacketAt (nextTime, payload);
    }

  m_server->SetStopTime (nextTime + MilliSeconds (250));
  m_client->SetStopTime (nextTime + MilliSeconds (10));

  Simulator::Stop (nextTime + MilliSeconds (300));
  Simulator::Run ();

  // Did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, m_expectedTxCount, "Did not see transmitted bytes");
  // Did we receive what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_udpReceived, m_expectedRxCount, "Did not see received bytes");

}

void
DocsisLldUpstreamPacketTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisLldDownstreamPacketTest : public DocsisLldTestCase
{
public:
  DocsisLldDownstreamPacketTest ();
  virtual ~DocsisLldDownstreamPacketTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  uint32_t m_txCount;
  uint32_t m_udpReceived;
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;

  void DeviceTxCallback (Ptr<const Packet> p);
  void UdpServerCallback (Ptr<const Packet> p);
};

DocsisLldDownstreamPacketTest::DocsisLldDownstreamPacketTest ()
  : DocsisLldTestCase ("Docsis LLD:  multiple downstream packets"),
    m_txCount (0),
    m_udpReceived (0)
{
}

DocsisLldDownstreamPacketTest::~DocsisLldDownstreamPacketTest ()
{
}

void
DocsisLldDownstreamPacketTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLldDownstreamPacketTest::UdpServerCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_udpReceived += p->GetSize ();
}

void
DocsisLldDownstreamPacketTest::DoSetup (void)
{
  SetupFourNodeTopology (1);

  UdpServerHelper myServer (9);
  ApplicationContainer serverApp = myServer.Install (m_lanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper myClient (Ipv4Address ("10.1.1.1"), 9);
  myClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = myClient.Install (m_wanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();

  m_server->SetStartTime (Seconds (0));

  m_client->SetStartTime (Seconds (0));
}

void
DocsisLldDownstreamPacketTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisLldDownstreamPacketTest::DoRun ()");

  // Capture trace of Tx events
  bool connected = m_downstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLldDownstreamPacketTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_server->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisLldDownstreamPacketTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  uint32_t numPackets = 10000;
  Ptr<UniformRandomVariable> payloadRv = CreateObject<UniformRandomVariable> ();
  // Payload will range between ~40 and ~1500 bytes
  // Send roughly 700 bytes every 0.0015 seconds = a little less than 4 Mb/s
  Ptr<UniformRandomVariable> intervalRv = CreateObject<UniformRandomVariable> ();
  Time nextTime = MilliSeconds (100);
  uint32_t payload;
  uint32_t expectedTxCount = 0;
  uint32_t expectedRxCount = 0;
  for (uint32_t i = 0; i < numPackets; i++)
    {
      nextTime += Seconds (intervalRv->GetValue (0.001, 0.002));
      // A value below 18 bytes in the next statement will force some padding
      // to occur (minimum Ethernet frame size is 64 bytes)
      payload = payloadRv->GetInteger (18, 1472);
      expectedRxCount += payload;
      // Account for padding and headers
      expectedTxCount += std::max<uint32_t> (46, payload + 28);
      NS_LOG_DEBUG ("Send time: " << nextTime.GetSeconds () << "s; size " << payload << " bytes");
      m_client->SendPacketAt (nextTime, payload);
    }

  m_server->SetStopTime (nextTime + MilliSeconds (10));
  m_client->SetStopTime (nextTime + MilliSeconds (10));

  Simulator::Stop (nextTime + MilliSeconds (10));
  Simulator::Run ();

  // Did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, expectedTxCount, "Did not see transmitted bytes");
  // Did we receive what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_udpReceived, expectedRxCount, "Did not see received bytes");

}

void
DocsisLldDownstreamPacketTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisLldDualQUpstreamPacketTest : public DocsisLldTestCase
{
public:
  DocsisLldDualQUpstreamPacketTest ();
  virtual ~DocsisLldDualQUpstreamPacketTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  uint32_t m_txCount;
  uint32_t m_udpReceived;
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  Ptr<UdpClient> m_efClient;
  Ptr<UdpServer> m_efServer;

  void DeviceTxCallback (Ptr<const Packet> p);
  void UdpServerCallback (Ptr<const Packet> p);
};

DocsisLldDualQUpstreamPacketTest::DocsisLldDualQUpstreamPacketTest ()
  : DocsisLldTestCase ("Docsis LLD:  multiple upstream packets, dual queue"),
    m_txCount (0),
    m_udpReceived (0)
{
}

DocsisLldDualQUpstreamPacketTest::~DocsisLldDualQUpstreamPacketTest ()
{
}

void
DocsisLldDualQUpstreamPacketTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLldDualQUpstreamPacketTest::UdpServerCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_udpReceived += p->GetSize ();
}

void
DocsisLldDualQUpstreamPacketTest::DoSetup (void)
{
  SetupFourNodeTopology (2);

  UdpServerHelper udpServer (9);
  ApplicationContainer serverApp = udpServer.Install (m_wanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper udpClient (Ipv4Address ("10.1.1.2"), 9);
  udpClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = udpClient.Install (m_lanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();

  m_server->SetStartTime (Seconds (0));
  m_client->SetStartTime (Seconds (0));

  UdpServerHelper efServer (10);
  ApplicationContainer efServerApp = efServer.Install (m_wanNode);
  m_efServer = efServerApp.Get (0)->GetObject<UdpServer> ();

  InetSocketAddress destAddressUdpEf (Ipv4Address ("10.1.1.2"), 10);
  uint16_t tos = Ipv4Header::DSCP_EF << 2;
  tos = tos | 0x01;  // for ECT(1)
  destAddressUdpEf.SetTos (tos);

  UdpClientHelper efClient (destAddressUdpEf);
  efClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer efClientApp = efClient.Install (m_lanNode);
  m_efClient = efClientApp.Get (0)->GetObject<UdpClient> ();

  m_efServer->SetStartTime (Seconds (0));
  m_efClient->SetStartTime (Seconds (0));
}

void
DocsisLldDualQUpstreamPacketTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisLldDualQUpstreamPacketTest::DoRun ()");

  // Capture trace of Tx events
  bool connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLldDualQUpstreamPacketTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_server->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisLldDualQUpstreamPacketTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_efServer->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisLldDualQUpstreamPacketTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  uint32_t numPackets = 1000;
  Ptr<UniformRandomVariable> payloadRv = CreateObject<UniformRandomVariable> ();
  // Payload will range between ~40 and ~1500 bytes
  // Send roughly 700 bytes every 0.0015 seconds = a little less than 4 Mb/s
  Ptr<UniformRandomVariable> intervalRv = CreateObject<UniformRandomVariable> ();
  Time nextTime = MilliSeconds (100);
  uint32_t payload;
  uint32_t expectedTxCount = 0;
  uint32_t expectedRxCount = 0;
  for (uint32_t i = 0; i < numPackets; i++)
    {
      nextTime += Seconds (intervalRv->GetValue (0.002, 0.004));
      // A value below 18 bytes in the next statement will force some padding
      // to occur (minimum Ethernet frame size is 64 bytes)
      payload = payloadRv->GetInteger (18, 1472);
      expectedRxCount += payload;
      // Account for padding and headers
      expectedTxCount += std::max<uint32_t> (46, payload + 28);
      NS_LOG_DEBUG ("Send time: " << nextTime.GetSeconds () << "s; size " << payload << " bytes");
      m_client->SendPacketAt (nextTime, payload);
    }

  for (uint32_t i = 0; i < numPackets; i++)
    {
      nextTime += Seconds (intervalRv->GetValue (0.002, 0.004));
      // A value below 18 bytes in the next statement will force some padding
      // to occur (minimum Ethernet frame size is 64 bytes)
      payload = payloadRv->GetInteger (18, 1472);
      expectedRxCount += payload;
      // Account for padding and headers
      expectedTxCount += std::max<uint32_t> (46, payload + 28);
      NS_LOG_DEBUG ("Send time: " << nextTime.GetSeconds () << "s; size " << payload << " bytes");
      m_efClient->SendPacketAt (nextTime, payload);
    }

  m_server->SetStopTime (nextTime + MilliSeconds (250));
  m_client->SetStopTime (nextTime + MilliSeconds (10));

  m_efServer->SetStopTime (nextTime + MilliSeconds (250));
  m_efClient->SetStopTime (nextTime + MilliSeconds (10));

  Simulator::Stop (nextTime + MilliSeconds (300));
  Simulator::Run ();

  // Did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, expectedTxCount, "Did not see transmitted bytes");
  // Did we receive what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_udpReceived, expectedRxCount, "Did not see received bytes");

}

void
DocsisLldDualQUpstreamPacketTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisLldDualQUpstreamTwoPacketReactiveTest : public DocsisLldTestCase
{
public:
  DocsisLldDualQUpstreamTwoPacketReactiveTest ();
  virtual ~DocsisLldDualQUpstreamTwoPacketReactiveTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  uint32_t m_txCount;
  uint32_t m_udpReceived;
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  Ptr<UdpClient> m_efClient;
  Ptr<UdpServer> m_efServer;

  void DeviceTxCallback (Ptr<const Packet> p);
  void UdpServerCallback (Ptr<const Packet> p);
};

DocsisLldDualQUpstreamTwoPacketReactiveTest::DocsisLldDualQUpstreamTwoPacketReactiveTest ()
  : DocsisLldTestCase ("Docsis LLD:  Dual queue, reactive sched, one LLD and one classic packet"),
    m_txCount (0),
    m_udpReceived (0)
{
}

DocsisLldDualQUpstreamTwoPacketReactiveTest::~DocsisLldDualQUpstreamTwoPacketReactiveTest ()
{
}

void
DocsisLldDualQUpstreamTwoPacketReactiveTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLldDualQUpstreamTwoPacketReactiveTest::UdpServerCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_udpReceived += p->GetSize ();
}

void
DocsisLldDualQUpstreamTwoPacketReactiveTest::DoSetup (void)
{
  SetupFourNodeTopology (2);

  UdpServerHelper udpServer (9);
  ApplicationContainer serverApp = udpServer.Install (m_wanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper udpClient (Ipv4Address ("10.1.1.2"), 9);
  udpClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = udpClient.Install (m_lanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();

  m_server->SetStartTime (Seconds (0));
  m_client->SetStartTime (Seconds (0));

  UdpServerHelper efServer (10);
  ApplicationContainer efServerApp = efServer.Install (m_wanNode);
  m_efServer = efServerApp.Get (0)->GetObject<UdpServer> ();

  InetSocketAddress destAddressUdpEf (Ipv4Address ("10.1.1.2"), 10);
  uint16_t tos = Ipv4Header::DSCP_EF << 2;
  tos = tos | 0x01;  // for ECT(1)
  destAddressUdpEf.SetTos (tos);

  UdpClientHelper efClient (destAddressUdpEf);
  efClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer efClientApp = efClient.Install (m_lanNode);
  m_efClient = efClientApp.Get (0)->GetObject<UdpClient> ();

  m_efServer->SetStartTime (Seconds (0));
  m_efClient->SetStartTime (Seconds (0));
}

void
DocsisLldDualQUpstreamTwoPacketReactiveTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisLldDualQUpstreamTwoPacketReactiveTest::DoRun ()");

  // Capture trace of Tx events
  bool connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLldDualQUpstreamTwoPacketReactiveTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_server->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisLldDualQUpstreamTwoPacketReactiveTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_efServer->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisLldDualQUpstreamTwoPacketReactiveTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  Ptr<UniformRandomVariable> payloadRv = CreateObject<UniformRandomVariable> ();
  // Payload will range between ~40 and ~1500 bytes
  // Send roughly 700 bytes every 0.0015 seconds = a little less than 4 Mb/s
  Ptr<UniformRandomVariable> intervalRv = CreateObject<UniformRandomVariable> ();
  uint32_t payload = 1450;
  uint32_t expectedTxCount = 0;
  uint32_t expectedRxCount = 0;
  Time nextTime = MilliSeconds (100) + Seconds (intervalRv->GetValue (0.002, 0.004));
  NS_LOG_DEBUG ("Classic send time: " << nextTime.GetSeconds () << "s; size " << payload << " bytes");
  m_client->SendPacketAt (nextTime, payload);
  expectedRxCount += payload;
  // Account for padding and headers
  expectedTxCount += std::max<uint32_t> (46, payload + 28);

  payload = 100;
  NS_LOG_DEBUG ("LL send time: " << nextTime.GetSeconds () << "s; size " << payload << " bytes");
  m_efClient->SendPacketAt (nextTime, payload);
  expectedRxCount += payload;
  // Account for padding and headers
  expectedTxCount += std::max<uint32_t> (46, payload + 28);

  m_server->SetStopTime (nextTime + MilliSeconds (200));
  m_client->SetStopTime (nextTime + MilliSeconds (10));

  m_efServer->SetStopTime (nextTime + MilliSeconds (200));
  m_efClient->SetStopTime (nextTime + MilliSeconds (10));

  Simulator::Stop (nextTime + MilliSeconds (210));
  Simulator::Run ();

  // Did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, expectedTxCount, "Did not see transmitted bytes");
  // Did we receive what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_udpReceived, expectedRxCount, "Did not see received bytes");

}

void
DocsisLldDualQUpstreamTwoPacketReactiveTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisQueueDiscItemTest : public DocsisLldTestCase
{
public:
  DocsisQueueDiscItemTest ();
  virtual ~DocsisQueueDiscItemTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  Ptr<UdpClient> m_efClient;
  Ptr<UdpServer> m_efServer;
};

DocsisQueueDiscItemTest::DocsisQueueDiscItemTest ()
  : DocsisLldTestCase ("Test the DocsisQueueDiscItem")
{
}

DocsisQueueDiscItemTest::~DocsisQueueDiscItemTest ()
{
}
void
DocsisQueueDiscItemTest::DoSetup (void)
{
  SetupFourNodeTopology (2);

  UdpServerHelper udpServer (9);
  ApplicationContainer serverApp = udpServer.Install (m_wanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper udpClient (Ipv4Address ("10.1.1.2"), 9);
  udpClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = udpClient.Install (m_lanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();

  m_server->SetStartTime (Seconds (0));
  m_client->SetStartTime (Seconds (0));

  UdpServerHelper efServer (10);
  ApplicationContainer efServerApp = efServer.Install (m_wanNode);
  m_efServer = efServerApp.Get (0)->GetObject<UdpServer> ();

  InetSocketAddress destAddressUdpEf (Ipv4Address ("10.1.1.2"), 10);
  uint16_t tos = Ipv4Header::DSCP_EF << 2;
  tos = tos | 0x01;  // for ECT(1)
  destAddressUdpEf.SetTos (tos);

  UdpClientHelper efClient (destAddressUdpEf);
  efClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer efClientApp = efClient.Install (m_lanNode);
  m_efClient = efClientApp.Get (0)->GetObject<UdpClient> ();

  m_efServer->SetStartTime (Seconds (0));
  m_efClient->SetStartTime (Seconds (0));
}

void
DocsisQueueDiscItemTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisQueueDiscItemTest::DoRun ()");

  uint32_t payload = 1472;  // UDP payload size in bytes
  m_client->SendPacketAt (MicroSeconds (1000), payload);
  // Check that two microseconds later, there are
  // 1500 IP bytes (1472 + 8 + 20) plus 18 (Ethernet) plus 10 bytes
  // (upstream MAC header size) = 1528 in queue
  Simulator::Schedule (MicroSeconds (1002), &DocsisQueueDiscItemTest::CheckUpstreamCQueueSize, this, 1528);
  // Wait a few ms for the data to be sent, and retry with different size
  payload = 12;  // UDP payload size in bytes
  m_client->SendPacketAt (MicroSeconds (6000), payload);
  // Check that two microseconds later, there are
  // 40 IP bytes (12 + 8 + 20) plus 24 (Ethernet pads to 64 bytes) plus 10 bytes
  // (upstream MAC header size) = 74 in queue
  Simulator::Schedule (MicroSeconds (6002), &DocsisQueueDiscItemTest::CheckUpstreamCQueueSize, this, 74);
  
  // Recheck with L-queue
  payload = 1472;  // UDP payload size in bytes
  m_efClient->SendPacketAt (MicroSeconds (11000), payload);
  // Check that two microseconds later, there are
  // 1500 IP bytes (1472 + 8 + 20) plus 18 (Ethernet) plus 10 bytes
  // (upstream MAC header size) = 1528 in queue
  Simulator::Schedule (MicroSeconds (11002), &DocsisQueueDiscItemTest::CheckUpstreamLQueueSize, this, 1528);
  // Wait a few ms for the data to be sent, and retry with different size
  payload = 12;  // UDP payload size in bytes
  m_efClient->SendPacketAt (MicroSeconds (16000), payload);
  // Check that two microseconds later, there are
  // 40 IP bytes (12 + 8 + 20) plus 24 (Ethernet pads to 64 bytes) plus 10 bytes
  // (upstream MAC header size) = 74 in queue
  Simulator::Schedule (MicroSeconds (16002), &DocsisQueueDiscItemTest::CheckUpstreamLQueueSize, this, 74);
  
  Simulator::Stop (MicroSeconds (21000));
  Simulator::Run ();

}

void
DocsisQueueDiscItemTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisPacketBurstTest : public DocsisLldTestCase
{
public:
  DocsisPacketBurstTest ();
  virtual ~DocsisPacketBurstTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  Ptr<UdpClient> m_client2;
  Ptr<UdpServer> m_server2;
};

DocsisPacketBurstTest::DocsisPacketBurstTest ()
  : DocsisLldTestCase ("Test the packet burst capability")
{
}

DocsisPacketBurstTest::~DocsisPacketBurstTest ()
{
}
void
DocsisPacketBurstTest::DoSetup (void)
{
  SetupFourNodeTopology (2);

  // Schedule packet trains to be run at the specified time

  DocsisHelper lldHelper;
  uint16_t udpPort = 9;
  uint16_t frameSize = 1000;
  Time startTime = Seconds (2);
  Time duration = Seconds (1);
  // Send a 1000-byte packet burst at 1 Mbps at time 2 seconds for 1 second
  // duration 
  Ptr<UdpClient> client;
  std::pair<Ptr<UdpClient>, Ptr<UdpServer> > appPair;
  appPair = lldHelper.AddPacketBurstDuration (m_lanNode, m_wanNode,
    udpPort, udpPort, DataRate ("1Mbps"), frameSize, 
    startTime, duration, Ipv4Header::DscpDefault, Ipv4Header::ECN_ECT1);
  m_client = appPair.first;
  m_server = appPair.second;
  
  // Send 500 frames of 200 bytes each at 1 Mbps at time 4 seconds
  frameSize = 200;
  startTime = Seconds (4);
  uint32_t frameCount = 500;
  udpPort = 10;
  appPair = lldHelper.AddPacketBurstCount (m_lanNode, m_wanNode,
    udpPort, udpPort, DataRate ("1Mbps"), frameSize, 
    startTime, frameCount, Ipv4Header::DscpDefault, Ipv4Header::ECN_ECT0);
  m_client2 = appPair.first;
  m_server2 = appPair.second;
}

void
DocsisPacketBurstTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisPacketBurstTest::DoRun ()");

  Simulator::Stop (Seconds (5));
  Simulator::Run ();

  // ns-3 logging confirms that the first packet is sent at time 2.000s, the
  // second at time 2.008s (i.e. 8 ms interarrival) as expected.  The last
  // is sent at 2.992s, as expected (125 packets).  Then, exactly 500 packets 
  // are logged between times 4.000s and 4.79840s

  NS_TEST_ASSERT_MSG_EQ (m_server->GetReceived (), 125, "Incorrect number of received frames");
  NS_TEST_ASSERT_MSG_EQ (m_server2->GetReceived (), 500, "Incorrect number of received frames");
}

void
DocsisPacketBurstTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisTimeToMinislotsTest : public DocsisLldTestCase
{
public:
  DocsisTimeToMinislotsTest ();
  virtual ~DocsisTimeToMinislotsTest () {}

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);
};

DocsisTimeToMinislotsTest::DocsisTimeToMinislotsTest (void)
  : DocsisLldTestCase ("DocsisTimeToMinislotsTest")
{
}

void
DocsisTimeToMinislotsTest::DoSetup (void)
{
  // Setup topology with two service flows
  SetupFourNodeTopology (2);
}

void
DocsisTimeToMinislotsTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisTimeToMinislotsTest::DoRun ()");
  // These tests assume that a frame is 135 us duration, and that 235 minislots
  // exist per frame.  If default configurations change, then this test
  // must be updated
  NS_TEST_ASSERT_MSG_EQ (m_upstream->TimeToMinislots (Seconds (0)), 0, "Incorrect conversion for time 0");
  // Times less than the first frame duration should return zero minislots
  NS_TEST_ASSERT_MSG_EQ (m_upstream->TimeToMinislots (MicroSeconds (134)), 0, "Incorrect conversion for time 134us (< 1 frame)");
  // Time equal to first frame duration should return 235 minislots
  NS_TEST_ASSERT_MSG_EQ (m_upstream->TimeToMinislots (MicroSeconds (135)), 235, "Incorrect conversion for time 135us (1 frame)");
  // Time slightly greater than first frame duration should return 235 minislots
  NS_TEST_ASSERT_MSG_EQ (m_upstream->TimeToMinislots (MicroSeconds (136)), 235, "Incorrect conversion for time 136us (1 frame)");
  // The 32-bit quantity will roll over on the 18276457'th frame. 
  // Time 2467321560 us corresponds to 18276456 frames and a minislot count of 4294967160 
  // Time 2467321695 corresponds to 18276457 frames and a minislot count of 4294967395
  // This should roll over and return a value of 4294967395 % 4294967296 = 99
  NS_TEST_ASSERT_MSG_EQ (m_upstream->TimeToMinislots (MicroSeconds (2467321560)), 4294967160, "Incorrect conversion for large time");
  NS_TEST_ASSERT_MSG_EQ (m_upstream->TimeToMinislots (MicroSeconds (2467321695)), 99, "Incorrect roll-over");
}

void
DocsisTimeToMinislotsTest::DoTeardown (void)
{
  Simulator::Destroy ();
}


class DocsisLldGgiTest : public DocsisLldTestCase
{
public:
  DocsisLldGgiTest (Time mapInterval, DataRate amsr, DataRate sendRate, DataRate ggr, uint16_t ggi);
  virtual ~DocsisLldGgiTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  uint32_t m_txCount {0};
  uint32_t m_udpReceived {0};
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  uint32_t m_expectedTxCount {0};
  uint32_t m_expectedRxCount {0};
  uint32_t m_dropCount {0};
  Time m_mapInterval;
  DataRate m_sendRate;
  DataRate m_ggr;
  uint16_t m_ggi;

  void DeviceTxCallback (Ptr<const Packet> p);
  void DeviceTxDropCallback (Ptr<const Packet> p);
  void UdpServerCallback (Ptr<const Packet> p);
  void MapTraceCallback (CmtsUpstreamScheduler::MapReport report);
};

DocsisLldGgiTest::DocsisLldGgiTest (Time mapInterval, DataRate amsr, DataRate sendRate, DataRate ggr, uint16_t ggi)
  : DocsisLldTestCase ("Docsis LLD:  GGI"),
    m_mapInterval (mapInterval),
    m_sendRate (sendRate),
    m_ggr (ggr),
    m_ggi (ggi)
{
  m_upstreamRate = amsr;
}

DocsisLldGgiTest::~DocsisLldGgiTest ()
{
}

void
DocsisLldGgiTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLldGgiTest::DeviceTxDropCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx drop callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  // This packet has been accounted for in the txCount (MacTx is hit before
  // the drop) and both expected counts, so decrement all three counters
  m_expectedTxCount -= p->GetSize ();
  m_txCount -= p->GetSize ();
  m_expectedRxCount -= p->GetSize () - 28;
  m_dropCount++;
}

void
DocsisLldGgiTest::UdpServerCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_udpReceived += p->GetSize ();
}

// B >= GGI * GGR
// effectively:  GGI 945 = 7 frames, GGI 946*1.1 (1041)
// measure bytes granted over interval (1.1*GGI).  Interval not aligned 
// on a frame boundary.  1041/135 = 7
void
DocsisLldGgiTest::MapTraceCallback (CmtsUpstreamScheduler::MapReport report)
{
#ifdef NOTYET
  std::cout << Now ().GetSeconds () << " ggr " <<  report.m_guaranteedGrantRate.GetBitRate () << " ggi " << report.m_guaranteedGrantInterval <<std::endl;
  std::cout << "mi " <<  report.m_mapInterval.GetMicroSeconds () << " framespermap " << report.m_framesPerMap << std::endl;
  std::cout <<  "alloc start time " <<  report.m_message.m_allocStartTime << " msperframe " << m_upstream->GetMinislotsPerFrame () << std::endl;
  std::cout <<  "alloc start time " <<  report.m_message.m_allocStartTime << " bytesperms " << m_upstream->GetMinislotCapacity () << std::endl;
#endif
}

void
DocsisLldGgiTest::DoSetup (void)
{
  // Set m_ggr and m_ggi before scenario setup
  SetupFourNodeTopology (2);
  m_upstream->SetAttribute ("MapInterval", TimeValue (m_mapInterval));

  UdpServerHelper myServer (9);
  ApplicationContainer serverApp = myServer.Install (m_wanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper myClient (Ipv4Address ("10.1.1.2"), 9);
  myClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = myClient.Install (m_lanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();

  m_server->SetStartTime (Seconds (1));
  m_client->SetStartTime (Seconds (1));
  m_server->SetStopTime (Seconds (11) + MilliSeconds (300));
  m_client->SetStopTime (Seconds (11));
}

void
DocsisLldGgiTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisLldGgiTest::DoRun ()");

  // Capture trace of Tx events
  bool connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLldGgiTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_upstream->TraceConnectWithoutContext ("MacTxDrop", MakeCallback (&DocsisLldGgiTest::DeviceTxDropCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_server->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisLldGgiTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_upstream->GetCmtsUpstreamScheduler ()->TraceConnectWithoutContext ("MapTrace", MakeCallback (&DocsisLldGgiTest::MapTraceCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  if (m_sendRate.GetBitRate () > 0)
    {
      uint32_t payload = 1000;
      Time interval = Seconds ((payload * 8.0)/m_sendRate.GetBitRate ());
      Time elapsed = Seconds (1);
      while (elapsed < Seconds (11))
        {
          elapsed += interval;
          m_expectedRxCount += payload;
          // Account for padding and headers
          m_expectedTxCount += std::max<uint32_t> (46, payload + 28);
          NS_LOG_DEBUG ("Send time: " << elapsed.GetSeconds () << "s; size " << payload << " bytes");
          m_client->SendPacketAt (elapsed, payload);
        }
      NS_LOG_DEBUG ("Expected Tx count " << m_expectedTxCount);
      NS_LOG_DEBUG ("Expected Rx count " << m_expectedRxCount);
    }

  Simulator::Stop (Seconds (11) + MilliSeconds (300));
  Simulator::Run ();


  // Did we transmit what we expected?
  NS_LOG_DEBUG ("Expected Tx count " << m_expectedTxCount << " actual " << m_txCount);
  NS_TEST_ASSERT_MSG_EQ (m_txCount, m_expectedTxCount, "Did not see transmitted bytes");
  NS_LOG_DEBUG ("Expected Rx count " << m_expectedRxCount << " actual " << m_udpReceived);
  // Did we receive what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_udpReceived, m_expectedRxCount, "Did not see received bytes");

}

void
DocsisLldGgiTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

/**
 * This test is configured to send one packet per service flow every
 * 500ms to create a light load on the system.  The test is run for
 * 45 minutes of simulation time, to ensure that the minislot counters
 * overflow, and that the model's logic correctly handles this overflow.
 */
class DocsisCheckWraparoundTest : public DocsisLldTestCase
{
public:
  DocsisCheckWraparoundTest ();
  virtual ~DocsisCheckWraparoundTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  uint64_t m_txCount {0};
  uint64_t m_udpReceived {0};
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  Ptr<UdpClient> m_llClient;
  Ptr<UdpServer> m_llServer;
  uint64_t m_expectedTxCount {0};
  uint64_t m_expectedRxCount {0};
  uint64_t m_dropCount {0};
  Time m_stopTime;

  void DeviceTxCallback (Ptr<const Packet> p);
  void DeviceTxDropCallback (Ptr<const Packet> p);
  void UdpServerCallback (Ptr<const Packet> p);
};

DocsisCheckWraparoundTest::DocsisCheckWraparoundTest ()
  : DocsisLldTestCase ("Docsis Wraparound Check")
{
  // Wraparound of the minislot counter should occur between 41 and 42
  // minutes into the simulation with this configuration
  // (1740735 minislots/sec, and rollover occurs when a 32-bit unsigned
  // integer counter overflows; i.e. at 4294967295).  Check that operation
  // continues without asserts or faults for a few minutes beyond this
  // overflow time.
  m_stopTime = Minutes (45);
}

DocsisCheckWraparoundTest::~DocsisCheckWraparoundTest ()
{
}

void
DocsisCheckWraparoundTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisCheckWraparoundTest::DeviceTxDropCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx drop callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  // This packet has been accounted for in the txCount (MacTx is hit before
  // the drop) and both expected counts, so decrement all three counters
  m_expectedTxCount -= p->GetSize ();
  m_txCount -= p->GetSize ();
  m_expectedRxCount -= p->GetSize () - 28;
  m_dropCount++;
}

void
DocsisCheckWraparoundTest::UdpServerCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_udpReceived += p->GetSize ();
}

void
DocsisCheckWraparoundTest::DoSetup (void)
{
  SetupFourNodeTopology (2);
  m_upstream->SetAttribute ("MapInterval", TimeValue (MilliSeconds (1)));

  // classic client/server
  UdpServerHelper myServer (9);
  ApplicationContainer serverApp = myServer.Install (m_wanNode);
  m_server = serverApp.Get (0)->GetObject<UdpServer> ();

  UdpClientHelper myClient (Ipv4Address ("10.1.1.2"), 9);
  myClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer clientApp = myClient.Install (m_lanNode);
  m_client = clientApp.Get (0)->GetObject<UdpClient> ();

  m_server->SetStartTime (Seconds (1));
  m_client->SetStartTime (Seconds (1));
  m_server->SetStopTime (m_stopTime + Seconds (1));
  m_client->SetStopTime (m_stopTime);

  // low latency client/server
  UdpServerHelper myLlServer (10);
  ApplicationContainer llServerApp = myLlServer.Install (m_wanNode);
  m_llServer = llServerApp.Get (0)->GetObject<UdpServer> ();

  InetSocketAddress destAddressUdpEf (Ipv4Address ("10.1.1.2"), 10);
  uint16_t tos = Ipv4Header::DSCP_EF << 2;
  tos = tos | 0x01;  // for ECT(1)
  destAddressUdpEf.SetTos (tos);

  UdpClientHelper myLlClient (destAddressUdpEf);
  myLlClient.SetAttribute ("OnDemand", BooleanValue (true));
  ApplicationContainer llClientApp = myLlClient.Install (m_lanNode);
  m_llClient = llClientApp.Get (0)->GetObject<UdpClient> ();

  m_llServer->SetStartTime (Seconds (1));
  m_llClient->SetStartTime (Seconds (1));
  m_llServer->SetStopTime (m_stopTime + Seconds (1));
  m_llClient->SetStopTime (m_stopTime);

}

void
DocsisCheckWraparoundTest::DoRun (void)
{
  NS_LOG_DEBUG ("DocsisCheckWraparoundTest::DoRun ()");

  // Capture trace of Tx events
  bool connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisCheckWraparoundTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_upstream->TraceConnectWithoutContext ("MacTxDrop", MakeCallback (&DocsisCheckWraparoundTest::DeviceTxDropCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_server->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisCheckWraparoundTest::UdpServerCallback, this));
  connected = m_llServer->TraceConnectWithoutContext ("Rx", MakeCallback (&DocsisCheckWraparoundTest::UdpServerCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  // Send one packet on each service flow every 500ms
  uint32_t payload = 1000;
  Time interval = MilliSeconds (500);
  Time elapsed = Seconds (1);
  uint64_t expectedPacketCount = 0;
  while (elapsed < m_stopTime)
    {
      elapsed += interval;
      m_expectedRxCount += payload;
      // Account for padding and headers
      m_expectedTxCount += std::max<uint32_t> (46, payload + 28);
      NS_LOG_DEBUG ("Classic send time: " << elapsed.GetSeconds () << "s; size " << payload << " bytes");
      m_client->SendPacketAt (elapsed, payload);
      m_expectedRxCount += payload;
      // Account for padding and headers
      m_expectedTxCount += std::max<uint32_t> (46, payload + 28);
      NS_LOG_DEBUG ("Low latency send time: " << elapsed.GetSeconds () << "s; size " << payload << " bytes");
      m_llClient->SendPacketAt (elapsed, payload);
      expectedPacketCount += 2;
    }
  NS_LOG_DEBUG ("Expected packet count " << expectedPacketCount);
  NS_LOG_DEBUG ("Expected Tx byte count " << m_expectedTxCount);
  NS_LOG_DEBUG ("Expected Rx byte count " << m_expectedRxCount);

  Simulator::Stop (m_stopTime + Seconds (1) + MilliSeconds (1));
  Simulator::Run ();


  // Did we transmit what we expected?
  NS_LOG_DEBUG ("Expected Tx count " << m_expectedTxCount << " actual " << m_txCount);
  NS_TEST_ASSERT_MSG_EQ (m_txCount, m_expectedTxCount, "Did not see transmitted bytes");
  NS_LOG_DEBUG ("Expected Rx count " << m_expectedRxCount << " actual " << m_udpReceived);
  // Did we receive what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_udpReceived, m_expectedRxCount, "Did not see received bytes");

}

void
DocsisCheckWraparoundTest::DoTeardown (void)
{
  Simulator::Destroy ();
}

class DocsisLldTestSuite : public TestSuite
{
public:
  DocsisLldTestSuite ();
};

DocsisLldTestSuite::DocsisLldTestSuite ()
  : TestSuite ("docsis-lld", UNIT)
{
  AddTestCase (new DocsisLldSingleUpstreamPacketTest, TestCase::QUICK);
  AddTestCase (new DocsisLldSingleDownstreamPacketTest, TestCase::QUICK);
  // 5000 packets, random packet sizes between 18 and 1472 bytes
  AddTestCase (new DocsisLldUpstreamPacketTest (false, 5000), TestCase::QUICK);
  // 5000 packets, packet sizes stepped from 18 through 1472 bytes
  AddTestCase (new DocsisLldUpstreamPacketTest (true, 5000), TestCase::QUICK);
  AddTestCase (new DocsisLldDownstreamPacketTest, TestCase::QUICK);
  AddTestCase (new DocsisLldDualQUpstreamPacketTest, TestCase::QUICK);
  AddTestCase (new DocsisLldDualQUpstreamTwoPacketReactiveTest, TestCase::QUICK);
  AddTestCase (new DocsisQueueDiscItemTest, TestCase::QUICK);
  AddTestCase (new DocsisPacketBurstTest, TestCase::QUICK);
  AddTestCase (new DocsisTimeToMinislotsTest, TestCase::QUICK);
  // Test GGI and GGR configuration
  // MAP interval, AMSR, GGR, GGI
  AddTestCase (new DocsisLldGgiTest (MilliSeconds (1), DataRate ("50Mb/s"), DataRate (0), DataRate ("5Mb/s"), 135), TestCase::QUICK);
  // Test that long simulation, with wraparound of time values, works correctly
  AddTestCase (new DocsisCheckWraparoundTest, TestCase::QUICK);
}

static DocsisLldTestSuite docsisLldTestSuite;

