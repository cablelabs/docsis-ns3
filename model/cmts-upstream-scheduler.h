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

#ifndef DOCSIS_CMTS_UPSTREAM_SCHEDULER_H
#define DOCSIS_CMTS_UPSTREAM_SCHEDULER_H

#include "ns3/object.h"
#include "ns3/cm-net-device.h"
#include "ns3/docsis-configuration.h"
#include "ns3/docsis-net-device.h"

namespace ns3 {

class RandomVariableStream;

namespace docsis {

/**
 * \ingroup docsis
 *
 * Objects of type CmtsUpstreamScheduler model the upstream grant scheduling 
 * process on a CMTS.  This class may be subclassed to implement different
 * scheduling disciplines.  The main interface methods to this class are:
 *
 * - the ReceiveRequest() method models the arrival of grant requests from
 *   the modem
 * - ProvisionAggregateServiceFlow() is used to configure the ASF information
 * - ReceiveUnusedLGrantUpdate() and ReceiveUnusedCGrantUpdate() are used to
 *   (optionally) model the detection of unused grants from the service flows
 */
class CmtsUpstreamScheduler : public Object
{
public:
  /**
   * \brief Get the TypeId
   *
   * \return The TypeId for this class
   */
  static TypeId GetTypeId (void);

  /**
   * Constructor for CmtsUpstreamScheduler
   */
  CmtsUpstreamScheduler ();

  /**
   * Destructor for CmtsUpstreamScheduler
   */
  virtual ~CmtsUpstreamScheduler ();

  /**
   * Possible allocation policies for distributing granted minislots
   * throughout the MAP interval
   */
  enum GrantAllocationPolicy
  {
    SINGLE,   /**< One grant is scheduled for the MAP interval */
    SPREAD    /**< The grant is spread, as evenly as possible, to all frames */
  };

  /**
   * Set upstream device pointer
   * \param upstream upstream device pointer
   */
  void SetUpstream (Ptr<CmNetDevice> upstream);

  /**
   * Get upstream device pointer
   * \return upstream device pointer
   */
  Ptr<CmNetDevice> GetUpstream (void) const;

  /**
   * \brief Process a request for grant from a CM
   * \param request grant request
   */
  void ReceiveRequest (GrantRequest request);

  /**
   * \brief Start the scheduling of MAP messages
   * \param allocStartTime AllocStartTime for the first MAP message
   */
  void Start (uint32_t allocStartTime);

  /**
   * \brief Provision the aggregate service flow
   * \param flow the aggregate service flow
   */
  void ProvisionAggregateServiceFlow (Ptr<AggregateServiceFlow> flow);

  /**
   * \brief Get a pointer to the aggregate service flow
   * \return the aggregate service flow
   */
  Ptr<const AggregateServiceFlow> GetAggregateServiceFlow (void) const;

  /**
   * \brief Remove the aggregate service flow
   */
  void RemoveAggregateServiceFlow (void);

  /**
   * \brief Provision the single service flow
   * \param flow the single service flow
   */
  void ProvisionSingleServiceFlow (Ptr<ServiceFlow> flow);

  /**
   * \brief Get a pointer to the single service flow
   * \return the single service flow
   */
  Ptr<const ServiceFlow> GetSingleServiceFlow (void) const;

  /**
   * \brief Remove the single service flow
   */
  void RemoveSingleServiceFlow (void);

  /**
   * \brief Return a pointer to the classic service flow, if present
   * \return the classic service flow
   */
  Ptr<const ServiceFlow> GetClassicServiceFlow (void) const;

  /**
   * \brief Return a pointer to the low latency service flow, if present
   * \return the low latency service flow
   */
  Ptr<const ServiceFlow> GetLowLatencyServiceFlow (void) const;

  /**
   * \brief Receive an update that may report upon unused grants (unused
   *        grant tracking)
   * \param state The state report for the LL service flow
   */
  void ReceiveUnusedLGrantUpdate (CmNetDevice::LGrantState state);

  /**
   * \brief Receive an update that may report upon unused grants (unused
   *        grant tracking)
   * \param state The state report for the classic service flow
   */
  void ReceiveUnusedCGrantUpdate (CmNetDevice::CGrantState state);

 /**
  * Assign a fixed random variable stream number to the random variables
  * used by this model.  Return the number of streams (possibly zero) that
  * have been assigned.
  *
  * \param stream first stream index to use
  * \return the number of stream indices assigned by this model
  */
  int64_t AssignStreams (int64_t stream);

protected:
  /**
   * \brief Dispose of the object
   */
  virtual void DoDispose (void);
  /**
   * \brief Perform runtime initialization
   */
  virtual void DoInitialize (void);

private:
  /**
   * \brief Generate a MAP message and send to upstream
   * \param allocStartTime AllocStartTime for the MAP message
   */
  void GenerateMap (uint32_t allocStartTime);

  /**
   * The "FreeCapacityVariation" attribute is used to simulate grant variability
   * from congestion.  For a non-congested situation, it should be set
   * low so that free capacity (nextmax) in the MAP is fairly uniform
   * from one MAP to the next.  For a congested situation, it should be
   * set high which will cause nextmax to vary significantly from one
   * MAP interval to the next.  This simulates the bursty nature of
   * bandwidth access when a CMTS scheduler is juggling many
   * simultaneous requests from modems and will temporarily starve some
   * flows while flooding others.
   *
   * \return minislots of capacity available
   */
  uint32_t CalculateFreeCapacity (void);

  MapMessage BuildMapMessage (uint32_t allocStartTime, const std::map<uint32_t, Grant>& grantList, bool dataGrantPending);

  virtual void ScheduleGrant (std::map<uint32_t, Grant>& grantList, uint32_t cMinislots, uint32_t lMinislots);

  std::vector<uint32_t> SpreadGrantAcrossFrames (uint32_t grantedSlots);

  virtual void SpreadAndScheduleGrant (std::map<uint32_t, Grant>& grantList, uint32_t cMinislots, uint32_t lMinislots);

  virtual std::pair<uint32_t, uint32_t> Allocate (uint32_t cRequestedBytes, uint32_t lRequestedBytes, uint32_t grantBytes, uint32_t freeMinislots);

  /**
   * Utility function analogous to std::round:  round to nearest number of
   * minislots required for bytes
   * \param bytes number of bytes
   * \return number of minislots
   */
  uint32_t MinislotRound (uint32_t bytes) const;

  /**
   * Utility function analogous to std::ceil:  smallest integral number of
   * minislots required for bytes
   * \param bytes number of bytes
   * \return number of minislots
   */
  uint32_t MinislotCeil (uint32_t bytes) const;

  /**
   * Utility function to convert minislots back to bytes
   * \param minislots number of minislots
   * \return number of bytes
   */
  uint32_t MinislotsToBytes (uint32_t minislots) const;

  /**
   * Increment the rate-shaping token bucket associated with the ASF or SF
   * \return pair of values (msr tokens, peak tokens)
   */
  std::pair<int32_t, uint32_t> IncrementTokenBucketCounters (void);

  /**
   * Decrement the rate-shaping token bucket associated with the ASF or SF
   * \param decrement the number of bytes to decrement
   */
  void DecrementTokenBucketCounters (uint32_t decrement);

  /**
   * \brief Assignment operator
   *
   * The method is private to disable
   *
   * \param o Other CmtsUpstreamScheduler
   * \return New instance of the CmtsUpstreamScheduler
   */
  CmtsUpstreamScheduler& operator = (const CmtsUpstreamScheduler &o);

  /**
   * \brief Copy constructor
   *
   * The method is private to disable
   *
   * \param o Other CmtsUpstreamScheduler
   */
  CmtsUpstreamScheduler (const CmtsUpstreamScheduler &o);

  // Variables tied to attributes
  double m_freeCapacityMean;  //!< Model congestion on the DOCSIS link by limiting available upstream capacity (bps)
  double m_freeCapacityVariation;     //!< Model congestion variation on the DOCSIS link by specifying a percentage bound (RANGE: 0 - 100)
  GrantAllocationPolicy m_grantAllocPolicy; //!< Grant allocation policy

  uint8_t m_schedulingWeight;  //!< Scheduling weight for low latency flow
  uint16_t m_meanPacketSize;   //!< Mean packet size (bytes) estimate

  // Other variables
  Ptr<RandomVariableStream> m_congestionVariable;
  Ptr<RandomVariableStream> m_schedulerVariable;

  Ptr<CmNetDevice> m_upstream;

  uint32_t m_lastRequestTime {0}; // track grant request times
  std::map<uint16_t, uint32_t> m_requests;  // track byte requests
  Ptr<AggregateServiceFlow> m_asf {nullptr}; // Supports only single ASF
  Ptr<ServiceFlow> m_sf {nullptr};           // Pointer to single SF
  Ptr<const ServiceFlow> m_classicSf {nullptr};    // Pointer to classic SF
  Ptr<const ServiceFlow> m_llSf {nullptr};         // Pointer to low latency SF
  TokenBucketState m_tokenBucketState;
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_CMTS_UPSTREAM_SCHEDULER */
