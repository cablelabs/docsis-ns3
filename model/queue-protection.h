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

#ifndef DOCSIS_QUEUE_PROTECTION_H
#define DOCSIS_QUEUE_PROTECTION_H

#include <vector>
#include <limits>
#include "ns3/callback.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/queue-disc.h"
#include "ns3/traffic-control-layer.h"
#include "dual-queue-coupled-aqm.h"
#include "docsis-l4s-packet-filter.h"
#include "microflow-descriptor.h"

namespace ns3 {
namespace docsis {

/**
 * \ingroup docsis
 *
 * \brief Queue Protection implementation for Dual Queue Coupled AQM
 *
 * Based on pseudocode description of Annex P, CM-SP-MULPIv3.1-I19-191016
 *
 * This object is designed to be used in conjunction with a
 * DualQueueCoupledAqm object.  The two objects have references to one
 * another, so that DualQueueCoupledAqm can pass a packet to this object
 * for evaluation at enqueue time, and this object can access latency
 * estimates of the dual queue.
 *
 * This object also depends on a hash algorithm, which is passed in via a
 * callback.
 *
 * Typical code for creating and connecting these objects is as follows:
 * \code
 * Ptr<DualQueueCoupledAqm> dual = CreateObject<DualQueueCoupledAqm> ();
 * Ptr<QueueProtection> queueProtection = CreateObject<QueueProtection> ();
 * queueProtection->SetHashCallback (MakeCallback(&DocsisL4SPacketFilter::GenerateHash32));
 * queueProtection->SetQueue (dual);
 * dual->SetQueueProtection (queueProtection);
 * \endcode
 *
 */
class QueueProtection : public Object
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief QueueProtection constructor
   */
  QueueProtection ();
  /**
   * \brief QueueProtection Destructor
   */
  virtual ~QueueProtection ();
  /**
   * \brief Set pointer to the DualQueueCoupledAqm
   * \param queue Pointer to DualQueueCoupledAqm
   */
  void SetQueue (Ptr<DualQueueCoupledAqm> queue);
  /**
   * \brief Get pointer to the DualQueueCoupledAqm
   * \return Pointer to DualQueueCoupledAqm
   */
  Ptr<DualQueueCoupledAqm> GetQueue (void) const;
  /**
   * \brief Perform queue protection action on the packet
   * \param item a QueueDiscItem containing a packet to evaluate
   * \return the outcome of queue protection action
   */
  QueueProtectionOutcome QueueProtect (Ptr<const QueueDiscItem> item);
  /**
   * Provide an external method to generate the 32-bit hash of a flow ID.
   * Decoupled by a callback function because this module does not have a
   * dependency on the internet module.
   *
   * \param hashCallback the callback function
   */
  void SetHashCallback (Callback<Uflw, Ptr<const QueueDiscItem>, uint32_t> hashCallback);
  /**
   * TracedCallback signature for bucket score reporting
   *
   * \param [in] flowId unique flow ID assigned to the flow
   * \param [in] bucketId bucket ID (index)
   * \param [in] bucketDepth bucket depth (may be negative)
   *
   * The bucket depth is state maintained as an absolute time (units of T_RES)
   * in the variable m_exp, but the reported depth here is the time
   * in normalized queueing score (m_exp - now) (may be positive or negative)
   */
  typedef void (* BucketScoreTracedCallback) (uint32_t flowId, uint32_t bucketId, int64_t bucketDepth);
  /**
   * TracedCallback signature for queue protection outcome reporting
   *
   * \param [in] item the queue disc item containing the packet
   * \param [in] flowId unique flow ID assigned to the flow
   * \param [in] qsize size in the LL queue (bytes) at time of outcome
   * \param [in] qDelay latency of LL queue at time of outcome (units of T_RES)
   * \param [in] qLscore bucket score at time of outcome (t_exp - now)
   * \param [in] criticalQl threshold against which qDelay is evaluated
   * \param [in] criticalQlProduct term used in second threshold decision
   * \param [in] bckt_id bucket ID
   *
   * In the case of a report of a success, the qsize and qDela will report
   * the values including the addition of the packet to the queue.  In the
   * case of a report of a sanction, the qsize and qDelay will report
   * the values excluding the addition of the packet to the queue.
   */
  typedef void (* ProtectionOutcomeTracedCallback) (Ptr<const QueueDiscItem> item, uint32_t flowId, uint32_t qsize, int64_t qDelay, int64_t qLscore, int64_t criticalQl, int64_t criticalQlProduct, uint16_t bckt_id);
  /**
   * TracedCallback signature for reporting new flows
   *
   * \param [in] flowId new flow ID
   * \param [in] flow microflow structure for the new flow
   */
  typedef void (* FlowTracedCallback) (uint32_t flowId, Uflw flow);

protected:
  /**
   * \brief Dispose of the object
   */
  virtual void DoDispose (void);

private:
  /**
   * Per-microflow state container
   *
   * The specification states that t_exp may be recalibrated periodically
   * because it grows without bound.  This model uses a 64-bit integer
   * without recalibration; overflow is avoided by the size of the integer.
   */
  struct Bucket
  {
    bool m_sanctioned {false};  //!< Whether bucket stores a sanctioned flow
    uint32_t m_flowId {std::numeric_limits<uint32_t>::max ()};   //!< Flow ID using the bucket
    int64_t m_exp {0};          //!< Expiry time (units of T_RES)
  };
  /**
   * \brief Get value to use as CRITICALqL
   * \return value to use as CRITICALqL, in units of ns
   */
  int64_t GetCriticalQl (void) const;
  /**
   * \brief Get value to use as CRITICALqLSCORE
   * \return value to use as CRITICALqLSCORE, in units of T_RES
   */
  int64_t GetCriticalQlScore (void) const;
  /**
   * \brief Get the LL queuing delay estimate and convert to integer ns
   * \param size Size of incoming packet to add to the delay
   * \return LL queuing delay in units of ns
   */
  int64_t GetLowLatencyQueuingDelay (uint32_t size = 0) const;
  /**
   * \brief Get value to use as MAX_QLSCORE
   * \return value to use as MAX_QLSCORE, in units of T_RES
   */
  int64_t GetMaxQlScore (void) const;
  /**
   * \brief Get value to use as probNative marking probability
   * \param size Size of incoming packet to use in computing probNative
   * \return value to use as probNative
   */
  double GetProbNative (uint32_t size = 0) const;
  /**
   * \brief Calculate the congestion score term to add to a flow's bucket
   * \param probNative probNative value to use in calculation
   * \param pktSize Size of incoming packet in bytes
   * \return congestion value to use, in units of (bytes * ns)/ T_RES
   */
  int64_t GetCongestionScore (double probNative, uint32_t pktSize) const;
  /**
   * \brief utility function to resize the width of array of buckets
   * \param size bit-width of index number for non-default buckets (BI_SIZE)
   */
  void SetBucketIdSize (uint8_t size);
  /**
   * \brief Fill and drain bucket based on congestion score and aging
   * \param bucketId bucket ID to fill
   * \param pktSize Size of incoming packet in bytes
   * \param probNative probNative value to use in calculation
   * \return qLscore in units of T_RES
   */
  int64_t FillBucket (uint32_t bucketId, uint32_t pktSize, double probNative);
  /**
   * \brief Pick a bucket for incoming packet
   * \param item QueueDiscItem to evaluate
   * \return pair of values; first value is flowId, second value is bucket ID
   */
  std::pair<uint32_t, uint16_t> PickBucket (Ptr<const QueueDiscItem> item);

  // Private member variables
  // Variables tied to attributes do not need default initializers
  bool m_qProtectOn;                            //!< queue protection enable
  int64_t m_criticalQlUs;                       //!< Queue delay (us) above which to evaluate
  bool m_configureQpLatencyThreshold;           //!< True if configured value for QpLatencyThreshold overrides MAXTH
  int64_t m_criticalQlScoreUs;                  //!< threshold queueing score (us)
  uint32_t m_qpDrainRateExponent;               //!< exponent of queuing score drain rate (congestion-B/s)
  uint32_t m_tRes;                              //!< resolution of t_exp [ns]
  uint32_t m_attempts;                          //!< Number of attempts to find a bucket
  uint8_t m_bucketIdSize;                       //!< Size of ID for dedicated buckets
  Time m_maxQlScoreTime;                        //!< MAX_QLSCORE value
  uint32_t m_perturbation;                      //!< Hash perturbation value

  // Pointers and callbacks
  Ptr<DualQueueCoupledAqm> m_queue;             //! Pointer to dual queue

  Callback<Uflw, Ptr<const QueueDiscItem>, uint32_t> m_hashCallback;

  // Other private variables
  uint32_t m_flowId {0};                        //!< Counter for flowId
  std::vector<Bucket> m_buckets;                //!< buckets
  // For large numbers of flows, optimize this for searching on result
  std::vector<Uflw> m_flows;                    //!< Observed flows

  /**
   * The trace source fired when a bucket score is updated.
   */
  TracedCallback<uint32_t, uint32_t, int64_t> m_bucketTrace;
  /**
   * The trace source fired when a sanction outcome is recorded
   */
  TracedCallback<Ptr<const QueueDiscItem>, uint32_t, uint32_t, int64_t, int64_t, int64_t, int64_t, uint16_t> m_sanctionTrace;
  /**
   * The trace source fired when a success outcome is recorded
   */
  TracedCallback<Ptr<const QueueDiscItem>, uint32_t, uint32_t, int64_t, int64_t, int64_t, int64_t, uint16_t> m_successTrace;
  /**
   * The trace source fired when a new flow is detected
   */
  TracedCallback<uint32_t, Uflw> m_flowTrace;

  /**
   * Trace of the value of probNative that is used in queue protection.
   * Note:  This value may be slightly different than the probNative
   * traced in DualQueueCoupledAqm because the value in this object
   * is based on the addition of the arriving packet to the queue length.
   * If queue protection sanctions the packet, the low latency queue never
   * sees it and the queue's value of probNative would not reflect it.
   */
  TracedValue<double> m_probNative;
};

}    // namespace docsis
}    // namespace ns3
#endif
