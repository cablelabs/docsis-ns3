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
 *
 * Authors:
 * Tom Henderson <tomh@tomh.org>
 */

#include "ns3/test.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/docsis-helper.h"
#include "ns3/cm-net-device.h"
#include "ns3/cmts-net-device.h"
#include "dual-queue-test.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE ("DualQCoupledPiSquaredTestSuite");

// Some constants for readability
static const bool LL = true;
static const bool CLASSIC = false;

// Dummy function to provide qDelaySingle() callback
Time
qDelaySingle (void)
{
  return Seconds (0);
}

class DualQIaqmThresholdsTestCase : public TestCase
{
public:
  DualQIaqmThresholdsTestCase ();
private:
  virtual void DoRun (void);
};

DualQIaqmThresholdsTestCase::DualQIaqmThresholdsTestCase ()
  : TestCase ("Check operation of IAQM ramp thresholds")
{
}

void
DualQIaqmThresholdsTestCase::DoRun (void)
{
  Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm> ();
  Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("50Mbps");
  queue->SetAsf (asf);
  queue->SetAttribute ("LgRange", UintegerValue (19));
  queue->SetAttribute ("MaxTh", TimeValue (MicroSeconds (1000)));
  Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter> ();
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  queue->AddPacketFilter (filter);
  queue->Initialize ();
  // FLOOR = 2 * 8 * 2000 * 10^9/50000000 = 640 us
  // RANGE = 1 << 19 = 524 us
  // MINTH = max (1000 - 524, 640) = 640
  Time tolerance = MicroSeconds (1);
  TimeValue minTh;
  queue->GetAttribute ("MinTh", minTh);
  NS_TEST_EXPECT_MSG_EQ_TOL (minTh.Get (), MicroSeconds (640), tolerance, "MinTh not within tolerance");

  // Check value without FLOOR.  If MAXTH = 2000 us, MINTH = (2000 - 524) us
  queue = CreateObject<DualQueueCoupledAqm> ();
  asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("50Mbps");
  queue->SetAsf (asf);
  queue->SetAttribute ("LgRange", UintegerValue (19));
  queue->SetAttribute ("LgRange", UintegerValue (19));
  queue->SetAttribute ("MaxTh", TimeValue (MicroSeconds (2000)));
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  queue->Initialize ();
  queue->GetAttribute ("MinTh", minTh);
  NS_TEST_EXPECT_MSG_EQ_TOL (minTh.Get (), MicroSeconds (2000 - 524), tolerance, "MinTh not within tolerance");
}

class DualQueueByteCountsTestCase : public TestCase
{
public:
  DualQueueByteCountsTestCase ();
private:
  virtual void DoRun (void);
  uint32_t m_pieQueueBytes {0};
  uint32_t m_lowLatencyBytes {0};
  uint32_t m_classicBytes {0};
  void TraceLowLatencyBytes (uint32_t oldValue, uint32_t newValue);
  void TraceClassicBytes (uint32_t oldValue, uint32_t newValue);
  void TracePieQueueBytes (uint32_t oldValue, uint32_t newValue);
};

DualQueueByteCountsTestCase::DualQueueByteCountsTestCase ()
  : TestCase ("Check that different queue byte counts are maintained correctly")
{
}

void
DualQueueByteCountsTestCase::TraceLowLatencyBytes (uint32_t oldValue, uint32_t newValue)
{
  m_lowLatencyBytes = newValue;
}

void
DualQueueByteCountsTestCase::TraceClassicBytes (uint32_t oldValue, uint32_t newValue)
{
  m_classicBytes = newValue;
}

void
DualQueueByteCountsTestCase::TracePieQueueBytes (uint32_t oldValue, uint32_t newValue)
{
  m_pieQueueBytes = newValue;
}

void
DualQueueByteCountsTestCase::DoRun (void)
{
  Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm> ();
  Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("50Mbps");
  queue->SetAsf (asf);
  Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter> ();
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  queue->AddPacketFilter (filter);
  queue->Initialize ();
  queue->TraceConnectWithoutContext ("LowLatencyBytes", MakeCallback (&DualQueueByteCountsTestCase::TraceLowLatencyBytes, this));
  queue->TraceConnectWithoutContext ("ClassicBytes", MakeCallback (&DualQueueByteCountsTestCase::TraceClassicBytes, this));
  queue->TraceConnectWithoutContext ("PieQueueBytes", MakeCallback (&DualQueueByteCountsTestCase::TracePieQueueBytes, this));

  uint16_t protocol = 6;
  uint32_t macHeaderSize = 10; // bytes

  // Create some test addresses
  Ipv4Address src ("100.2.3.4");
  Ipv4Address dest1 ("99.5.6.7");
  Ipv4Address dest2 ("102.100.8.3");
  Ipv4Address dest3 ("119.80.7.3");

  Ptr<Packet> p1 = Create<Packet> (82);
  Ptr<Packet> p2 = Create<Packet> (1300);
  Ptr<Packet> p3 = Create<Packet> (20);  // less than Ethernet minimum

  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (false), 0, "No MAC header should be included when false");
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (true), 0, "No MAC header should be included when false");
  // Enqueue a non-padded packet to the low latency queue
  Ptr<DualQueueTestItem> item1 = Create<DualQueueTestItem> (p1, src, dest1, protocol, macHeaderSize, LL);
  queue->Enqueue (item1);
  // Check the output of the traces
  NS_TEST_EXPECT_MSG_EQ (m_lowLatencyBytes, (82 + 18 + 10), "Size should include packet plus mac header plus 18 for Ethernet framing");
  NS_TEST_EXPECT_MSG_EQ (m_classicBytes, 0, "No bytes should be in the classic queue yet");
  NS_TEST_EXPECT_MSG_EQ (m_pieQueueBytes, 0, "No bytes should be in the classic queue yet");
  // Check the output of the method GetLowLatencyQueueSize ()
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (false), (82 + 18), "No MAC header should be included when false");
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (true), (82 + 18 + 10), "MAC header should be included when true");
  // Dequeue and check the output of the traces
  Ptr<QueueDiscItem> dequeueItem1 = queue->LowLatencyDequeue (); 
  NS_TEST_EXPECT_MSG_EQ (dequeueItem1->GetSize (), (82 + 18 + 10), "Size should include packet plus mac header plus 18 for Ethernet framing");
  NS_TEST_EXPECT_MSG_EQ (dequeueItem1->GetPacket ()->GetSize (), 82, "Size should just be of the internal packet");
  NS_TEST_EXPECT_MSG_EQ (m_lowLatencyBytes, 0, "No bytes should remain in the low latency queue");
  // Check the output of the method GetLowLatencyQueueSize ()
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (false), 0, "No bytes should remain in the low latency queue");
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (true), 0, "No bytes should remain in the low latency queue");

  // Enqueue a non-padded packet to the classic queue
  Ptr<DualQueueTestItem> item2 = Create<DualQueueTestItem> (p2, src, dest2, protocol, macHeaderSize, CLASSIC);
  queue->Enqueue (item2);
  // Check the output of the traces
  NS_TEST_EXPECT_MSG_EQ (m_classicBytes, (1300 + 18 + 10), "Size should include packet plus mac header plus 18 for Ethernet framing");
  NS_TEST_EXPECT_MSG_EQ (m_pieQueueBytes, (1300 + 18), "Size should include packet plus 18 for Ethernet framing; no MAC header bytes");
  NS_TEST_EXPECT_MSG_EQ (m_lowLatencyBytes, 0, "No bytes should be in the low latency queue");
  // Check the output of the method GetClassicQueueSize ()
  NS_TEST_EXPECT_MSG_EQ (queue->GetClassicQueueSize (false), (1300 + 18), "No MAC header should be included when false");
  NS_TEST_EXPECT_MSG_EQ (queue->GetClassicQueueSize (true), (1300 + 18 + 10), "MAC header should be included when true");
  // Dequeue and check the output of the traces
  Ptr<QueueDiscItem> dequeueItem2 = queue->ClassicDequeue (); 
  NS_TEST_EXPECT_MSG_EQ (dequeueItem2->GetSize (), (1300 + 18 + 10), "Size should include packet plus mac header plus 18 for Ethernet framing");
  NS_TEST_EXPECT_MSG_EQ (dequeueItem2->GetPacket ()->GetSize (), 1300, "Size should just be of the internal packet");
  NS_TEST_EXPECT_MSG_EQ (m_lowLatencyBytes, 0, "No bytes should remain in the low latency queue");
  NS_TEST_EXPECT_MSG_EQ (m_pieQueueBytes, 0, "No bytes should remain in the low latency queue");
  // Check the output of the method GetClassicQueueSize ()
  NS_TEST_EXPECT_MSG_EQ (queue->GetClassicQueueSize (false), 0, "No MAC header should be included when false");
  NS_TEST_EXPECT_MSG_EQ (queue->GetClassicQueueSize (true), 0, "MAC header should be included when true");

  // Enqueue a padded packet (smaller than 46 bytes) to the low latency queue
  // Packet will be padded out to 64 bytes of MAC PDU
  Ptr<DualQueueTestItem> item3 = Create<DualQueueTestItem> (p3, src, dest3, protocol, macHeaderSize, LL);
  queue->Enqueue (item3);
  // Check the output of the traces
  NS_TEST_EXPECT_MSG_EQ (m_lowLatencyBytes, (64 + 10), "Size should include packet plus mac header plus 18 for Ethernet framing");
  NS_TEST_EXPECT_MSG_EQ (m_classicBytes, 0, "No bytes should be in the classic queue yet");
  NS_TEST_EXPECT_MSG_EQ (m_pieQueueBytes, 0, "No bytes should be in the classic queue yet");
  // Check the output of the method GetLowLatencyQueueSize ()
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (false), (64), "No MAC header should be included when false");
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (true), (64 + 10), "MAC header should be included when true");
  // Dequeue and check the output of the traces
  Ptr<QueueDiscItem> dequeueItem3 = queue->LowLatencyDequeue (); 
  NS_TEST_EXPECT_MSG_EQ (dequeueItem3->GetSize (), (64 + 10), "Size should include packet plus mac header plus 18 for Ethernet framing");
  NS_TEST_EXPECT_MSG_EQ (dequeueItem3->GetPacket ()->GetSize (), 20, "Size should just be of the internal packet");
  NS_TEST_EXPECT_MSG_EQ (m_lowLatencyBytes, 0, "No bytes should remain in the low latency queue");
  // Check the output of the method GetLowLatencyQueueSize ()
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (false), 0, "No bytes should remain in the low latency queue");
  NS_TEST_EXPECT_MSG_EQ (queue->GetLowLatencyQueueSize (true), 0, "No bytes should remain in the low latency queue");

}

class DualQueueBufferSizeTestCase : public TestCase
{
public:
  DualQueueBufferSizeTestCase ();
private:
  virtual void DoRun (void);
};

DualQueueBufferSizeTestCase::DualQueueBufferSizeTestCase ()
  : TestCase ("Check that internal queue sizes are correctly configured")
{
}

void
DualQueueBufferSizeTestCase::DoRun (void)
{
  // This test instantiates a queue and configures it in different ways
  // to check which configurations of buffer sizes take precedence.
  // After Initialize(), the queue configuration is checked.  Initialize()
  // will call DualQueueCoupledAqm::CheckConfig (); running the simulator
  // is not necessary.  A new queue object is created for each test case.
  Ptr<DualQueueCoupledAqm> queue;
  Ptr<AggregateServiceFlow> asf;
  Ptr<ServiceFlow> sf;
  Ptr<ServiceFlow> classicSf;
  Ptr<ServiceFlow> llSf;
  Ptr<DualQueueTestFilter> filter;
  int CL = 0; // classic queue index
  int LL = 1; // low latency queue index
  //
  // 1. If there is no Target Buffer parameter, derive queue sizes from
  //    the configured AMSR or MSR.  The attribute values default to the
  //    special value of "0B" which corresponds to spec defaults
  //
  queue = CreateObject<DualQueueCoupledAqm> ();
  asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("50Mbps");
  queue->SetAsf (asf);
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  queue->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (CL)->GetMaxSize (), QueueSize ("625000B"), "100ms at 50Mbps = 625000 bytes");
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (LL)->GetMaxSize (), QueueSize ("62500B"), "10ms at 50Mbps = 62500 bytes");
  //
  // 2. If there is no Target Buffer parameter, but the queue attributes 
  //    are changed from the default values of "0B", use those values
  //
  queue = CreateObject<DualQueueCoupledAqm> ();
  asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("50Mbps");
  queue->SetAsf (asf);
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  queue->SetAttribute ("ClassicBufferSize", QueueSizeValue (QueueSize ("400000B")));
  queue->SetAttribute ("LowLatencyBufferSize", QueueSizeValue (QueueSize ("5000B")));
  queue->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (CL)->GetMaxSize (), QueueSize ("400000B"), "configured to 400000 bytes");
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (LL)->GetMaxSize (), QueueSize ("5000B"), "configured to 5000 bytes");
  //
  // 3. If there is no Target Buffer parameter, and the service flows
  //    have defined MSR and PeakRate, use the AMSR for sizing the LL queue
  //    and the SF MSR or PeakRate (in this case, PeakRate because it is
  //    larger) for sizing the classic queue
  //
  queue = CreateObject<DualQueueCoupledAqm> ();
  asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("50Mbps");
  classicSf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  classicSf->m_maxSustainedRate = DataRate ("20Mbps");
  classicSf->m_peakRate = DataRate ("40Mbps");
  llSf = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
  asf->SetClassicServiceFlow (classicSf);
  asf->SetLowLatencyServiceFlow (llSf);
  queue->SetAsf (asf);
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  queue->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (CL)->GetMaxSize (), QueueSize ("500000B"), "100ms at 40Mbps = 500000 bytes");
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (LL)->GetMaxSize (), QueueSize ("62500B"), "10ms at 50Mbps = 62500 bytes");
  //
  // 4. Ensure that the LL SF buffer size does not go below the floor of
  //    20 * MaxFrameSize
  //
  queue = CreateObject<DualQueueCoupledAqm> ();
  asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("1Kbps");
  queue->SetAsf (asf);
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  queue->SetAttribute ("MaxFrameSize", UintegerValue (2000));
  queue->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (LL)->GetMaxSize (), QueueSize ("40000B"), "20 * MaxFrameSize");
  //
  // 5. Check buffer size logic for a single service flow
  //
  queue = CreateObject<DualQueueCoupledAqm> ();
  sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  sf->m_maxSustainedRate = DataRate ("10Mbps");
  sf->m_peakRate = DataRate ("16Mbps");
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  queue->SetSf (sf);
  queue->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (CL)->GetMaxSize (), QueueSize ("200000B"), "100m * 16Mbps/8 = 200000");
  //
  // 6. Check that a Target Buffer configuration overrides both attribute
  //    configuration and MSR configuration.  For this test, we need to
  //    instantiate a DOCSIS link because the target buffer configuration
  //    is handled by the CmNetDevice and CmtsNetDevice, and Initialize()
  //    will fail if the DOCSIS link is not instantiated
  //
  DocsisHelper docsis;
  Ptr<Node> cm = CreateObject<Node> ();
  Ptr<Node> cmts = CreateObject<Node> ();
  NetDeviceContainer nd = docsis.Install (cmts, cm);
  Ptr<CmNetDevice> cmNetDevice = docsis.GetUpstream (nd);
  // Check CmNetDevice
  queue = CreateObject<DualQueueCoupledAqm> ();
  asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("50Mbps");
  classicSf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  classicSf->m_maxSustainedRate = DataRate ("20Mbps");
  classicSf->m_peakRate = DataRate ("40Mbps");
  classicSf->m_targetBuffer = 327680;
  llSf = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
  llSf->m_targetBuffer = 45020;
  asf->SetClassicServiceFlow (classicSf);
  asf->SetLowLatencyServiceFlow (llSf);
  cmNetDevice->SetUpstreamAsf (asf);
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  cmNetDevice->SetQueue (queue);
  cmNetDevice->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (CL)->GetMaxSize (), QueueSize ("327680B"), "Target Buffer direct configuration");
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (LL)->GetMaxSize (), QueueSize ("45020B"), "Target Buffer direct configuration");
  // Check CmtsNetDevice
  Ptr<CmtsNetDevice> cmtsNetDevice = docsis.GetDownstream (nd);
  queue = CreateObject<DualQueueCoupledAqm> ();
  asf = CreateObject<AggregateServiceFlow> ();
  asf->m_maxSustainedRate = DataRate ("100Mbps");
  classicSf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  classicSf->m_maxSustainedRate = DataRate ("40Mbps");
  classicSf->m_peakRate = DataRate ("80Mbps");
  classicSf->m_targetBuffer = 445680;
  llSf = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
  llSf->m_targetBuffer = 50080;
  asf->SetClassicServiceFlow (classicSf);
  asf->SetLowLatencyServiceFlow (llSf);
  cmtsNetDevice->SetDownstreamAsf (asf);
  queue->SetQDelaySingleCallback (MakeCallback (&qDelaySingle));
  filter = CreateObject<DualQueueTestFilter> ();
  queue->AddPacketFilter (filter);
  cmtsNetDevice->SetQueue (queue);
  cmtsNetDevice->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (CL)->GetMaxSize (), QueueSize ("445680B"), "Target Buffer direct configuration");
  NS_TEST_EXPECT_MSG_EQ (queue->GetInternalQueue (LL)->GetMaxSize (), QueueSize ("50080B"), "Target Buffer direct configuration");

}

static class DualQueueCoupledAqmTestSuite : public TestSuite
{
public:
  DualQueueCoupledAqmTestSuite ()
    : TestSuite ("dual-queue-coupled-aqm", UNIT)
  {
    AddTestCase (new DualQIaqmThresholdsTestCase (), TestCase::QUICK);
    AddTestCase (new DualQueueByteCountsTestCase (), TestCase::QUICK);
    AddTestCase (new DualQueueBufferSizeTestCase (), TestCase::QUICK);
  }
} g_DualQCoupledAqmQueueTestSuite;
