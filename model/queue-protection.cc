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
 *  Authors:
 *  Tom Henderson <tomh@tomh.org>
 *  Bob Briscoe <B.Briscoe-contractor@cablelabs.com>
 *  Greg White <g.white@cablelabs.com>
 *  Karthik Sundaresan <k.sundaresan@cablelabs.com>
 */

#include "queue-protection.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/integer.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("QueueProtection");

namespace docsis
{

NS_OBJECT_ENSURE_REGISTERED(QueueProtection);

TypeId
QueueProtection::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::docsis::QueueProtection")
            .SetParent<Object>()
            .SetGroupName("Docsis")
            .AddConstructor<QueueProtection>()
            .AddAttribute("QProtectOn",
                          "If true, enables queue protection",
                          BooleanValue(true),
                          MakeBooleanAccessor(&QueueProtection::m_qProtectOn),
                          MakeBooleanChecker())
            .AddAttribute(
                "QpLatencyThreshold",
                "(CRITICALqL_us) Configured latency threshold (us) for the qprotect function",
                IntegerValue(1000),
                MakeIntegerAccessor(&QueueProtection::m_criticalQlUs),
                MakeIntegerChecker<int64_t>())
            .AddAttribute("ConfigureQpLatencyThreshold",
                          "If true, use configured value of QpLatencyThreshold instead of MAXTH",
                          BooleanValue(false),
                          MakeBooleanAccessor(&QueueProtection::m_configureQpLatencyThreshold),
                          MakeBooleanChecker())
            .AddAttribute("QpQueuingScoreThreshold",
                          "(CRITICALqLSCORE_us) Bucket score threshold (units of us)",
                          IntegerValue(4000),
                          MakeIntegerAccessor(&QueueProtection::m_criticalQlScoreUs),
                          MakeIntegerChecker<int64_t>())
            .AddAttribute("QpDrainRateExponent",
                          "(LG_AGING) Exponent (power of 2) to set the aging rate of queuing score",
                          UintegerValue(19),
                          MakeUintegerAccessor(&QueueProtection::m_qpDrainRateExponent),
                          MakeUintegerChecker<uint32_t>(1, 30))
            .AddAttribute("TRes",
                          "Resolution factor T_RES (resolution of t_exp) (ns)",
                          UintegerValue(1),
                          MakeUintegerAccessor(&QueueProtection::m_tRes),
                          MakeUintegerChecker<uint32_t>(1, 1000000))
            .AddAttribute("Attempts",
                          "Number of attempts to find a bucket",
                          UintegerValue(2),
                          MakeUintegerAccessor(&QueueProtection::m_attempts),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("BucketIdSize",
                          "Size of ID for dedicated buckets (2^size + 1 total buckets)",
                          UintegerValue(5),
                          MakeUintegerAccessor(&QueueProtection::SetBucketIdSize),
                          MakeUintegerChecker<uint8_t>(0, 14))
            .AddAttribute("MaxQLScore",
                          "Time value for MAX_QLSCORE",
                          TimeValue(Seconds(5)),
                          MakeTimeAccessor(&QueueProtection::m_maxQlScoreTime),
                          MakeTimeChecker())
            .AddAttribute("Perturbation",
                          "The salt used as an additional input to the hash function",
                          UintegerValue(0),
                          MakeUintegerAccessor(&QueueProtection::m_perturbation),
                          MakeUintegerChecker<uint32_t>())
            .AddTraceSource("BucketScoreUpdate",
                            "Report upon a change to a bucket score",
                            MakeTraceSourceAccessor(&QueueProtection::m_bucketTrace),
                            "ns3::QueueProtection::BucketScoreTracedCallback")
            .AddTraceSource("Sanction",
                            "Report Low Latency queue latency upon a sanctioning decision",
                            MakeTraceSourceAccessor(&QueueProtection::m_sanctionTrace),
                            "ns3::QueueProtection::ProtectionOutcomeTracedCallback")
            .AddTraceSource("Success",
                            "Report Low Latency queue latency and backlog upon a success decision",
                            MakeTraceSourceAccessor(&QueueProtection::m_successTrace),
                            "ns3::QueueProtection::ProtectionOutcomeTracedCallback")
            .AddTraceSource("Flow",
                            "Report new flow arrival",
                            MakeTraceSourceAccessor(&QueueProtection::m_flowTrace),
                            "ns3::QueueProtection::FlowTracedCallback")
            .AddTraceSource("ProbNative",
                            "Value of probNative used to compute congestion score",
                            MakeTraceSourceAccessor(&QueueProtection::m_probNative),
                            "ns3::TracedValueCallback::Double");
    return tid;
}

QueueProtection::QueueProtection()
{
    NS_LOG_FUNCTION(this);
    m_hashCallback = MakeNullCallback<Uflw, Ptr<const QueueDiscItem>, uint32_t>();
}

QueueProtection::~QueueProtection()
{
    NS_LOG_FUNCTION(this);
}

void
QueueProtection::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_queue = nullptr;
    m_hashCallback = MakeNullCallback<Uflw, Ptr<const QueueDiscItem>, uint32_t>();
}

void
QueueProtection::SetBucketIdSize(uint8_t size)
{
    NS_LOG_FUNCTION(this << size);
    m_bucketIdSize = size;
    m_buckets.resize((1 << m_bucketIdSize) + 1);
}

void
QueueProtection::SetHashCallback(Callback<Uflw, Ptr<const QueueDiscItem>, uint32_t> hashCallback)
{
    NS_LOG_FUNCTION(this);
    m_hashCallback = hashCallback;
}

void
QueueProtection::SetQueue(Ptr<DualQueueCoupledAqm> queue)
{
    NS_LOG_FUNCTION(this << queue);
    m_queue = queue;
}

Ptr<DualQueueCoupledAqm>
QueueProtection::GetQueue() const
{
    return m_queue;
}

int64_t
QueueProtection::GetCriticalQl() const
{
    if (m_configureQpLatencyThreshold)
    {
        // Convert us to ns
        return m_criticalQlUs * 1000;
    }
    else
    {
        NS_ABORT_MSG_UNLESS(GetQueue(), "Error: queue pointer not set");
        TimeValue tVal;
        GetQueue()->GetAttribute("MaxTh", tVal);
        return tVal.Get().GetNanoSeconds();
    }
}

int64_t
QueueProtection::GetCriticalQlScore() const
{
    // Convert us to T_RES
    return m_criticalQlScoreUs * 1000 / m_tRes;
}

int64_t
QueueProtection::GetMaxQlScore() const
{
    // Convert Time to T_RES
    return m_maxQlScoreTime.GetNanoSeconds() / m_tRes;
}

std::pair<uint32_t, uint16_t>
QueueProtection::PickBucket(Ptr<const QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);
    // The pseudocode passes in the microflow identifier as an argument.
    // Instead, we pass in the packet (via item) and generate the identifier here
    Uflw result = m_hashCallback(item, m_perturbation);
    uint32_t nbuckets = 1 << m_bucketIdSize; // pow(2, BI_SIZE)
    uint32_t h32 = result.h32;               // holds the hash of the packet's flow identifiers
    uint32_t h = 0;                          // bucket index being checked
    uint32_t hsav = nbuckets;                // Default bucket

    int64_t now = Simulator::Now().GetNanoSeconds() / m_tRes; // units of T_RES
    uint32_t mask = nbuckets - 1; // a convenient constant, filled with ones
    NS_ABORT_MSG_IF(m_hashCallback.IsNull(), "Queue Protection requires a hash function");

    // Find the flowId corresponding to the above hash stored in 'result'
    uint32_t flowId;
    auto it = std::find(m_flows.begin(), m_flows.end(), result);
    if (it != m_flows.end())
    {
        flowId = it - m_flows.begin();
        NS_LOG_LOGIC("Existing flow id " << flowId << " vector: " << result);
    }
    else
    {
        flowId = m_flowId++;
        m_flows.push_back(result);
        m_flowTrace(flowId, result);
        NS_LOG_LOGIC("New flow " << flowId << " inserted into flow vector: " << result);
    }

    // The for loop checks ATTEMPTS buckets for ownership by the microflow-ID.
    // It also records the first bucket, if any, that could be recycled because
    // it has expired.  However, it is not allowed to recycle a bucket until
    // it has completed all the ownership checks.
    for (uint32_t j = 0; j < m_attempts; j++)
    {
        h = h32 & mask; // Use least signif. BI_SIZE bits of hash for each attempt
        if (m_buckets[h].m_flowId == flowId) // Once bucket found...
        {
            NS_LOG_LOGIC("Found existing bucket ID " << h << " for attempt " << j << "; flowId "
                                                     << flowId);
            if (m_buckets[h].m_exp <= now)
            {
                m_buckets[h].m_exp = now; // ...reset it
                m_bucketTrace(m_buckets[h].m_flowId, h, 0);
            }
            return std::pair<uint32_t, uint16_t>(flowId, h); // ...use it
        }
        // If an expired bucket is yet to be found, and bucket under test
        // has expired, set it as the interim bucket
        else if ((hsav == nbuckets) && (m_buckets[h].m_exp <= now))
        {
            NS_LOG_LOGIC("Found expired bucket ID " << h << " for attempt " << j << "; flowId "
                                                    << flowId);
            hsav = h;
        }
        h32 >>= m_bucketIdSize; // Bit-shift hash for next attempt
    }
    // If reached here, no tested bucket was owned by the microflow-ID
    bool traceChangedScore = false;
    if (hsav != nbuckets)
    {
        // If here, we found an expired bucket within the above for loop
        NS_LOG_LOGIC("Found expired bucket " << hsav << " within above for loop");
        m_buckets[hsav].m_exp = now; // Reset expired bucket
        traceChangedScore = true;
    }
    else
    {
        // If here, we're having to use the default bucket
        NS_LOG_LOGIC("No buckets available; use default bucket " << nbuckets);
        if (m_buckets[hsav].m_exp <= now) // If default bucket has expired
        {
            m_buckets[hsav].m_exp = now; // ...reset it
            traceChangedScore = true;
        }
        // else if (buckets[hsav].id != uvlw)
        // then the default bucket is in use by another flow
        // optionally count default bucket collisions in vendor-specific counter
    }
    m_buckets[hsav].m_flowId = flowId; // In either case, claim bucket for recycling
    if (traceChangedScore)
    {
        m_bucketTrace(m_buckets[hsav].m_flowId, hsav, 0);
    }
    return std::pair<uint32_t, uint16_t>(flowId, hsav);
}

int64_t
QueueProtection::GetCongestionScore(double probNative, uint32_t pkt_sz) const
{
    NS_LOG_FUNCTION(this << probNative << pkt_sz);
    // AGING is defined as pow (2, (LG_AGING - 30)) * T_RES
    // 1/AGING = 1/pow (2, (LG_AGING - 30)) * 1/T_RES
    //         = pow (2, (30 - LG_AGING)) * 1/T_RES
    // define pow (2, (30 - LG_AGING)) as an intermediate 'agingFactor' variable
    int64_t agingFactor = 1 << (30 - m_qpDrainRateExponent);
    // calculate probNative * pkt_size / AGING
    int64_t score = static_cast<int64_t>(probNative * pkt_sz * agingFactor / m_tRes);
    NS_ASSERT_MSG(score >= 0, "Should not be negative congestion scores");
    NS_LOG_LOGIC("Congestion score for probNative " << probNative << " pkt_sz " << pkt_sz
                                                    << " is: " << score << "ns");
    return score;
}

// return value in units of T_RES
int64_t
QueueProtection::FillBucket(uint32_t bucket_id, uint32_t pkt_sz, double probNative)
{
    NS_LOG_FUNCTION(this << bucket_id << pkt_sz << probNative);
    int64_t lastExp = m_buckets[bucket_id].m_exp;             // Save for later comparison
    int64_t now = Simulator::Now().GetNanoSeconds() / m_tRes; // units of T_RES
    int64_t congestionScore = GetCongestionScore(probNative, pkt_sz);
    NS_LOG_LOGIC("congestion score for bucket " << bucket_id << " probNative= " << probNative
                                                << " pkt_sz= " << pkt_sz
                                                << " score= " << congestionScore);
    m_buckets[bucket_id].m_exp += congestionScore;
    if ((m_buckets[bucket_id].m_exp - now) > GetMaxQlScore())
    {
        m_buckets[bucket_id].m_exp = now + GetMaxQlScore();
        NS_LOG_LOGIC("Congestion Score limited by MAX_QLSCORE << " << GetMaxQlScore());
    }
    if (m_buckets[bucket_id].m_exp != lastExp)
    {
        // only trace changed values
        m_bucketTrace(m_buckets[bucket_id].m_flowId, bucket_id, (m_buckets[bucket_id].m_exp - now));
    }
    return (m_buckets[bucket_id].m_exp - now);
}

QueueProtectionOutcome
QueueProtection::QueueProtect(Ptr<const QueueDiscItem> item, Time qDelay, double probNative)
{
    NS_LOG_FUNCTION(this << item << qDelay.As(Time::NS) << probNative);
    m_probNative = probNative; // Update trace source value
    // In the pseudocode, the QPROTECT_ON flag controls whether this method
    // is called.  However, this is implemented such that QProtect is called
    // all of the time from the DualQueue Enqueue() method, so we implement
    // the check on this flag here, from within this method.
    if (!m_qProtectOn)
    {
        return QueueProtectionOutcome::SUCCESS;
    }
    uint16_t bckt_id; // bucket index
    int64_t qLscore;  // queuing score of pkt's flow in units of T_RES
    int64_t qDelayNs = qDelay.GetNanoSeconds();

    // This implementation sets and returns the uflow ID from within PickBucket()
    std::pair<uint32_t, uint16_t> result = PickBucket(item);
    uint32_t flowId = result.first;
    bckt_id = result.second;
    qLscore = FillBucket(bckt_id, item->GetSize(), probNative);
    NS_LOG_LOGIC("bckt_id: " << bckt_id << "  qDelay(ns): " << qDelayNs
                             << "  qLscore: " << qLscore);
    // units of this product are (T_RES)              *    (ns)
    int64_t criticalQlProduct = GetCriticalQlScore() * GetCriticalQl();
    // Determine whether to sanction packet
    // if ( ( qdelay > CRITICALqL )
    // Test if qdelay over a threshold...
    // ...and if microflow's q'ing score scaled by qdelay/CRITICALqL
    // ...exceeds CRITICALqLSCORE
    // && ( qdelay * qLscore > CRITICALqLPRODUCT ) )
    if ((qDelayNs > GetCriticalQl()) && (qDelayNs * qLscore > criticalQlProduct))
    {
        NS_LOG_INFO("QueueProtect() sanctioning: " << qDelayNs << " " << qLscore << " "
                                                   << GetCriticalQl() << " " << criticalQlProduct
                                                   << " " << flowId << " " << bckt_id);
        m_sanctionTrace(item,
                        flowId,
                        m_queue->GetLowLatencyQueueSize(),
                        qDelayNs,
                        qLscore,
                        GetCriticalQl(),
                        criticalQlProduct,
                        bckt_id);
        return QueueProtectionOutcome::SANCTION;
    }
    else
    {
        NS_LOG_INFO("QueueProtect() success: " << qDelayNs << " " << qLscore << " "
                                               << GetCriticalQl() << " " << criticalQlProduct << " "
                                               << flowId << " " << bckt_id);
        m_successTrace(item,
                       flowId,
                       m_queue->GetLowLatencyQueueSize() + item->GetSize(),
                       qDelayNs,
                       qLscore,
                       GetCriticalQl(),
                       criticalQlProduct,
                       bckt_id);
        return QueueProtectionOutcome::SUCCESS;
    }
}

} // namespace docsis
} // namespace ns3
