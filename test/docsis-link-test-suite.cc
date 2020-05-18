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
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/udp-client.h"
#include "ns3/udp-server.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/application-container.h"
#include "ns3/packet.h"
#include "ns3/cmts-net-device.h"
#include "ns3/cm-net-device.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-header.h"
#include "docsis-link-test-class.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE ("DocsisLinkTestSuite");

class DocsisLinkSingleUpstreamPacketTest : public DocsisLinkTestCase
{
public:
  DocsisLinkSingleUpstreamPacketTest (bool pointToPointMode);
  virtual ~DocsisLinkSingleUpstreamPacketTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  Time m_simulationTime;
  uint32_t m_txCount;
  Time m_lastRxTime;
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  bool m_pointToPointMode {false};

  void DeviceTxCallback (Ptr<const Packet> p);
  void MacRxCallback (Ptr<const Packet> p);
};

DocsisLinkSingleUpstreamPacketTest::DocsisLinkSingleUpstreamPacketTest (bool pointToPointMode)
  : DocsisLinkTestCase ("Docsis LLD:  one upstream packet"),
    m_txCount (0),
    m_pointToPointMode (pointToPointMode)
{
  NS_LOG_DEBUG ("Point-to-point mode: " << pointToPointMode);
}

DocsisLinkSingleUpstreamPacketTest::~DocsisLinkSingleUpstreamPacketTest ()
{
}

void
DocsisLinkSingleUpstreamPacketTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLinkSingleUpstreamPacketTest::MacRxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_lastRxTime = Simulator::Now ();
}

void
DocsisLinkSingleUpstreamPacketTest::DoSetup (void)
{
  NS_LOG_FUNCTION (this);
  SetupFourNodeTopology ();
  AddDualQueue ();
  // one service flow
  Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  sf->m_maxSustainedRate = m_upstreamRate;
  sf->m_peakRate = DataRate (2 * m_upstreamRate.GetBitRate ());
  sf->m_maxTrafficBurst = static_cast<uint32_t> (m_upstreamRate.GetBitRate () * 0.1 / 8); // 100ms at MSR
  m_upstream->SetUpstreamSf (sf);
  sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  sf->m_maxSustainedRate = m_downstreamRate;
  sf->m_peakRate = DataRate (2 * m_downstreamRate.GetBitRate ());
  sf->m_maxTrafficBurst = static_cast<uint32_t> (m_downstreamRate.GetBitRate () * 0.1 / 8); // 100ms at MSR
  m_downstream->SetDownstreamSf (sf);

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
DocsisLinkSingleUpstreamPacketTest::DoRun (void)
{
  NS_LOG_FUNCTION (this);

  // Ensure that random variables are set as expected
  NS_TEST_ASSERT_MSG_EQ (RngSeedManager::GetRun (), 1, "Error:  RngRun number needs to be set to 1");
  m_upstream->SetAttribute ("PointToPointMode", BooleanValue (m_pointToPointMode));
  m_upstream->SetAttribute ("DataRate", DataRateValue (DataRate ("50Mbps")));
  bool connected;
  // Capture trace of Tx events
  connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLinkSingleUpstreamPacketTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_downstream->TraceConnectWithoutContext ("MacPromiscRx", MakeCallback (&DocsisLinkSingleUpstreamPacketTest::MacRxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  // 972 + 20 bytes IPv4 + 8 bytes UDP will be 1000 bytes Ethernet payload
  m_client->SendPacketAt (MilliSeconds (100), 972);

  Simulator::Stop (m_simulationTime);
  Simulator::Run ();

  // First, did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, 1*(1000), "Did not see transmitted packet");

  Time expectedTime;
  if (m_pointToPointMode == false)
    {
      // The packet arrives at time       0.100001 (100ms plus 1us)
      // The request is sent at           0.100710
      // The MAP message is received at   0.1025175
      // Sending time is at time          0.103410
      // Receive time end decoding is at  0.103720
      expectedTime = Seconds (0.103720);
    }
  else
    {
      // The packet arrives at time       0.100001 (100ms plus 1us)
      // 1018 bytes at 50 Mbps = 162.88 us
      // The transmission completes at    0.10016388
      // The far-side receives at         0.10020388 (162.88us + 40 us prop)
      // The far-side ends decoding at    0.10033888 (one frame later)
      expectedTime = Seconds (0.100339);
    }
  // Check receive time is expected value within a tolerance of 2 us
  NS_TEST_ASSERT_MSG_EQ_TOL (m_lastRxTime, expectedTime, MicroSeconds (2), "Did not see packet at expected time");
}

void
DocsisLinkSingleUpstreamPacketTest::DoTeardown (void)
{
  NS_LOG_FUNCTION (this);
  Simulator::Destroy ();
}

class DocsisLinkMultipleUpstreamPacketTest : public DocsisLinkTestCase
{
public:
  DocsisLinkMultipleUpstreamPacketTest (bool pointToPointMode);
  virtual ~DocsisLinkMultipleUpstreamPacketTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  Time m_simulationTime;
  uint32_t m_txCount;
  Time m_lastRxTime;
  Ptr<UdpClient> m_client;
  Ptr<UdpServer> m_server;
  bool m_pointToPointMode {false};

  void DeviceTxCallback (Ptr<const Packet> p);
  void MacRxCallback (Ptr<const Packet> p);
};

DocsisLinkMultipleUpstreamPacketTest::DocsisLinkMultipleUpstreamPacketTest (bool pointToPointMode)
  : DocsisLinkTestCase ("Docsis LLD:  multiple upstream packets"),
    m_txCount (0),
    m_pointToPointMode (pointToPointMode)
{
  NS_LOG_DEBUG ("Point-to-point mode: " << pointToPointMode);
  NS_TEST_ASSERT_MSG_EQ (pointToPointMode, true, "Non-point-to-point mode is not yet supported");
}

DocsisLinkMultipleUpstreamPacketTest::~DocsisLinkMultipleUpstreamPacketTest ()
{
}

void
DocsisLinkMultipleUpstreamPacketTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txCount += p->GetSize ();
}

void
DocsisLinkMultipleUpstreamPacketTest::MacRxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_lastRxTime = Simulator::Now ();
}

void
DocsisLinkMultipleUpstreamPacketTest::DoSetup (void)
{
  NS_LOG_FUNCTION (this);
  SetupFourNodeTopology ();
  AddDualQueue ();
  m_upstream->SetUpstreamAsf (GetUpstreamAsf ());
  m_downstream->SetDownstreamAsf (GetDownstreamAsf ());


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
DocsisLinkMultipleUpstreamPacketTest::DoRun (void)
{
  NS_LOG_FUNCTION (this);

  // Ensure that random variables are set as expected
  NS_TEST_ASSERT_MSG_EQ (RngSeedManager::GetRun (), 1, "Error:  RngRun number needs to be set to 1");
  m_upstream->SetAttribute ("PointToPointMode", BooleanValue (m_pointToPointMode));
  m_upstream->SetAttribute ("DataRate", DataRateValue (DataRate ("50Mbps")));
  bool connected;
  // Capture trace of Tx events
  connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLinkMultipleUpstreamPacketTest::DeviceTxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_downstream->TraceConnectWithoutContext ("MacPromiscRx", MakeCallback (&DocsisLinkMultipleUpstreamPacketTest::MacRxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  // Send 10 packets roughly back-to-back
  // 972 + 20 bytes IPv4 + 8 bytes UDP will be 1000 bytes Ethernet payload
  // It takes 163 us transmission time at 50 Mbps
  for (uint16_t i = 0; i < 10; i++)
    { 
      m_client->SendPacketAt (MilliSeconds (100) + i * MicroSeconds (200), 972);
    }

  Simulator::Stop (m_simulationTime);
  Simulator::Run ();

  // First, did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txCount, 10*(1000), "Did not see transmitted packet");

  // Transmissions at:
  //   0.100001
  //   0.100201
  //   0.100401
  //   ... 0.101801 (10th packet)
  // Receptions of each at 162.88 us + 40 us + 135 us = 0.00033788 sec later

  Time expectedTime  = Seconds (0.101801) + Seconds (0.00033788);
  // Check receive time is expected value within a tolerance of 2 us
  NS_TEST_ASSERT_MSG_EQ_TOL (m_lastRxTime, expectedTime, MicroSeconds (2), "Did not see packet at expected time");

}

void
DocsisLinkMultipleUpstreamPacketTest::DoTeardown (void)
{
  NS_LOG_FUNCTION (this);
  Simulator::Destroy ();
}

class DocsisLinkCbrTwoServiceFlowTest : public DocsisLinkTestCase
{
public:
  DocsisLinkCbrTwoServiceFlowTest (uint32_t numPackets, uint32_t ggr);
  virtual ~DocsisLinkCbrTwoServiceFlowTest ();

private:
  virtual void DoSetup (void);
  virtual void DoRun (void);
  virtual void DoTeardown (void);

  Time m_simulationTime;
  uint32_t m_txByteCount {0};
  uint32_t m_txPacketCount {0};
  uint32_t m_rxByteCount {0};
  uint32_t m_rxPacketCount {0};
  uint32_t m_cGrantCount {0};
  uint32_t m_lGrantCount {0};
  uint32_t m_cRequestCount {0};
  uint32_t m_lRequestCount {0};
  Time m_lastTxTime;
  Time m_lastRxTime;
  Time m_lastCRequestTime;
  Time m_lastLRequestTime;
  Ptr<UdpClient> m_cClient;
  Ptr<UdpServer> m_cServer;
  Ptr<UdpClient> m_lClient;
  Ptr<UdpServer> m_lServer;
  uint32_t m_numPackets;
  uint32_t m_ggr;

  void DeviceTxCallback (Ptr<const Packet> p);
  void MacRxCallback (Ptr<const Packet> p);
  void CGrantCallback (uint32_t bytes);
  void LGrantCallback (uint32_t bytes);
  void CRequestCallback (uint32_t bytes);
  void LRequestCallback (uint32_t bytes);
};

DocsisLinkCbrTwoServiceFlowTest::DocsisLinkCbrTwoServiceFlowTest (uint32_t numPackets, uint32_t ggr)
  : DocsisLinkTestCase ("Docsis LLD:  two service flow test"),
    m_numPackets (numPackets),
    m_ggr (ggr)
{
}

DocsisLinkCbrTwoServiceFlowTest::~DocsisLinkCbrTwoServiceFlowTest ()
{
}

void
DocsisLinkCbrTwoServiceFlowTest::DeviceTxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("Tx callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_txByteCount += p->GetSize ();
  m_txPacketCount += 1;
  m_lastTxTime = Simulator::Now ();
}

void
DocsisLinkCbrTwoServiceFlowTest::MacRxCallback (Ptr<const Packet> p)
{
  NS_LOG_DEBUG ("MacRxEnd callback of size " << p->GetSize () << " at " << Simulator::Now ().GetMicroSeconds ());
  m_rxByteCount += p->GetSize ();
  m_rxPacketCount += 1;
  m_lastRxTime = Simulator::Now ();
}

void
DocsisLinkCbrTwoServiceFlowTest::CGrantCallback (uint32_t bytes)
{
  m_cGrantCount += bytes;
  NS_LOG_DEBUG ("C grant bytes " << bytes << " cumulative " << m_cGrantCount);
}

void
DocsisLinkCbrTwoServiceFlowTest::LGrantCallback (uint32_t bytes)
{
  m_lGrantCount += bytes;
  NS_LOG_DEBUG ("L grant bytes " << bytes << " cumulative " << m_lGrantCount);
}

void
DocsisLinkCbrTwoServiceFlowTest::CRequestCallback (uint32_t bytes)
{
  m_cRequestCount += bytes;
  NS_LOG_DEBUG ("C request bytes " << bytes << " cumulative " << m_cRequestCount);
  m_lastCRequestTime = Simulator::Now ();
}

void
DocsisLinkCbrTwoServiceFlowTest::LRequestCallback (uint32_t bytes)
{
  m_lRequestCount += bytes;
  m_lastLRequestTime = Simulator::Now ();
  NS_LOG_DEBUG ("L request bytes " << bytes << " cumulative " << m_lRequestCount);
}

void
DocsisLinkCbrTwoServiceFlowTest::DoSetup (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow> ();
  asf->m_asfid = 1;
  asf->m_maxSustainedRate = m_upstreamRate;
  asf->m_peakRate = DataRate (2 * m_upstreamRate.GetBitRate ());
  asf->m_maxTrafficBurst = static_cast<uint32_t> (m_upstreamRate.GetBitRate () * 0.1 / 8);
  Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  asf->SetClassicServiceFlow (sf);
  Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
  sf2->m_guaranteedGrantRate = m_ggr;
  asf->SetLowLatencyServiceFlow (sf2);

  Ptr<AggregateServiceFlow> downstreamAsf = CreateObject<AggregateServiceFlow> ();
  downstreamAsf->m_asfid = 2;
  downstreamAsf->m_maxSustainedRate = m_downstreamRate;
  downstreamAsf->m_peakRate = DataRate (2 * m_downstreamRate.GetBitRate ());
  downstreamAsf->m_maxTrafficBurst = static_cast<uint32_t> (m_downstreamRate.GetBitRate () * 0.1 / 8);
  Ptr<ServiceFlow> sf3 = CreateObject<ServiceFlow> (CLASSIC_SFID);
  downstreamAsf->SetClassicServiceFlow (sf3);
  Ptr<ServiceFlow> sf4 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
  downstreamAsf->SetLowLatencyServiceFlow (sf4);

  SetupFourNodeTopology ();
  AddDualQueue ();
  m_upstream->SetUpstreamAsf (asf);
  m_downstream->SetDownstreamAsf (downstreamAsf);

  m_cServer = CreateObject<UdpServer> ();
  m_cServer->SetAttribute ("Port", UintegerValue (9));
  m_wanNode->AddApplication (m_cServer);

  m_cClient = CreateObject<UdpClient> ();
  InetSocketAddress remote = InetSocketAddress (Ipv4Address ("10.1.1.2"), 9);
  m_cClient->SetAttribute ("RemoteAddress", AddressValue (remote));
  m_cClient->SetAttribute ("OnDemand", BooleanValue (true));
  m_lanNode->AddApplication (m_cClient);

  m_lServer = CreateObject<UdpServer> ();
  m_lServer->SetAttribute ("Port", UintegerValue (10));
  m_wanNode->AddApplication (m_lServer);

  m_lClient = CreateObject<UdpClient> ();
  remote = InetSocketAddress (Ipv4Address ("10.1.1.2"), 10);
  uint16_t tos = Ipv4Header::DSCP_EF << 2;
  remote.SetTos (tos);
  m_lClient->SetAttribute ("RemoteAddress", AddressValue (remote));
  m_lClient->SetAttribute ("OnDemand", BooleanValue (true));
  m_lanNode->AddApplication (m_lClient);

  //  100 ms to start packets, 250 ms reasonable upper bound on queue depth and
  // link latency, and 200 us per packet
  m_simulationTime = MilliSeconds (100) + MilliSeconds (250) + MicroSeconds (329) * m_numPackets;

  m_cServer->SetStartTime (Seconds (0));
  m_cServer->SetStopTime (m_simulationTime);

  m_cClient->SetStartTime (Seconds (0));
  m_cClient->SetStopTime (m_simulationTime);

  m_lServer->SetStartTime (Seconds (0));
  m_lServer->SetStopTime (m_simulationTime);

  m_lClient->SetStartTime (Seconds (0));
  m_lClient->SetStopTime (m_simulationTime);
}

void
DocsisLinkCbrTwoServiceFlowTest::DoRun (void)
{
  NS_LOG_FUNCTION (this);

  // Ensure that random variables are set as expected
  NS_TEST_ASSERT_MSG_EQ (RngSeedManager::GetRun (), 1, "Error:  RngRun number needs to be set to 1");
  bool connected;
  // Capture trace of Tx events
  connected = m_upstream->TraceConnectWithoutContext ("MacTx", MakeCallback (&DocsisLinkCbrTwoServiceFlowTest::DeviceTxCallback, this));
  connected = m_upstream->TraceConnectWithoutContext ("ClassicGrantReceived", MakeCallback (&DocsisLinkCbrTwoServiceFlowTest::CGrantCallback, this));
  connected = m_upstream->TraceConnectWithoutContext ("LowLatencyGrantReceived", MakeCallback (&DocsisLinkCbrTwoServiceFlowTest::LGrantCallback, this));
  connected = m_upstream->TraceConnectWithoutContext ("ClassicRequest", MakeCallback (&DocsisLinkCbrTwoServiceFlowTest::CRequestCallback, this));
  connected = m_upstream->TraceConnectWithoutContext ("LowLatencyRequest", MakeCallback (&DocsisLinkCbrTwoServiceFlowTest::LRequestCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");
  connected = m_downstream->TraceConnectWithoutContext ("MacPromiscRx", MakeCallback (&DocsisLinkCbrTwoServiceFlowTest::MacRxCallback, this));
  NS_TEST_ASSERT_MSG_EQ (connected, true, "Could not hook callback");

  // 972 + 20 bytes IPv4 + 8 bytes UDP will be 1000 bytes Ethernet payload
  // Ethernet header + MAC header is 1028 byte packets.  329us interarrival
  // time works out to be almost 25 Mbps
  for (uint32_t i = 0; i < m_numPackets; i++)
    { 
      m_cClient->SendPacketAt (MilliSeconds (100) + i * MicroSeconds (329), 972);
    }
  // 972 + 20 bytes IPv4 + 8 bytes UDP will be 1000 bytes Ethernet payload
  // Ethernet header + MAC header is 1028 byte packets.  329us interarrival
  // time works out to be almost 25 Mbps
  for (uint16_t i = 0; i < m_numPackets; i++)
    { 
      m_lClient->SendPacketAt (MilliSeconds (100) + i * MicroSeconds (329), 972);
    }
  Simulator::Stop (m_simulationTime);
  Simulator::Run ();

  // Did we transmit what we expected?
  NS_TEST_ASSERT_MSG_EQ (m_txPacketCount, 2 * m_numPackets, "Did not see transmitted packets");
  NS_TEST_ASSERT_MSG_EQ (m_txByteCount, 2 * m_numPackets*(1000), "Did not see transmitted bytes");
  // Did we receive all packets?
  NS_TEST_ASSERT_MSG_EQ (m_rxPacketCount, 2 * m_numPackets, "Did not see received packets");
  NS_TEST_ASSERT_MSG_EQ (m_rxByteCount, 2 * m_numPackets*(1000), "Did not see received bytes");
  // 0.010 is ten milliseconds; upper bound on latency if no backlog
  NS_TEST_ASSERT_MSG_LT ((m_lastRxTime - m_lastTxTime).GetSeconds (), 0.010, "Excessive latency observed");

  NS_LOG_DEBUG ("TX count " << m_txPacketCount << " RX count " << m_rxPacketCount);
  NS_LOG_DEBUG ("Last TX " << m_lastTxTime.GetSeconds () << " RX " << m_lastRxTime.GetSeconds () << " diff (ms) " << (m_lastRxTime - m_lastTxTime).GetSeconds () * 1000);
  NS_LOG_DEBUG ("Last C request time " << m_lastCRequestTime.GetSeconds () << " L request time " << m_lastLRequestTime.GetSeconds ());
}

void
DocsisLinkCbrTwoServiceFlowTest::DoTeardown (void)
{
  NS_LOG_FUNCTION (this);
  Simulator::Destroy ();
}

class DocsisLinkTestSuite : public TestSuite
{
public:
  DocsisLinkTestSuite ();
};

DocsisLinkTestSuite::DocsisLinkTestSuite ()
  : TestSuite ("docsis-link", UNIT)
{
  // Check transmission of single packet, point-to-point mode enabled/disabled
  AddTestCase (new DocsisLinkSingleUpstreamPacketTest (false), TestCase::QUICK);
  AddTestCase (new DocsisLinkSingleUpstreamPacketTest (true), TestCase::QUICK);
  // Check that point-to-point mode works for multiple packets
  AddTestCase (new DocsisLinkMultipleUpstreamPacketTest (true), TestCase::QUICK);
  // Set MSR to 50 Mb/s and configure bursts at almost half of MSR for 
  // each service flow.   Each argument below is the number of back-to-back
  // packets to test (sent at intervals of 329 us corresponding to 25 Mbps)
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (1, 0), TestCase::QUICK);
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (10, 0), TestCase::QUICK);
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (100, 0), TestCase::QUICK);
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (1000, 0), TestCase::QUICK);
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (5000, 0), TestCase::QUICK);
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (10000, 0), TestCase::QUICK);
  // 30000 yields about 10 seconds
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (30000, 0), TestCase::QUICK);
  // Check for typical values of GGR (two slots per frame)
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (10000, 5690000), TestCase::QUICK);
  // Check that classic queue is not starved when GGR is at the MSR
  // and peak rate > MSR is available. This tests the unused grant accounting
  AddTestCase (new DocsisLinkCbrTwoServiceFlowTest (10000, 50000000), TestCase::QUICK);
}

static DocsisLinkTestSuite docsisLinkTestSuite;

