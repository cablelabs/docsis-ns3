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

#include "dual-queue-test.h"

#include "ns3/boolean.h"
#include "ns3/cm-net-device.h"
#include "ns3/cmts-net-device.h"
#include "ns3/docsis-helper.h"
#include "ns3/double.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE("DualQCoupledPiSquaredTestSuite");

// Some constants for readability
static const bool LL = true;
static const bool CLASSIC = false;

// Dummy function to provide qDelaySingle() callback
Time
qDelaySingle()
{
    return Seconds(0);
}

class DualQIaqmThresholdsTestCase : public TestCase
{
  public:
    DualQIaqmThresholdsTestCase();

  private:
    void DoRun() override;
};

DualQIaqmThresholdsTestCase::DualQIaqmThresholdsTestCase()
    : TestCase("Check operation of IAQM ramp thresholds")
{
}

void
DualQIaqmThresholdsTestCase::DoRun()
{
    Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm>();
    Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    queue->SetAsf(asf);
    queue->SetAttribute("LgRange", UintegerValue(19));
    queue->SetAttribute("MaxTh", TimeValue(MicroSeconds(1000)));
    Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter>();
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    queue->AddPacketFilter(filter);
    queue->Initialize();
    // FLOOR = 2 * 8 * 2000 * 10^9/50000000 = 640 us
    // RANGE = 1 << 19 = 524 us
    // MINTH = max (1000 - 524, 640) = 640
    Time tolerance = MicroSeconds(1);
    TimeValue minTh;
    queue->GetAttribute("MinTh", minTh);
    NS_TEST_EXPECT_MSG_EQ_TOL(minTh.Get(),
                              MicroSeconds(640),
                              tolerance,
                              "MinTh not within tolerance");

    // Check value without FLOOR.  If MAXTH = 2000 us, MINTH = (2000 - 524) us
    queue = CreateObject<DualQueueCoupledAqm>();
    asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    queue->SetAsf(asf);
    queue->SetAttribute("LgRange", UintegerValue(19));
    queue->SetAttribute("LgRange", UintegerValue(19));
    queue->SetAttribute("MaxTh", TimeValue(MicroSeconds(2000)));
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    queue->Initialize();
    queue->GetAttribute("MinTh", minTh);
    NS_TEST_EXPECT_MSG_EQ_TOL(minTh.Get(),
                              MicroSeconds(2000 - 524),
                              tolerance,
                              "MinTh not within tolerance");
}

class DualQueueByteCountsTestCase : public TestCase
{
  public:
    DualQueueByteCountsTestCase();

  private:
    void DoRun() override;
    uint32_t m_pieQueueBytes{0};
    uint32_t m_lowLatencyBytes{0};
    uint32_t m_classicBytes{0};
    void TraceLowLatencyBytes(uint32_t oldValue, uint32_t newValue);
    void TraceClassicBytes(uint32_t oldValue, uint32_t newValue);
    void TracePieQueueBytes(uint32_t oldValue, uint32_t newValue);
};

DualQueueByteCountsTestCase::DualQueueByteCountsTestCase()
    : TestCase("Check that different queue byte counts are maintained correctly")
{
}

void
DualQueueByteCountsTestCase::TraceLowLatencyBytes(uint32_t oldValue, uint32_t newValue)
{
    m_lowLatencyBytes = newValue;
}

void
DualQueueByteCountsTestCase::TraceClassicBytes(uint32_t oldValue, uint32_t newValue)
{
    m_classicBytes = newValue;
}

void
DualQueueByteCountsTestCase::TracePieQueueBytes(uint32_t oldValue, uint32_t newValue)
{
    m_pieQueueBytes = newValue;
}

void
DualQueueByteCountsTestCase::DoRun()
{
    Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm>();
    Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    queue->SetAsf(asf);
    Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter>();
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    queue->AddPacketFilter(filter);
    queue->Initialize();
    queue->TraceConnectWithoutContext(
        "LowLatencyBytes",
        MakeCallback(&DualQueueByteCountsTestCase::TraceLowLatencyBytes, this));
    queue->TraceConnectWithoutContext(
        "ClassicBytes",
        MakeCallback(&DualQueueByteCountsTestCase::TraceClassicBytes, this));
    queue->TraceConnectWithoutContext(
        "PieQueueBytes",
        MakeCallback(&DualQueueByteCountsTestCase::TracePieQueueBytes, this));

    uint16_t protocol = 6;
    uint32_t macHeaderSize = 10; // bytes

    // Create some test addresses
    Ipv4Address src("100.2.3.4");
    Ipv4Address dest1("99.5.6.7");
    Ipv4Address dest2("102.100.8.3");
    Ipv4Address dest3("119.80.7.3");

    Ptr<Packet> p1 = Create<Packet>(82);
    Ptr<Packet> p2 = Create<Packet>(1300);
    Ptr<Packet> p3 = Create<Packet>(20); // less than Ethernet minimum

    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(false),
                          0,
                          "No MAC header should be included when false");
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(true),
                          0,
                          "No MAC header should be included when false");
    // Enqueue a non-padded packet to the low latency queue
    Ptr<DualQueueTestItem> item1 =
        Create<DualQueueTestItem>(p1, src, dest1, protocol, macHeaderSize, LL);
    queue->Enqueue(item1);
    // Check the output of the traces
    NS_TEST_EXPECT_MSG_EQ(
        m_lowLatencyBytes,
        (82 + 18 + 10),
        "Size should include packet plus mac header plus 18 for Ethernet framing");
    NS_TEST_EXPECT_MSG_EQ(m_classicBytes, 0, "No bytes should be in the classic queue yet");
    NS_TEST_EXPECT_MSG_EQ(m_pieQueueBytes, 0, "No bytes should be in the classic queue yet");
    // Check the output of the method GetLowLatencyQueueSize ()
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(false),
                          (82 + 18),
                          "No MAC header should be included when false");
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(true),
                          (82 + 18 + 10),
                          "MAC header should be included when true");
    // Dequeue and check the output of the traces
    Ptr<QueueDiscItem> dequeueItem1 = queue->LowLatencyDequeue();
    NS_TEST_EXPECT_MSG_EQ(
        dequeueItem1->GetSize(),
        (82 + 18 + 10),
        "Size should include packet plus mac header plus 18 for Ethernet framing");
    NS_TEST_EXPECT_MSG_EQ(dequeueItem1->GetPacket()->GetSize(),
                          82,
                          "Size should just be of the internal packet");
    NS_TEST_EXPECT_MSG_EQ(m_lowLatencyBytes, 0, "No bytes should remain in the low latency queue");
    // Check the output of the method GetLowLatencyQueueSize ()
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(false),
                          0,
                          "No bytes should remain in the low latency queue");
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(true),
                          0,
                          "No bytes should remain in the low latency queue");

    // Enqueue a non-padded packet to the classic queue
    Ptr<DualQueueTestItem> item2 =
        Create<DualQueueTestItem>(p2, src, dest2, protocol, macHeaderSize, CLASSIC);
    queue->Enqueue(item2);
    // Check the output of the traces
    NS_TEST_EXPECT_MSG_EQ(
        m_classicBytes,
        (1300 + 18 + 10),
        "Size should include packet plus mac header plus 18 for Ethernet framing");
    NS_TEST_EXPECT_MSG_EQ(
        m_pieQueueBytes,
        (1300 + 18),
        "Size should include packet plus 18 for Ethernet framing; no MAC header bytes");
    NS_TEST_EXPECT_MSG_EQ(m_lowLatencyBytes, 0, "No bytes should be in the low latency queue");
    // Check the output of the method GetClassicQueueSize ()
    NS_TEST_EXPECT_MSG_EQ(queue->GetClassicQueueSize(false),
                          (1300 + 18),
                          "No MAC header should be included when false");
    NS_TEST_EXPECT_MSG_EQ(queue->GetClassicQueueSize(true),
                          (1300 + 18 + 10),
                          "MAC header should be included when true");
    // Dequeue and check the output of the traces
    Ptr<QueueDiscItem> dequeueItem2 = queue->ClassicDequeue();
    NS_TEST_EXPECT_MSG_EQ(
        dequeueItem2->GetSize(),
        (1300 + 18 + 10),
        "Size should include packet plus mac header plus 18 for Ethernet framing");
    NS_TEST_EXPECT_MSG_EQ(dequeueItem2->GetPacket()->GetSize(),
                          1300,
                          "Size should just be of the internal packet");
    NS_TEST_EXPECT_MSG_EQ(m_lowLatencyBytes, 0, "No bytes should remain in the low latency queue");
    NS_TEST_EXPECT_MSG_EQ(m_pieQueueBytes, 0, "No bytes should remain in the low latency queue");
    // Check the output of the method GetClassicQueueSize ()
    NS_TEST_EXPECT_MSG_EQ(queue->GetClassicQueueSize(false),
                          0,
                          "No MAC header should be included when false");
    NS_TEST_EXPECT_MSG_EQ(queue->GetClassicQueueSize(true),
                          0,
                          "MAC header should be included when true");

    // Enqueue a padded packet (smaller than 46 bytes) to the low latency queue
    // Packet will be padded out to 64 bytes of MAC PDU
    Ptr<DualQueueTestItem> item3 =
        Create<DualQueueTestItem>(p3, src, dest3, protocol, macHeaderSize, LL);
    queue->Enqueue(item3);
    // Check the output of the traces
    NS_TEST_EXPECT_MSG_EQ(
        m_lowLatencyBytes,
        (64 + 10),
        "Size should include packet plus mac header plus 18 for Ethernet framing");
    NS_TEST_EXPECT_MSG_EQ(m_classicBytes, 0, "No bytes should be in the classic queue yet");
    NS_TEST_EXPECT_MSG_EQ(m_pieQueueBytes, 0, "No bytes should be in the classic queue yet");
    // Check the output of the method GetLowLatencyQueueSize ()
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(false),
                          (64),
                          "No MAC header should be included when false");
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(true),
                          (64 + 10),
                          "MAC header should be included when true");
    // Dequeue and check the output of the traces
    Ptr<QueueDiscItem> dequeueItem3 = queue->LowLatencyDequeue();
    NS_TEST_EXPECT_MSG_EQ(
        dequeueItem3->GetSize(),
        (64 + 10),
        "Size should include packet plus mac header plus 18 for Ethernet framing");
    NS_TEST_EXPECT_MSG_EQ(dequeueItem3->GetPacket()->GetSize(),
                          20,
                          "Size should just be of the internal packet");
    NS_TEST_EXPECT_MSG_EQ(m_lowLatencyBytes, 0, "No bytes should remain in the low latency queue");
    // Check the output of the method GetLowLatencyQueueSize ()
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(false),
                          0,
                          "No bytes should remain in the low latency queue");
    NS_TEST_EXPECT_MSG_EQ(queue->GetLowLatencyQueueSize(true),
                          0,
                          "No bytes should remain in the low latency queue");
}

class DualQueueBufferSizeTestCase : public TestCase
{
  public:
    DualQueueBufferSizeTestCase();

  private:
    void DoRun() override;
};

DualQueueBufferSizeTestCase::DualQueueBufferSizeTestCase()
    : TestCase("Check that internal queue sizes are correctly configured")
{
}

void
DualQueueBufferSizeTestCase::DoRun()
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
    queue = CreateObject<DualQueueCoupledAqm>();
    asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    queue->SetAsf(asf);
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    queue->Initialize();
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(CL)->GetMaxSize(),
                          QueueSize("625000B"),
                          "100ms at 50Mbps = 625000 bytes");
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(LL)->GetMaxSize(),
                          QueueSize("62500B"),
                          "10ms at 50Mbps = 62500 bytes");
    //
    // 2. If there is no Target Buffer parameter, but the queue attributes
    //    are changed from the default values of "0B", use those values
    //
    queue = CreateObject<DualQueueCoupledAqm>();
    asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    queue->SetAsf(asf);
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    queue->SetAttribute("ClassicBufferSize", QueueSizeValue(QueueSize("400000B")));
    queue->SetAttribute("LowLatencyConfigBufferSize", QueueSizeValue(QueueSize("5000B")));
    queue->Initialize();
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(CL)->GetMaxSize(),
                          QueueSize("400000B"),
                          "configured to 400000 bytes");
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(LL)->GetMaxSize(),
                          QueueSize("5000B"),
                          "configured to 5000 bytes");
    //
    // 3. If there is no Target Buffer parameter, and the service flows
    //    have defined MSR and PeakRate, use the AMSR for sizing the LL queue
    //    and the SF MSR or PeakRate (in this case, PeakRate because it is
    //    larger) for sizing the classic queue
    //
    queue = CreateObject<DualQueueCoupledAqm>();
    asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    classicSf = CreateObject<ServiceFlow>(CLASSIC_SFID);
    classicSf->m_maxSustainedRate = DataRate("20Mbps");
    classicSf->m_peakRate = DataRate("40Mbps");
    llSf = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
    asf->SetClassicServiceFlow(classicSf);
    asf->SetLowLatencyServiceFlow(llSf);
    queue->SetAsf(asf);
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    queue->Initialize();
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(CL)->GetMaxSize(),
                          QueueSize("500000B"),
                          "100ms at 40Mbps = 500000 bytes");
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(LL)->GetMaxSize(),
                          QueueSize("62500B"),
                          "10ms at 50Mbps = 62500 bytes");
    //
    // 4. Ensure that the LL SF buffer size does not go below the floor of
    //    20 * MaxFrameSize
    //
    queue = CreateObject<DualQueueCoupledAqm>();
    asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("1Kbps");
    queue->SetAsf(asf);
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    queue->SetAttribute("MaxFrameSize", UintegerValue(2000));
    queue->Initialize();
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(LL)->GetMaxSize(),
                          QueueSize("40000B"),
                          "20 * MaxFrameSize");
    //
    // 5. Check buffer size logic for a single service flow
    //
    queue = CreateObject<DualQueueCoupledAqm>();
    sf = CreateObject<ServiceFlow>(CLASSIC_SFID);
    sf->m_maxSustainedRate = DataRate("10Mbps");
    sf->m_peakRate = DataRate("16Mbps");
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    queue->SetSf(sf);
    queue->Initialize();
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(CL)->GetMaxSize(),
                          QueueSize("200000B"),
                          "100m * 16Mbps/8 = 200000");
    //
    // 6. Check that a Target Buffer configuration overrides both attribute
    //    configuration and MSR configuration.  For this test, we need to
    //    instantiate a DOCSIS link because the target buffer configuration
    //    is handled by the CmNetDevice and CmtsNetDevice, and Initialize()
    //    will fail if the DOCSIS link is not instantiated
    //
    DocsisHelper docsis;
    Ptr<Node> cm = CreateObject<Node>();
    Ptr<Node> cmts = CreateObject<Node>();
    NetDeviceContainer nd = docsis.Install(cmts, cm);
    Ptr<CmNetDevice> cmNetDevice = docsis.GetUpstream(nd);
    // Check CmNetDevice
    queue = CreateObject<DualQueueCoupledAqm>();
    asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    classicSf = CreateObject<ServiceFlow>(CLASSIC_SFID);
    classicSf->m_maxSustainedRate = DataRate("20Mbps");
    classicSf->m_peakRate = DataRate("40Mbps");
    classicSf->m_targetBuffer = 327680;
    llSf = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
    llSf->m_targetBuffer = 45020;
    asf->SetClassicServiceFlow(classicSf);
    asf->SetLowLatencyServiceFlow(llSf);
    cmNetDevice->SetUpstreamAsf(asf);
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    cmNetDevice->SetQueue(queue);
    cmNetDevice->Initialize();
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(CL)->GetMaxSize(),
                          QueueSize("327680B"),
                          "Target Buffer direct configuration");
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(LL)->GetMaxSize(),
                          QueueSize("45020B"),
                          "Target Buffer direct configuration");
    // Check CmtsNetDevice
    Ptr<CmtsNetDevice> cmtsNetDevice = docsis.GetDownstream(nd);
    queue = CreateObject<DualQueueCoupledAqm>();
    asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("100Mbps");
    classicSf = CreateObject<ServiceFlow>(CLASSIC_SFID);
    classicSf->m_maxSustainedRate = DataRate("40Mbps");
    classicSf->m_peakRate = DataRate("80Mbps");
    classicSf->m_targetBuffer = 445680;
    llSf = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
    llSf->m_targetBuffer = 50080;
    asf->SetClassicServiceFlow(classicSf);
    asf->SetLowLatencyServiceFlow(llSf);
    cmtsNetDevice->SetDownstreamAsf(asf);
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    cmtsNetDevice->SetQueue(queue);
    cmtsNetDevice->Initialize();
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(CL)->GetMaxSize(),
                          QueueSize("445680B"),
                          "Target Buffer direct configuration");
    NS_TEST_EXPECT_MSG_EQ(queue->GetInternalQueue(LL)->GetMaxSize(),
                          QueueSize("50080B"),
                          "Target Buffer direct configuration");
}

class DualQueueVirtualQueueTestCase : public TestCase
{
  public:
    DualQueueVirtualQueueTestCase(bool useLowLatencyServiceFlow);

  private:
    void DoRun() override;
    bool m_useLowLatencyServiceFlow{false}; //!< whether to use LL SF
    void TraceLowLatencyQueueDelay(Time delay, uint32_t queueSize);
    void TraceVirtualQueueDelay(Time delay, uint32_t queueSize);
    Ptr<DualQueueCoupledAqm> m_queue;
    void Enqueue();
    void Dequeue();

    // Test event recording
    struct TestEvent
    {
        uint32_t m_time;
        uint32_t m_delay;

        TestEvent(uint32_t t, uint32_t d)
            : m_time(t),
              m_delay(d)
        {
        }

        bool operator!=(const TestEvent& b) const
        {
            return (m_time != b.m_time || m_delay != b.m_delay);
        }
    };

    std::list<TestEvent> m_expectedVirtualEvents;  //!< Expected events
    std::list<TestEvent> m_observedVirtualEvents;  //!< Observed events
    std::list<TestEvent> m_expectedStandardEvents; //!< Expected events
    std::list<TestEvent> m_observedStandardEvents; //!< Observed events
};

DualQueueVirtualQueueTestCase::DualQueueVirtualQueueTestCase(bool useLowLatencyServiceFlow)
    : TestCase("Check that virtual queue is maintained correctly"),
      m_useLowLatencyServiceFlow(useLowLatencyServiceFlow)
{
}

void
DualQueueVirtualQueueTestCase::TraceLowLatencyQueueDelay(Time delay, uint32_t queueSize)
{
    m_observedStandardEvents.emplace_back(Simulator::Now().GetMicroSeconds(),
                                          delay.GetMicroSeconds());
}

void
DualQueueVirtualQueueTestCase::TraceVirtualQueueDelay(Time delay, uint32_t queueSize)
{
    m_observedVirtualEvents.emplace_back(Simulator::Now().GetMicroSeconds(),
                                         delay.GetMicroSeconds());
}

void
DualQueueVirtualQueueTestCase::Enqueue()
{
    uint16_t protocol = 6;
    uint32_t macHeaderSize = 10; // bytes
    Ipv4Address src("100.2.3.4");
    Ipv4Address dest1("99.5.6.7");
    Ptr<Packet> p1 = Create<Packet>(1000);
    Ptr<DualQueueTestItem> item1 =
        Create<DualQueueTestItem>(p1, src, dest1, protocol, macHeaderSize, LL);
    m_queue->Enqueue(item1);
}

void
DualQueueVirtualQueueTestCase::Dequeue()
{
    m_queue->Dequeue();
}

void
DualQueueVirtualQueueTestCase::DoRun()
{
    Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm>();
    queue->SetAttribute("VirtualQueueResetInterval", TimeValue(Seconds(0)));
    Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    if (m_useLowLatencyServiceFlow)
    {
        Ptr<ServiceFlow> sf = CreateObject<ServiceFlow>(CLASSIC_SFID);
        sf->m_maxSustainedRate = DataRate("30Mbps");
        asf->SetClassicServiceFlow(sf);
        sf = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
        sf->m_maxSustainedRate = DataRate("20Mbps");
        asf->SetLowLatencyServiceFlow(sf);
    }
    queue->SetAsf(asf);
    Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter>();
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    queue->AddPacketFilter(filter);
    queue->Initialize();
    queue->TraceConnectWithoutContext(
        "LowLatencyQueueDelay",
        MakeCallback(&DualQueueVirtualQueueTestCase::TraceLowLatencyQueueDelay, this));
    queue->TraceConnectWithoutContext(
        "VirtualQueueDelay",
        MakeCallback(&DualQueueVirtualQueueTestCase::TraceVirtualQueueDelay, this));
    m_queue = queue;

    // Schedule some events and some expected trace results
    // Enqueue a low-latency packet at 1 second = 1e6 microseconds
    Simulator::Schedule(MicroSeconds(1000000), &DualQueueVirtualQueueTestCase::Enqueue, this);
    // This will trigger an estimate of the virtual queue that will be
    // caught by the trace source, as well as an actual queue delay estimate
    //
    if (!m_useLowLatencyServiceFlow)
    {
        // The virtual queue will be 1018 bytes = 8144 bits, and the overall
        // virtual departure rate = 50 Mbps * (230/256) = 44921875 bps, because
        // we did not specify a LL service flow MSR (and are using the AMSR)
        // This yields 181.293 us delay estimate, which rounds down to 181 us
        m_expectedVirtualEvents.emplace_back(1000000, 181);
        // The actual queue delay estimate is for 1028 bytes (includes 10 bytes
        // of MAC header) and uses the AMSR (50000000bps), yielding 164 us
        m_expectedStandardEvents.emplace_back(1000000, 164);
    }
    else
    {
        // The virtual queue will be 1018 bytes = 8144 bits, and the overall
        // virtual departure rate = 20 Mbps, because we use the lower of
        // weighted AMSR and LL MSR
        // This yields 407.2 us delay estimate, which rounds down to 407 us
        m_expectedVirtualEvents.emplace_back(1000000, 407);
        // The actual queue delay estimate is for 1028 bytes (includes 10 bytes
        // of MAC header) and uses the MSR (20000000bps), yielding 411.2 us
        m_expectedStandardEvents.emplace_back(1000000, 411);
    }

    // Schedule another packet enqueue 1ms later.
    Simulator::Schedule(MicroSeconds(1001000), &DualQueueVirtualQueueTestCase::Enqueue, this);
    if (!m_useLowLatencyServiceFlow)
    {
        // By this point in time, the virtual queue will have fully drained, but
        // we will add back another 181us
        m_expectedVirtualEvents.emplace_back(1001000, 181);
        // However, there was no dequeue, so the actual delay estimate will be
        // double the previous (164*2) = 328us
        m_expectedStandardEvents.emplace_back(1001000, 328);
    }
    else
    {
        // By this point in time, the virtual queue will have fully drained, but
        // we will add back another 407us
        m_expectedVirtualEvents.emplace_back(1001000, 407);
        // However, there was no dequeue, so the actual delay estimate will be
        // double the previous (411*2) = 822us
        m_expectedStandardEvents.emplace_back(1001000, 822);
    }

    // Schedule another packet enqueue 1us later.
    Simulator::Schedule(MicroSeconds(1001001), &DualQueueVirtualQueueTestCase::Enqueue, this);
    if (!m_useLowLatencyServiceFlow)
    {
        // Only 1us of drain of the previous packet should have occurred.  This is
        // only 6 bytes.  We expect the virtual queue to be basically double
        // in size (within integer rounding at us resolution), or 361 us
        m_expectedVirtualEvents.emplace_back(1001001, 361);
        // Again, there was no dequeue, so the actual delay estimate will add
        // another 164 us, for 492 us (with fractional parts, it rounds to 493us)
        m_expectedStandardEvents.emplace_back(1001001, 493);
    }
    else
    {
        // As above, only 1us of drain of the previous packet should
        // have occurred, almost doubling of 407 us
        m_expectedVirtualEvents.emplace_back(1001001, 813);
        // Again, there was no dequeue, so the actual delay estimate will add
        // another 164 us, for 492 us (with fractional parts, it rounds to 493us)
        m_expectedStandardEvents.emplace_back(1001001, 1233);
    }

    // Dequeue does not trigger the traces
    Simulator::Schedule(MicroSeconds(1001002), &DualQueueVirtualQueueTestCase::Dequeue, this);
    Simulator::Schedule(MicroSeconds(1050000), &DualQueueVirtualQueueTestCase::Dequeue, this);
    Simulator::Schedule(MicroSeconds(1150000), &DualQueueVirtualQueueTestCase::Dequeue, this);

    // Now enqueue and dequeue in rapid succession.  If timed reset is not
    // enabled (i.e. if the virtual queue is not checked for alignment upon
    // dequeue but instead upon timed intervals), the virtual queue will
    // reset.
    Simulator::Schedule(MicroSeconds(1200000), &DualQueueVirtualQueueTestCase::Enqueue, this);
    if (!m_useLowLatencyServiceFlow)
    {
        m_expectedVirtualEvents.emplace_back(1200000, 181);
        m_expectedStandardEvents.emplace_back(1200000, 164);
    }
    else
    {
        m_expectedVirtualEvents.emplace_back(1200000, 407);
        m_expectedStandardEvents.emplace_back(1200000, 411);
    }
    Simulator::Schedule(MicroSeconds(1200001), &DualQueueVirtualQueueTestCase::Dequeue, this);
    // Now enqueue again.  The virtual queue should be empty for this enqueue.
    Simulator::Schedule(MicroSeconds(1200002), &DualQueueVirtualQueueTestCase::Enqueue, this);
    if (!m_useLowLatencyServiceFlow)
    {
        m_expectedVirtualEvents.emplace_back(1200002, 181);
        m_expectedStandardEvents.emplace_back(1200002, 164);
    }
    else
    {
        m_expectedVirtualEvents.emplace_back(1200002, 407);
        m_expectedStandardEvents.emplace_back(1200002, 411);
    }

    Simulator::Stop(Seconds(2));
    Simulator::Run();

#ifdef VERBOSE_DIALOG
    // Define this to print out the observed events, if needed for debugging
    // or to generate new expected events.
    for (auto it = m_observedVirtualEvents.begin(); it != m_observedVirtualEvents.end(); it++)
    {
        std::cout << std::fixed << "  m_expectedVirtualEvents.push_back (TestEvent (" << it->m_time
                  << ", " << it->m_delay << "));" << std::endl;
    }
    for (auto it = m_observedStandardEvents.begin(); it != m_observedStandardEvents.end(); it++)
    {
        std::cout << std::fixed << "  m_expectedStandardEvents.push_back (TestEvent (" << it->m_time
                  << ", " << it->m_delay << "));" << std::endl;
    }
#endif

    // Check that all observed events match expected events
    NS_TEST_ASSERT_MSG_EQ(m_observedStandardEvents.size(),
                          m_expectedStandardEvents.size(),
                          "Did not observe all expected standard events");
    auto it1 = m_observedStandardEvents.begin();
    auto it2 = m_expectedStandardEvents.begin();
    while (it1 != m_observedStandardEvents.end() && it2 != m_expectedStandardEvents.end())
    {
        if (*it1 != *it2)
        {
            NS_TEST_ASSERT_MSG_EQ(it1->m_time, it2->m_time, "Event times not equal");
            NS_TEST_ASSERT_MSG_EQ(it1->m_delay, it2->m_delay, "Delays not equal");
        }
        it1++;
        it2++;
    }
    NS_TEST_ASSERT_MSG_EQ(m_observedVirtualEvents.size(),
                          m_expectedVirtualEvents.size(),
                          "Did not observe all expected virtual queue events");
    it1 = m_observedVirtualEvents.begin();
    it2 = m_expectedVirtualEvents.begin();
    while (it1 != m_observedVirtualEvents.end() && it2 != m_expectedVirtualEvents.end())
    {
        if (*it1 != *it2)
        {
            NS_TEST_ASSERT_MSG_EQ(it1->m_time, it2->m_time, "Event times not equal");
            NS_TEST_ASSERT_MSG_EQ(it1->m_delay, it2->m_delay, "Delays not equal");
        }
        it1++;
        it2++;
    }
    Simulator::Destroy();
}

class DualQueueTimedVirtualQueueTestCase : public TestCase
{
  public:
    DualQueueTimedVirtualQueueTestCase();

  private:
    void DoRun() override;
    void TraceLowLatencyQueueDelay(Time delay, uint32_t queueSize);
    void TraceVirtualQueueDelay(Time delay, uint32_t queueSize);
    Ptr<DualQueueCoupledAqm> m_queue;
    void Enqueue();
    void Dequeue();

    // Test event recording
    struct TestEvent
    {
        uint32_t m_time;
        uint32_t m_delay;

        TestEvent(uint32_t t, uint32_t d)
            : m_time(t),
              m_delay(d)
        {
        }

        bool operator!=(const TestEvent& b) const
        {
            return (m_time != b.m_time || m_delay != b.m_delay);
        }
    };

    std::list<TestEvent> m_expectedVirtualEvents;  //!< Expected events
    std::list<TestEvent> m_observedVirtualEvents;  //!< Observed events
    std::list<TestEvent> m_expectedStandardEvents; //!< Expected events
    std::list<TestEvent> m_observedStandardEvents; //!< Observed events
};

DualQueueTimedVirtualQueueTestCase::DualQueueTimedVirtualQueueTestCase()
    : TestCase(
          "Check that virtual queue is maintained correctly when timed drain and reset are used")
{
}

void
DualQueueTimedVirtualQueueTestCase::TraceLowLatencyQueueDelay(Time delay, uint32_t queueSize)
{
    m_observedStandardEvents.emplace_back(Simulator::Now().GetMicroSeconds(),
                                          delay.GetMicroSeconds());
}

void
DualQueueTimedVirtualQueueTestCase::TraceVirtualQueueDelay(Time delay, uint32_t queueSize)
{
    m_observedVirtualEvents.emplace_back(Simulator::Now().GetMicroSeconds(),
                                         delay.GetMicroSeconds());
}

void
DualQueueTimedVirtualQueueTestCase::Enqueue()
{
    uint16_t protocol = 6;
    uint32_t macHeaderSize = 10; // bytes
    Ipv4Address src("100.2.3.4");
    Ipv4Address dest1("99.5.6.7");
    Ptr<Packet> p1 = Create<Packet>(1000);
    Ptr<DualQueueTestItem> item1 =
        Create<DualQueueTestItem>(p1, src, dest1, protocol, macHeaderSize, LL);
    m_queue->Enqueue(item1);
}

void
DualQueueTimedVirtualQueueTestCase::Dequeue()
{
    m_queue->Dequeue();
}

void
DualQueueTimedVirtualQueueTestCase::DoRun()
{
    Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm>();
    Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow>();
    asf->m_maxSustainedRate = DataRate("50Mbps");
    queue->SetAttribute("VirtualQueueResetInterval", TimeValue(MilliSeconds(1)));
    queue->SetAsf(asf);
    Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter>();
    queue->SetQDelaySingleCallback(MakeCallback(&qDelaySingle));
    queue->AddPacketFilter(filter);
    queue->Initialize();
    queue->TraceConnectWithoutContext(
        "LowLatencyQueueDelay",
        MakeCallback(&DualQueueTimedVirtualQueueTestCase::TraceLowLatencyQueueDelay, this));
    queue->TraceConnectWithoutContext(
        "VirtualQueueDelay",
        MakeCallback(&DualQueueTimedVirtualQueueTestCase::TraceVirtualQueueDelay, this));
    m_queue = queue;

    // Schedule some events and some expected trace results
    // Enqueue a low-latency packet at 1 second = 1e6 microseconds
    Simulator::Schedule(MicroSeconds(1000000), &DualQueueTimedVirtualQueueTestCase::Enqueue, this);
    // This will trigger an estimate of the virtual queue that will be
    // caught by the trace source, as well as an actual queue delay estimate
    //
    // The virtual queue will be 1018 bytes = 8144 bits, and the overall
    // virtual departure rate = 50 Mbps * (230/256) = 44921875 bps, because
    // we did not specify a LL service flow MSR (and are using the AMSR)
    // This yields 181.293 us delay estimate, which rounds down to 181 us
    m_expectedVirtualEvents.emplace_back(1000000, 181);
    // The actual queue delay estimate is for 1028 bytes (includes 10 bytes
    // of MAC header) and uses the AMSR (50000000bps), yielding 164 us
    m_expectedStandardEvents.emplace_back(1000000, 164);

    // Schedule another packet enqueue 1ms later.
    Simulator::Schedule(MicroSeconds(1001000), &DualQueueTimedVirtualQueueTestCase::Enqueue, this);
    // By this point in time, the virtual queue will have fully drained, but
    // we will add back another 181us
    m_expectedVirtualEvents.emplace_back(1001000, 181);
    // However, there was no dequeue, so the actual delay estimate will be
    // double the previous (164*2) = 328us
    m_expectedStandardEvents.emplace_back(1001000, 328);

    // Schedule another packet enqueue 1us later.
    Simulator::Schedule(MicroSeconds(1001001), &DualQueueTimedVirtualQueueTestCase::Enqueue, this);
    // Only 1us of drain of the previous packet should have occurred.  This is
    // only 6 bytes.  We expect the virtual queue to be basically double
    // in size (within integer rounding at us resolution), or 361 us
    m_expectedVirtualEvents.emplace_back(1001001, 361);
    // Again, there was no dequeue, so the actual delay estimate will add
    // another 164 us, for 492 us (with fractional parts, it rounds to 493us)
    m_expectedStandardEvents.emplace_back(1001001, 493);

    // Dequeue does not trigger the traces, because by the time the physical
    // queue has emptied, the virtual queue will have already drained
    Simulator::Schedule(MicroSeconds(1001002), &DualQueueTimedVirtualQueueTestCase::Dequeue, this);
    Simulator::Schedule(MicroSeconds(1050000), &DualQueueTimedVirtualQueueTestCase::Dequeue, this);
    Simulator::Schedule(MicroSeconds(1150000), &DualQueueTimedVirtualQueueTestCase::Dequeue, this);

    // Now enqueue and dequeue in rapid succession.
    // traced.
    Simulator::Schedule(MicroSeconds(1200000), &DualQueueTimedVirtualQueueTestCase::Enqueue, this);
    m_expectedVirtualEvents.emplace_back(1200000, 181);
    m_expectedStandardEvents.emplace_back(1200000, 164);
    Simulator::Schedule(MicroSeconds(1200001), &DualQueueTimedVirtualQueueTestCase::Dequeue, this);
    // Unlike the previous test case that did not set the virtual queue
    // reset interval, the above dequeue will empty the virtual queue.  Here,
    // the next scheduled time is 1ms later, by which time the virtual queue
    // will have drained.

    // Now enqueue again.  Unlike the previous test case, the virtual queue
    // should not be empty for this enqueue.  The virtual queue and its delay
    // estimate will be larger than that of the actual queue.
    Simulator::Schedule(MicroSeconds(1200002), &DualQueueTimedVirtualQueueTestCase::Enqueue, this);
    m_expectedVirtualEvents.emplace_back(1200002, 360);
    m_expectedStandardEvents.emplace_back(1200002, 164);

    Simulator::Stop(Seconds(2));
    Simulator::Run();

#ifdef VERBOSE_DIALOG
    // Define this to print out the observed events, if needed for debugging
    // or to generate new expected events.
    for (auto it = m_observedVirtualEvents.begin(); it != m_observedVirtualEvents.end(); it++)
    {
        std::cout << std::fixed << "  m_expectedVirtualEvents.push_back (TestEvent (" << it->m_time
                  << ", " << it->m_delay << "));" << std::endl;
    }
    for (auto it = m_observedStandardEvents.begin(); it != m_observedStandardEvents.end(); it++)
    {
        std::cout << std::fixed << "  m_expectedStandardEvents.push_back (TestEvent (" << it->m_time
                  << ", " << it->m_delay << "));" << std::endl;
    }
#endif

    // Check that all observed events match expected events
    NS_TEST_ASSERT_MSG_EQ(m_observedStandardEvents.size(),
                          m_expectedStandardEvents.size(),
                          "Did not observe all expected standard events");
    auto it1 = m_observedStandardEvents.begin();
    auto it2 = m_expectedStandardEvents.begin();
    while (it1 != m_observedStandardEvents.end() && it2 != m_expectedStandardEvents.end())
    {
        if (*it1 != *it2)
        {
            NS_TEST_ASSERT_MSG_EQ(it1->m_time, it2->m_time, "Event times not equal");
            NS_TEST_ASSERT_MSG_EQ(it1->m_delay, it2->m_delay, "Delays not equal");
        }
        it1++;
        it2++;
    }
    NS_TEST_ASSERT_MSG_EQ(m_observedVirtualEvents.size(),
                          m_expectedVirtualEvents.size(),
                          "Did not observe all expected virtual queue events");
    it1 = m_observedVirtualEvents.begin();
    it2 = m_expectedVirtualEvents.begin();
    while (it1 != m_observedVirtualEvents.end() && it2 != m_expectedVirtualEvents.end())
    {
        if (*it1 != *it2)
        {
            NS_TEST_ASSERT_MSG_EQ(it1->m_time, it2->m_time, "Event times not equal");
            NS_TEST_ASSERT_MSG_EQ(it1->m_delay, it2->m_delay, "Delays not equal");
        }
        it1++;
        it2++;
    }
    Simulator::Destroy();
}

static class DualQueueCoupledAqmTestSuite : public TestSuite
{
  public:
    DualQueueCoupledAqmTestSuite()
        : TestSuite("dual-queue-coupled-aqm", UNIT)
    {
        AddTestCase(new DualQIaqmThresholdsTestCase(), TestCase::QUICK);
        AddTestCase(new DualQueueByteCountsTestCase(), TestCase::QUICK);
        AddTestCase(new DualQueueBufferSizeTestCase(), TestCase::QUICK);
        // Boolean value controls whether to configure a MSR on a LL SF
        // Argument controls whether to use the LL SF MSR instead of the AMSR
        // for rate estimates; false means use the AMSR
#if 0
    XXX need to recalibrate with VQ changes
    AddTestCase (new DualQueueVirtualQueueTestCase (false), TestCase::QUICK);
    AddTestCase (new DualQueueVirtualQueueTestCase (true), TestCase::QUICK);
    // Check the timed drain and reset behavior; this has some different
    // traces for certain enqueue/dequeue sequences
    AddTestCase (new DualQueueTimedVirtualQueueTestCase (), TestCase::QUICK);
#endif
    }
} g_DualQCoupledAqmQueueTestSuite;
