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
 *
 */

#include "dual-queue-test.h"

#include "ns3/double.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/log.h"
#include "ns3/microflow-descriptor.h"
#include "ns3/queue-protection.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/test.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/uinteger.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE("QueueProtectionTestSuite");

// Some constants for readability
static const bool LL = true;
static const bool CLASSIC = false;

// dummy function to use as a callback
Time
QDelaySingle()
{
    return Seconds(0);
}

// General class to collect test outcomes
class QueueProtectionTestOutcome
{
  public:
    QueueProtectionTestOutcome(uint32_t f, uint32_t qs, int64_t l, int64_t s, int64_t t);
    Time m_timestamp;
    uint32_t m_flowId;
    uint32_t m_qsize;
    int64_t m_latency;
    int64_t m_score;
    int64_t m_threshold;
};

QueueProtectionTestOutcome::QueueProtectionTestOutcome(uint32_t f,
                                                       uint32_t qs,
                                                       int64_t l,
                                                       int64_t s,
                                                       int64_t t)
    : m_flowId(f),
      m_qsize(qs),
      m_latency(l),
      m_score(s),
      m_threshold(t)
{
    m_timestamp = Simulator::Now();
}

class QueueProtectionBasicTestCase : public TestCase
{
  public:
    QueueProtectionBasicTestCase();

  private:
    void DoRun() override;
    void Enqueue(Ptr<DualQueueCoupledAqm> queue,
                 uint32_t size,
                 uint32_t nPkt,
                 StringValue trafficType);
    void EnqueueWithDelay(Ptr<DualQueueCoupledAqm> queue,
                          uint32_t size,
                          uint32_t nPkt,
                          StringValue trafficType);
    void Dequeue(Ptr<DualQueueCoupledAqm> queue, uint32_t nPkt);
    void DequeueWithDelay(Ptr<DualQueueCoupledAqm> queue, double delay, uint32_t nPkt);

    void BucketScoreTrace(uint32_t flowId, uint32_t bucketId, int64_t bucketDepth);
    void SuccessTrace(Ptr<const QueueDiscItem> item,
                      uint32_t flowId,
                      uint32_t qsize,
                      int64_t qDelay,
                      int64_t qLscore,
                      int64_t criticalQl,
                      int64_t criticalQlProduct,
                      uint16_t bckt_id);
    void SanctionTrace(Ptr<const QueueDiscItem> item,
                       uint32_t flowId,
                       uint32_t qsize,
                       int64_t qDelay,
                       int64_t qLscore,
                       int64_t criticalQl,
                       int64_t criticalQlProduct,
                       uint16_t bckt_id);
    void FlowTrace(uint32_t flowId, Uflw flow);

    static Uflw GenerateHash32(Ptr<const QueueDiscItem> item, uint32_t perturbation);

    std::vector<QueueProtectionTestOutcome> m_successOutcomes;
    std::vector<QueueProtectionTestOutcome> m_sanctionOutcomes;
};

Uflw
QueueProtectionBasicTestCase::GenerateHash32(Ptr<const QueueDiscItem> item, uint32_t perturbation)
{
    Ipv4Address ipv4Address = Ipv4Address::ConvertFrom(item->GetAddress());

    /* serialize the 5-tuple and the perturbation in buf */
    uint8_t buf[17];
    ipv4Address.Serialize(buf);
    ipv4Address.Serialize(buf + 4);
    buf[8] = static_cast<uint8_t>(item->GetProtocol());
    buf[9] = 0;
    buf[10] = 0;
    buf[11] = 0;
    buf[12] = 0;
    buf[13] = (perturbation >> 24) & 0xff;
    buf[14] = (perturbation >> 16) & 0xff;
    buf[15] = (perturbation >> 8) & 0xff;
    buf[16] = perturbation & 0xff;

    // murmur3 hash in ns-3 core module (hash-murmur3.cc)
    uint32_t hash = Hash32((char*)buf, 17);

    NS_LOG_DEBUG("Generate hash for 5-tuple src: " << ipv4Address << " dst: " << ipv4Address
                                                   << " proto: " << (uint16_t)item->GetProtocol()
                                                   << " hash: " << hash);
    Uflw h;
    h.source = ipv4Address;
    h.destination = ipv4Address;
    h.protocol = static_cast<uint8_t>(item->GetProtocol());
    h.sourcePort = 0;
    h.destinationPort = 0;
    h.h32 = hash;
    return h;
}

QueueProtectionBasicTestCase::QueueProtectionBasicTestCase()
    : TestCase("Sanity check on the DualQ Coupled PI Square queue disc implementation")
{
}

void
QueueProtectionBasicTestCase::DoRun()
{
    uint32_t pktSize = 1000; // bytes
    uint32_t qSizePkts = 50;

    Ptr<DualQueueCoupledAqm> queue = CreateObject<DualQueueCoupledAqm>();
    Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow>();
    queue->SetAsf(asf);
    Ptr<DualQueueTestFilter> filter = CreateObject<DualQueueTestFilter>();
    queue->AddPacketFilter(filter);
    queue->SetQDelaySingleCallback(MakeCallback(&QDelaySingle));
    queue->SetAttribute("MaxSize",
                        QueueSizeValue(QueueSize(QueueSizeUnit::BYTES, qSizePkts * pktSize)));

    Ptr<QueueProtection> queueProtection = CreateObject<QueueProtection>();
    queueProtection->SetHashCallback(MakeCallback(&QueueProtectionBasicTestCase::GenerateHash32));
    queueProtection->SetQueue(queue);
    queue->SetQueueProtection(queueProtection);

    // configure QueueProtection
    queueProtection->SetAttribute("ConfigureQpLatencyThreshold", BooleanValue(true));
    queueProtection->SetAttribute("QpLatencyThreshold", IntegerValue(1000));

    // setup callbacks
    queueProtection->TraceConnectWithoutContext(
        "Flow",
        MakeCallback(&QueueProtectionBasicTestCase::FlowTrace, this));
    queueProtection->TraceConnectWithoutContext(
        "PenaltyScoreUpdate",
        MakeCallback(&QueueProtectionBasicTestCase::BucketScoreTrace, this));
    queueProtection->TraceConnectWithoutContext(
        "Success",
        MakeCallback(&QueueProtectionBasicTestCase::SuccessTrace, this));
    queueProtection->TraceConnectWithoutContext(
        "Sanction",
        MakeCallback(&QueueProtectionBasicTestCase::SanctionTrace, this));

    queue->Initialize();

    NS_LOG_DEBUG("Test 1:  Check a few enqueue operations");
    Ipv4Address src("10.1.2.3");
    Ipv4Address dest1("10.0.0.1");
    Ipv4Address dest2("10.0.0.2");
    Ipv4Address dest3("10.0.0.3");

    // Enqueue two packets that should match on one flow-id, and a third
    // that should be given another flow-id

    Ptr<Packet> p1;
    p1 = Create<Packet>(pktSize);
    queue->Enqueue(Create<DualQueueTestItem>(p1, src, dest1, 0, 0, LL));

    Ptr<Packet> p2;
    p2 = Create<Packet>(pktSize);
    queue->Enqueue(Create<DualQueueTestItem>(p2, src, dest1, 0, 0, LL));

    Ptr<Packet> p3;
    p3 = Create<Packet>(pktSize);
    // Different bucket
    queue->Enqueue(Create<DualQueueTestItem>(p3, src, dest2, 0, 0, LL));

    Ptr<Packet> p4;
    p4 = Create<Packet>(pktSize);
    // Different bucket
    queue->Enqueue(Create<DualQueueTestItem>(p4, src, dest3, 0, 0, LL));

    Ptr<Packet> p5;
    p5 = Create<Packet>(pktSize);
    // Different bucket
    queue->Enqueue(Create<DualQueueTestItem>(p5, src, dest3, 0, 0, LL));

    // Dequeue
    Ptr<QueueDiscItem> item;
    item = queue->Dequeue();
    Ptr<QueueDiscItem> item2;
    item2 = queue->Dequeue();
    Ptr<QueueDiscItem> item3;
    item3 = queue->Dequeue();
    Ptr<QueueDiscItem> item4;
    item4 = queue->Dequeue();
    Ptr<QueueDiscItem> item5;
    item5 = queue->Dequeue();
    NS_TEST_EXPECT_MSG_EQ(queue->GetCurrentSize().GetValue(), 0, "Check zero packets in queue");

    // Check test output
    NS_TEST_EXPECT_MSG_EQ(m_successOutcomes.size(), 5, "Three successes should be observed");
    NS_TEST_EXPECT_MSG_EQ(m_sanctionOutcomes.size(), 0, "No sanctions should be observed");
    Simulator::Destroy();
}

void
QueueProtectionBasicTestCase::Enqueue(Ptr<DualQueueCoupledAqm> queue,
                                      uint32_t size,
                                      uint32_t nPkt,
                                      StringValue trafficType)
{
    Address src;
    Address dest;
    for (uint32_t i = 0; i < nPkt; i++)
    {
        if (trafficType.Get() == "LL")
        {
            NS_LOG_DEBUG("At time " << Simulator::Now().GetSeconds() << " enqueue packet size "
                                    << size << " of type LL");
            queue->Enqueue(Create<DualQueueTestItem>(Create<Packet>(size), src, dest, 0, 0, LL));
        }
        else if (trafficType.Get() == "Classic")
        {
            NS_LOG_DEBUG("At time " << Simulator::Now().GetSeconds() << " enqueue packet size "
                                    << size << " of type CLASSIC");
            queue->Enqueue(
                Create<DualQueueTestItem>(Create<Packet>(size), src, dest, 0, 0, CLASSIC));
        }
    }
}

void
QueueProtectionBasicTestCase::EnqueueWithDelay(Ptr<DualQueueCoupledAqm> queue,
                                               uint32_t size,
                                               uint32_t nPkt,
                                               StringValue trafficType)
{
    Address dest;
    double delay = 0.01; // enqueue packets with incremental delay of 10 ms
    for (uint32_t i = 0; i < nPkt; i++)
    {
        Simulator::Schedule(Time(Seconds(i * delay)),
                            &QueueProtectionBasicTestCase::Enqueue,
                            this,
                            queue,
                            size,
                            1,
                            trafficType);
    }
}

void
QueueProtectionBasicTestCase::Dequeue(Ptr<DualQueueCoupledAqm> queue, uint32_t nPkt)
{
    for (uint32_t i = 0; i < nPkt; i++)
    {
        Ptr<QueueDiscItem> item = queue->Dequeue();
        NS_LOG_DEBUG("At time " << Simulator::Now().GetSeconds() << " dequeue packet size "
                                << item->GetSize());
    }
}

void
QueueProtectionBasicTestCase::DequeueWithDelay(Ptr<DualQueueCoupledAqm> queue,
                                               double delay,
                                               uint32_t nPkt)
{
    for (uint32_t i = 0; i < nPkt; i++)
    {
        Simulator::Schedule(Time(Seconds((i + 1) * delay)),
                            &QueueProtectionBasicTestCase::Dequeue,
                            this,
                            queue,
                            1);
    }
}

void
QueueProtectionBasicTestCase::BucketScoreTrace(uint32_t flowId,
                                               uint32_t bucketId,
                                               int64_t bucketDepth)
{
    NS_LOG_DEBUG("Bucket score trace " << flowId << " " << bucketId << " " << bucketDepth);
}

void
QueueProtectionBasicTestCase::SuccessTrace(Ptr<const QueueDiscItem> item,
                                           uint32_t flowId,
                                           uint32_t qsize,
                                           int64_t qDelay,
                                           int64_t qLscore,
                                           int64_t criticalQl,
                                           int64_t criticalQlProduct,
                                           uint16_t bckt_id)
{
    NS_LOG_DEBUG("Success trace flowID "
                 << flowId << " qDelay " << qDelay << " criticalQl " << criticalQl << " qLscore "
                 << qLscore << " qsize " << qsize << " criticalQl " << criticalQl
                 << " criticalQlProduct " << criticalQlProduct << " bckt_id " << bckt_id);
    QueueProtectionTestOutcome outcome =
        QueueProtectionTestOutcome(flowId, qsize, qDelay, qLscore, criticalQl);
    m_successOutcomes.push_back(outcome);
}

void
QueueProtectionBasicTestCase::SanctionTrace(Ptr<const QueueDiscItem> item,
                                            uint32_t flowId,
                                            uint32_t qsize,
                                            int64_t qDelay,
                                            int64_t qLscore,
                                            int64_t criticalQl,
                                            int64_t criticalQlProduct,
                                            uint16_t bckt_id)
{
    NS_LOG_DEBUG("Success trace flowID "
                 << flowId << " qDelay " << qDelay << " criticalQl " << criticalQl << " qLscore "
                 << qLscore << " qsize " << qsize << " criticalQl " << criticalQl
                 << " criticalQlProduct " << criticalQlProduct << " bckt_id " << bckt_id);
    QueueProtectionTestOutcome outcome =
        QueueProtectionTestOutcome(flowId, qsize, qDelay, qLscore, criticalQl);
    m_sanctionOutcomes.push_back(outcome);
}

void
QueueProtectionBasicTestCase::FlowTrace(uint32_t flowId, Uflw flow)
{
    NS_LOG_DEBUG("FlowTrace " << flowId << " " << flow);
}

static class QueueProtectionTestSuite : public TestSuite
{
  public:
    QueueProtectionTestSuite()
        : TestSuite("queue-protection", UNIT)
    {
        AddTestCase(new QueueProtectionBasicTestCase(), TestCase::QUICK);
    }
} g_QueueProtectionTestSuite;
