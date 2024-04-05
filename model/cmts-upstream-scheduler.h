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

#include "cm-net-device.h"
#include "docsis-configuration.h"
#include "docsis-net-device.h"

#include "ns3/data-rate.h"
#include "ns3/object.h"
#include "ns3/sequence-number.h"
#include "ns3/trace-source-accessor.h"

#include <map>

namespace ns3
{

class RandomVariableStream;

namespace docsis
{

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
    static TypeId GetTypeId();

    /**
     * Constructor for CmtsUpstreamScheduler
     */
    CmtsUpstreamScheduler();

    /**
     * Destructor for CmtsUpstreamScheduler
     */
    ~CmtsUpstreamScheduler() override;

    /**
     * Possible allocation policies for distributing granted minislots
     * throughout the MAP interval
     */
    enum GrantAllocationPolicy
    {
        SINGLE, /**< One grant is scheduled for the MAP interval */
        SPREAD  /**< The grant is spread, as evenly as possible, to all frames */
    };

    /**
     * Structure of values (CMTS internal state) intended for the MapTrace
     * trace source, called at the notional time that the CMTS generates
     * an upstream Bandwidth Allocation MAP message.
     */
    struct MapReport
    {
        // Information on the available capacity
        DataRate m_freeCapacityMean;    //!< free capacity mean
        double m_freeCapacityVariation; //!< free capacity variation (percentage)
        uint32_t m_maxGrant;            //!< max grant in bytes (based on FreeCapacityMean)
        uint32_t m_adjustedMaxGrant;    //!< adjusted max grant due to capacity variation (bytes)
        // Information on the GGR and GGI
        DataRate m_guaranteedGrantRate;     //!< GGR (if LL SF is present)
        uint16_t m_guaranteedGrantInterval; //!< GGI (if LL SF is present)
        // Information on the request backlog (inputs to the granting process)
        uint32_t m_lRequestBacklog; //!< LL service flow request backlog (bytes)
        uint32_t m_cRequestBacklog; //!< classic service flow request backlog (bytes)
        // Outcome of the grant allocation
        uint32_t m_availableTokens;       //!< Number of bytes permitted by rate shaping
        uint32_t m_lGrantedBytes;         //!< Number of LL granted bytes
        uint32_t m_cGrantedBytes;         //!< Number of classic granted bytes
        uint32_t m_pgsGrantedBytes;       //!< PGS bytes granted to satisfy the GGR
        uint32_t m_excessPgsGrantedBytes; //!< excess PGS bytes granted (due to roundup)
        uint32_t m_lGrantedMinislots;     //!< Number of LL granted minislots
        uint32_t m_cGrantedMinislots;     //!< Number of classic granted minislots
        uint32_t m_pgsGrantedMinislots;   //!< PGS minislots granted
        // Information on the MAP structure itself
        Time m_mapInterval;           //!< actual MAP interval
        uint32_t m_framesPerMap;      //!< Number of frames per MAP
        uint32_t m_minislotsPerFrame; //!< Number of minislots per frame
        uint32_t m_minislotCapacity;  //!< Number of bytes per minislot
        // Copy of the MAP message itself
        MapMessage m_message; //!< MAP message
    };

    /**
     * TracedCallback signature for MapTrace trace
     *
     * \param [in] report structure of values to report
     */
    typedef void (*MapTracedCallback)(MapReport report);

    /**
     * Set upstream device pointer
     * \param upstream upstream device pointer
     */
    void SetUpstream(Ptr<CmNetDevice> upstream);

    /**
     * Get upstream device pointer
     * \return upstream device pointer
     */
    Ptr<CmNetDevice> GetUpstream() const;

    /**
     * \brief Process a request for grant from a CM
     * \param request grant request
     */
    void ReceiveRequest(GrantRequest request);

    /**
     * \brief Start the scheduling of MAP messages
     *
     * This method starts the generation of MAP messages from the scheduler,
     * and is called from the CmNetDevice during initialization.  The
     * AllocStartTime is calculated by the CmNetDevice, and is in units of
     * minislots and is an integer multiple of the number of minislots in a
     * MAP interval.  For example, the first notional MAP interval starts
     * at time 0, the second at time GetActualMapInterval (), and so on.
     * The MAP interval starting at time 0 cannot process a received MAP
     * message, which would have had to have been generated before time 0.
     * See DocisisNetDevice::StartScheduler() method for the logic of
     * how the initial AllocStartTime is computed.
     *
     * The value of MinReqGntTime, expressed in minislots, is also provided.
     * This value corresponds notionally to the number of minislots prior
     * to Alloc Start Time after which grant requests cannot be considered
     * for the MAP interval being scheduled.  Because the scheduler
     * doesn't actually decode minislots, this method provides a suitable
     * value to estimate the Ack Time that would correspond to the
     * Alloc Start Time
     *
     * \param allocStartTime Alloc Start Time (minislots) for the first MAP message
     * \param minReqGntDelaySlots Difference (minislots) between Alloc Start Time and Ack Time
     */
    void Start(uint32_t allocStartTime, uint32_t minReqGntDelaySlots);

    /**
     * \brief Provision the aggregate service flow
     * \param flow the aggregate service flow
     */
    void ProvisionAggregateServiceFlow(Ptr<const AggregateServiceFlow> flow);

    /**
     * \brief Get a pointer to the aggregate service flow
     * \return the aggregate service flow
     */
    Ptr<const AggregateServiceFlow> GetAggregateServiceFlow() const;

    /**
     * \brief Remove the aggregate service flow
     */
    void RemoveAggregateServiceFlow();

    /**
     * \brief Provision the single service flow
     * \param flow the single service flow
     */
    void ProvisionSingleServiceFlow(Ptr<ServiceFlow> flow);

    /**
     * \brief Get a pointer to the single service flow
     * \return the single service flow
     */
    Ptr<const ServiceFlow> GetSingleServiceFlow() const;

    /**
     * \brief Remove the single service flow
     */
    void RemoveSingleServiceFlow();

    /**
     * \brief Return a pointer to the classic service flow, if present
     * \return the classic service flow
     */
    Ptr<const ServiceFlow> GetClassicServiceFlow() const;

    /**
     * \brief Return a pointer to the low latency service flow, if present
     * \return the low latency service flow
     */
    Ptr<const ServiceFlow> GetLowLatencyServiceFlow() const;

    /**
     * \brief Receive an update that may report upon unused grants (unused
     *        grant tracking)
     * \param state The state report for the LL service flow
     */
    void ReceiveUnusedLGrantUpdate(CmNetDevice::LGrantState state);

    /**
     * \brief Receive an update that may report upon unused grants (unused
     *        grant tracking)
     * \param state The state report for the classic service flow
     */
    void ReceiveUnusedCGrantUpdate(CmNetDevice::CGrantState state);

    /**
     * Assign a fixed random variable stream number to the random variables
     * used by this model.  Return the number of streams (possibly zero) that
     * have been assigned.
     *
     * \param stream first stream index to use
     * \return the number of stream indices assigned by this model
     */
    int64_t AssignStreams(int64_t stream);

  protected:
    /**
     * \brief Dispose of the object
     */
    void DoDispose() override;
    /**
     * \brief Perform runtime initialization
     */
    void DoInitialize() override;

  private:
    /**
     * \brief Initialize the building of the MAP report trace
     */
    void InitializeMapReport();

    /**
     * \brief Generate a MAP message and send to upstream modem
     */
    void GenerateMap();

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
    uint32_t CalculateFreeCapacity();

    /**
     * Construct a MAP message from the provided grant list
     * \param grantList the grant list
     * \return a MAP message struct
     */
    MapMessage BuildMapMessage(const std::map<uint32_t, Grant>& grantList);

    /**
     * Schedule the requested number of minislots (if possible) into minislot
     * offsets within the next MAP interval.  Any PGS minislots that have
     * been previously allocated are taken into consideration.  Return the
     * number of minislots actually scheduled (typically, the same as the number
     * that have been requested, unless constraints arise).
     * \param grantList the grant list to modify and return
     * \param pgsGrantList the list of grants allocated by PGS (possibly empty)
     * \param pgsMinislots the number of PGS minislots granted
     * \param cMinislotsToSchedule the number of classic minislots to schedule
     * \param lMinislotsToSchedule the number of low latency minislots to schedule
     * \return a pair of values (classic and low latency minislots scheduled)
     */
    virtual std::pair<uint32_t, uint32_t> ScheduleGrant(
        std::map<uint32_t, Grant>& grantList,
        const std::map<uint32_t, Grant>& pgsGrantList,
        uint32_t pgsMinislots,
        uint32_t cMinislotsToSchedule,
        uint32_t lMinislotsToSchedule);

    /**
     * Allocate classic and low-latency minislots for the next MAP interval
     * based on a number of constraints (free minislots, token bucket state,
     * previously allocated PGS minislots) and based on the number of requested
     * bytes for one or both service flows.
     * \param freeMinislots Number of free minislots available
     * \param pgsMinislots Number of PGS minislots already granted
     * \param availableTokens Number of available tokens (bytes)
     * \param cRequestedBytes Number of classic requested bytes
     * \param lRequestedBytes Number of low-latency requested bytes
     * \return a pair of values (classic and low latency minislots allocated)
     */
    virtual std::pair<uint32_t, uint32_t> Allocate(uint32_t freeMinislots,
                                                   uint32_t pgsMinislots,
                                                   uint32_t availableTokens,
                                                   uint32_t cRequestedBytes,
                                                   uint32_t lRequestedBytes);

    /**
     * Utility function analogous to std::round:  round to nearest number of
     * minislots required for bytes
     * \param bytes number of bytes
     * \return number of minislots
     */
    uint32_t MinislotRound(uint32_t bytes) const;

    /**
     * Utility function analogous to std::ceil:  smallest integral number of
     * minislots required for bytes
     * \param bytes number of bytes
     * \return number of minislots
     */
    uint32_t MinislotCeil(uint32_t bytes) const;

    /**
     * Utility function to convert minislots back to bytes
     * \param minislots number of minislots
     * \return number of bytes
     */
    uint32_t MinislotsToBytes(uint32_t minislots) const;

    /**
     * Increment the rate-shaping token bucket associated with the ASF or SF
     * \return pair of values (msr tokens, peak tokens)
     */
    std::pair<int32_t, uint32_t> IncrementTokenBucketCounters();

    /**
     * Decrement the rate-shaping token bucket associated with the ASF or SF
     * \param decrement the number of bytes to decrement
     */
    void DecrementTokenBucketCounters(uint32_t decrement);

    /**
     * \brief Assignment operator
     *
     * The method is private to disable
     *
     * \param o Other CmtsUpstreamScheduler
     * \return New instance of the CmtsUpstreamScheduler
     */
    CmtsUpstreamScheduler& operator=(const CmtsUpstreamScheduler& o);

    /**
     * \brief Copy constructor
     *
     * The method is private to disable
     *
     * \param o Other CmtsUpstreamScheduler
     */
    CmtsUpstreamScheduler(const CmtsUpstreamScheduler& o);

    // Variables tied to attributes
    DataRate m_freeCapacityMean;    //!< Model congestion on the DOCSIS link by limiting available
                                    //!< upstream capacity
    double m_freeCapacityVariation; //!< Model congestion variation on the DOCSIS link by specifying
                                    //!< a percentage bound (RANGE: 0 - 100)
    uint8_t m_schedulingWeight;     //!< Scheduling weight for low latency flow
    uint16_t m_meanPacketSize;      //!< Mean packet size (bytes) estimate

    // Other variables
    Ptr<RandomVariableStream> m_congestionVariable;
    Ptr<RandomVariableStream> m_schedulerVariable;

    Ptr<CmNetDevice> m_upstream;
    // Event IDs for scheduled events
    EventId m_mapArrivalEvent{};  //!< Schedule MAP arrival
    EventId m_generateMapEvent{}; //!< Schedule next MAP generation

    SequenceNumber32 m_lastRequestTime{0};  // track grant request times
    SequenceNumber32 m_lastPgsGrantTime{0}; // track last PGS grant time, in minislots
    SequenceNumber32 m_allocStartTime{0};   // MAP Alloc Start Time for next MAP message
    uint32_t m_minReqGntDelaySlots{0};      // MinReqGntDelay value in minislots
    uint32_t m_pgsFrameInterval{0};         // Frame interval corresponding to GGI
    uint32_t m_pgsMinislotsPerInterval{0};  // PGS minislots to grant each GGI
    uint32_t m_pgsBytesPerInterval{
        0}; // PGS bytes to grant each GGI, to satisfy GGR (not accounting for minislot roundup)
    uint32_t m_excessPgsBytesPerInterval{
        0}; // Excess PGS bytes granted each GGI (due to minislot roundup)
    uint32_t m_firstPgsGrantFrameOffset{0}; // Offset from first Alloc Start Time

    std::map<uint16_t, uint32_t> m_requests;     // track byte requests
    Ptr<AggregateServiceFlow> m_asf{nullptr};    // Supports only single ASF
    Ptr<ServiceFlow> m_sf{nullptr};              // Pointer to single SF
    Ptr<const ServiceFlow> m_classicSf{nullptr}; // Pointer to classic SF
    Ptr<const ServiceFlow> m_llSf{nullptr};      // Pointer to low latency SF
    TokenBucketState m_tokenBucketState;
    Time m_peakRateTokenInterval;    // Peak rate token bucket burst size
    uint32_t m_peakRateBurstSize{0}; // Peak rate bucket in bytes
    MapReport m_mapReport;           // structure to report on CMTS and MAP state

    /**
     * The trace source fired when a MAP message is generated
     */
    TracedCallback<MapReport> m_mapTrace;
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_CMTS_UPSTREAM_SCHEDULER */
