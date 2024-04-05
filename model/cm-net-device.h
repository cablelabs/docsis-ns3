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
 *   Joey Padden <j.padden@cablelabs.com>
 *   Takashi Hayakawa <t.hayakawa@cablelabs.com>
 */

#ifndef DOCSIS_CM_NET_DEVICE_H
#define DOCSIS_CM_NET_DEVICE_H

#include "docsis-configuration.h"
#include "docsis-net-device.h"
#include "queue-protection.h"

#include "ns3/drop-tail-queue.h"
#include "ns3/queue-item.h"
#include "ns3/traced-value.h"

#include <deque>
#include <queue>
#include <utility>

namespace ns3
{

class RandomVariableStream;

namespace docsis
{

/**
 * \ingroup docsis
 * \class CmPipeline
 * \brief A data structure to hold request, grant, or transmit pipeline
 * information
 *
 * Stores data counters with eligibility timestamps associated with them
 */
class CmPipeline
{
  public:
    /**
     * \return sum of values for elements whose timestamps <= deadline
     */
    uint32_t GetEligible(Time deadline) const;
    /**
     * \return sum of values for elements whose timestamps <= Simulator::Now ()
     */
    uint32_t GetEligible() const;
    /**
     * \return sum of values for all elements in the pipeline
     */
    uint32_t GetTotal() const;
    /**
     * Return the number of elements in the pipeline
     *
     * \return number of elements in the pipeline
     */
    uint32_t GetSize() const;
    /**
     * Add an element to the pipeline.
     *
     * \param value Add an element to the pipeline
     * \param eligible deadline time to consider for eligibility
     */
    void Add(uint32_t value, Time eligible);
    /**
     * Peek element from front of pipeline.
     *
     * \return element element peeked from front of pipeline
     */
    std::pair<uint32_t, Time> Peek() const;
    /**
     * Pop (remove) element from front of pipeline and return it.
     *
     * \return element removed from front of pipeline
     */
    std::pair<uint32_t, Time> Pop();
    /**
     * Remove value from front of pipeline.  Any elements that become empty
     * are also removed.
     *
     * \param value quantity to remove from the front
     */
    void Remove(uint32_t value);
    /**
     * Clear the contents of the pipeline
     */
    void Clear();

  private:
    std::deque<std::pair<uint32_t, Time>> m_pipeline;
};

/**
 * \ingroup docsis
 * \class CmNetDevice
 * \brief A model for upstream devices (i.e. cable modems)
 *
 * This CmNetDevice class models an upstream (CM) device.  It is a layer-2
 * device and must be bridged to another device (such as CsmaNetDevice) that
 * supports bridging.
 */
class CmNetDevice : public DocsisNetDevice
{
  public:
    /**
     * \brief Get the TypeId
     *
     * \return The TypeId for this class
     */
    static TypeId GetTypeId();

    /**
     * Constructor for CmNetDevice
     */
    CmNetDevice();

    /**
     * Destructor for CmNetDevice
     */
    ~CmNetDevice() override;

    /**
     * Handle a scheduled MAP.  This method is typically scheduled by the
     * notional CmtsUpstreamScheduler.
     */
    void HandleMapMsg(MapMessage msg);

    /**
     * Schedule burst preparation and data transmission events for grants
     */
    void ScheduleGrantEvents(uint16_t sfid, std::vector<Grant> grantList);

    /**
     * Add upstream aggregate service flow.  This operation must be done before
     * the simulation is started.
     *
     * Either an AggregateServiceFlow or a single ServiceFlow object should
     * be present, but not both.  It is a simulation error (misconfiguration)
     * to try to set both.
     *
     * \param asf pointer to the AggregateServiceFlow object
     */
    void SetUpstreamAsf(Ptr<AggregateServiceFlow> asf);
    /**
     * Add upstream single service flow.  This operation must be done before
     * the simulation is started.
     *
     * Either an AggregateServiceFlow or a single ServiceFlow object should
     * be present, but not both.  It is a simulation error (misconfiguration)
     * to try to set both.
     *
     * \param sf pointer to the ServiceFlow object
     */
    void SetUpstreamSf(Ptr<ServiceFlow> sf);
    /**
     * Get upstream aggregate service flow (if present).
     * \return pointer to the AggregateServiceFlow object, if present
     */
    Ptr<const AggregateServiceFlow> GetUpstreamAsf() const;
    /**
     * Get upstream service flow (if present).
     * \return pointer to the ServiceFlow object, if present
     */
    Ptr<const ServiceFlow> GetUpstreamSf() const;

    // Documented in DocsisNetDevice class
    Time ExpectedDelay() const override;
    // Documented in DocsisNetDevice class
    std::pair<Time, uint32_t> GetLoopDelayEstimate() const override;

    /**
     * Add a time sample to the loop delay estimate
     * \param sample time sample to add
     */
    void AddLoopDelayEstimate(Time sample);

    // Documented in ns3::NetDevice base class
    bool Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber) override;

    // Documented in ns3::NetDevice base class
    bool SendFrom(Ptr<Packet> packet,
                  const Address& source,
                  const Address& dest,
                  uint16_t protocolNumber) override;

    /**
     * Assign a fixed random variable stream number to the random variables
     * used by this model.  Return the number of streams (possibly zero) that
     * have been assigned.
     *
     * \param stream first stream index to use
     * \return the number of stream indices assigned by this model
     */
    int64_t AssignStreams(int64_t stream);

    /**
     * \brief Structure for reporting classic SFID state information
     */
    struct CGrantState
    {
        uint16_t sfid;    //!< service flow ID
        uint32_t granted; //!< given grant (bytes)
        uint32_t used;    //!< used grant (bytes)
        uint32_t unused;  //!< accumulated unused grant (bytes)
        uint32_t queued;  //!< queued state (bytes)
        Time delay;       //!< current AQM delay estimate for this SFID
        double dropProb;  //!< drop probability value for this SFID
    };

    /**
     * \brief Structure for reporting low latency SFID state information
     */
    struct LGrantState
    {
        uint16_t sfid;          //!< service flow ID
        uint32_t granted;       //!< given grant (bytes)
        uint32_t used;          //!< used grant (bytes)
        uint32_t unused;        //!< accumulated unused grant (bytes)
        uint32_t queued;        //!< queued state (bytes)
        Time delay;             //!< current AQM delay estimate for this SFID
        double markProb;        //!< drop probability value for this SFID
        double coupledMarkProb; //!< drop probability value for this SFID
    };

    /**
     * TracedCallback signature for CM state reporting
     *
     * \param [in] state classic SFID state report
     */
    typedef void (*CGrantStateTracedCallback)(CGrantState state);

    /**
     * TracedCallback signature for CM state reporting
     *
     * \param [in] state low latency SFID state report
     */
    typedef void (*LGrantStateTracedCallback)(LGrantState state);

    /**
     * TracedCallback signature for grant reporting
     *
     * \param [in] value value traced
     */
    typedef void (*UintegerTracedCallback)(uint32_t value);

  protected:
    friend class DualQueueCoupledAqm;
    /**
     * \brief Dispose of the object
     */
    void DoDispose() override;

    /**
     * \brief Initialize the object
     */
    void DoInitialize() override;

    /*
     * The number of bytes that exist in the device include any unsent
     * bytes from the fragmented packet (if any), and bytes that exist
     * in the local FIFO staging queue m_queue, and bytes in the AQM.
     *
     * A CmNetDevice needs to keep track of two quantities:  framed MAC
     * bytes (which must be serialized into granted minislots) and
     * Data PDU bytes (which are used for rate shaping of requests)
     *
     * By default, these quantities account for MAC header overheads,
     * Ethernet header, and padding overheads (MAC Segment Header
     * is not modeled, however); i.e. the default is to count MAC Frame
     * bytes.
     *
     * If the parameter includeMacHeaders is false, the quantities
     * return Data PDU bytes (without MAC header bytes counted).
     *
     * \param includeMacHeaders if true, return MAC frame bytes
     * \return number of corresponding bytes in the device
     */
    uint32_t GetCUnsentBytes(bool includeMacHeaders = true) const;
    uint32_t GetCDeviceBytes(bool includeMacHeaders = true) const;
    uint32_t GetCAqmBytes(bool includeMacHeaders = true) const;
    uint32_t GetLUnsentBytes(bool includeMacHeaders = true) const;
    uint32_t GetLDeviceBytes(bool includeMacHeaders = true) const;
    uint32_t GetLAqmBytes(bool includeMacHeaders = true) const;

  private:
    void Reset();

    /**
     * \brief Information to store for assisting with packing D3.1 minislots
     */
    struct MapState
    {
        uint32_t m_unused{0};          //!< Cumulative unused bytes
        uint32_t m_grantBytesInMap{0}; //!< Grant bytes for the MAP interval
    };

    void MakeRequest(uint16_t sfid);
    uint32_t ShapeRequest(uint32_t request);
    void InitializeTokenBucket();
    void UpdateTokenBucket();
    void AskForNextPacket(Time howSoon);
    /**
     * Called by HandleGrantTimerWithMapState () after MAP state is loaded.
     *
     * This method starts to send data allowed by the grant.
     */
    void HandleGrantTimer();
    Time GetNextGrantTime(double grantTimeMax);
    uint32_t GetFrameForMinislot(uint32_t minislot);
    uint32_t MinislotsRemainingInFrame(uint32_t offset) const;

    uint32_t GetFramingSize() const;

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

    void RemoveReceivedDocsisHeader(Ptr<Packet> packet) override;
    void AddDocsisHeader(Ptr<Packet> packet) override;
    uint32_t GetMacFrameSize(uint32_t sduSize) const override;

    void SendImmediatelyIfAvailable();
    void TransmitComplete();
    // Used in Docsis 3.1 models
    void StartDecoding(Ptr<Packet> packet) override;
    void EndDecoding(Ptr<Packet> packet);

    // Used for tracking grant usage
    void InsertGrantInfoToQueue(uint32_t grant_given, uint32_t grant_unused);

    /**
     * Calculate a (random) time that the cable modem notionally makes
     * a request to the CMTS.  The spreading of this time across a MAP
     * interval allows for the modeling that some requests miss the deadline
     * to be serviced in time for a particular MAP interval.
     *
     * \return a Time value between 0 and the offset in time between the start
     *         of the MAP interval and the last frame in the MAP
     */
    Time GetInitialRequestOffset();

    /**
     * Calculate the future time at which a transmission of bytes will complete
     * (taking into account serialization delay for DOCSIS 3.0 model, and the
     * current MAP state for DOCSIS 3.1 model).
     *
     * \param bytes the number of bytes to determine completion time
     * \param mapState current MAP state of the model
     * \return the amount of time until transmission completes for bytes
     */
    Time GetCompletionTime(uint32_t bytes, struct MapState mapState) const;

    uint32_t SendOutFromCQueue(Ptr<Packet> packet,
                               const Address& src,
                               const Address& dest,
                               uint16_t protocolNumber,
                               uint32_t eligibleBytes);
    uint32_t SendOutFromLQueue(Ptr<Packet> packet,
                               const Address& src,
                               const Address& dest,
                               uint16_t protocolNumber,
                               uint32_t eligibleBytes);
    uint32_t SendOutFragmentFromCQueue();
    uint32_t SendOutFragmentFromLQueue();

    void SendFrame(uint16_t sfid, uint32_t frameNumber, uint32_t minislotsToSend);
    void SendFrameFromCQueue(uint32_t frameNumber, uint32_t minislotsToSend);
    void SendFrameFromLQueue(uint32_t frameNumber, uint32_t minislotsToSend);
    void PrepareBurst(uint16_t sfid, uint32_t minislotsToPrepare);
    void PrepareBurstFromCQueue(uint32_t minislotsToPrepare);
    void PrepareBurstFromLQueue(uint32_t minislotsToPrepare);

    void LoadMapState(uint16_t sfid, struct MapState mapState);
    void DumpGrant(uint16_t sfid);

    // Internal variables
    bool m_pointToPointMode{false};
    bool m_pointToPointBusy{false};
    MapState m_cMapState;
    MapState m_lMapState;

    Ptr<AggregateServiceFlow> m_asf{nullptr};
    Ptr<ServiceFlow> m_sf{nullptr};
    Ptr<const ServiceFlow> m_classicSf{nullptr};
    Ptr<const ServiceFlow> m_llSf{nullptr};

    Time m_loopDelay{Seconds(0)}; //!< Loop delay estimate
    double m_loopDelayOffset{0};  //!< Fraction of MAP interval to add to loop delay

    uint32_t m_msrTokens;  //!< accumulated Max Sustained Rate tokens for single SF
    uint32_t m_peakTokens; //!< accumulated Peak Rate tokens for single SF
    Time m_lastUpdateTime; //!< Time of last update of request pipeline

    // DOCSIS configuration variables tied to attributes
    uint32_t m_freeCapacityMean;      //!< Model congestion on the DOCSIS link by limiting available
                                      //!< upstream capacity (bps)
    uint32_t m_freeCapacityVariation; //!< Model congestion variation on the DOCSIS link by
                                      //!< specifying a percentage bound (RANGE: 0 - 100)
    Time m_burstPreparationTime;      //!< Time offset for burst preparation

    uint32_t m_grantBytes{0};       //!< accumulated/granted tokens
    uint32_t m_grantBytesNext{0};   //!< granted tokens for next interval (DOCSIS 3.0)
    CmPipeline m_lGrantPipeline;    //!< pipeline of upcoming L-queue grants
    CmPipeline m_cGrantPipeline;    //!< pipeline of upcoming C-queue grants
    CmPipeline m_cTransmitPipeline; //!< track internal data eligible to be sent
    CmPipeline m_lTransmitPipeline; //!< track internal data eligible to be sent
    CmPipeline m_lRequestPipeline;  //!< Previous requests outstanding
    CmPipeline m_cRequestPipeline;  //!< Previous requests outstanding

    /**
     * Encapsulate the state of a packet fragment
     */
    struct PacketFragment
    {
        void Clear()
        {
            m_fragPkt = nullptr;
            m_fragSdu = nullptr;
            m_fragSentBytes = 0;
            m_fragSrc = Address();
            m_fragDest = Address();
            m_fragProtocolNumber = 0;
            m_lastSduSize = 0;
        }

        Ptr<Packet> m_fragPkt;         //!< Fragmented packet across multiple grants
        Ptr<Packet> m_fragSdu;         //!< Copy of original SDU for emulation use
        uint32_t m_fragSentBytes;      //!< # of bytes Fragment
        Address m_fragSrc;             //!< Store value of argument to SendFrom ()
        Address m_fragDest;            //!< Store value of argument to SendFrom ()
        uint16_t m_fragProtocolNumber; //!< Store value of argument to Send ()
        uint32_t m_lastSduSize;        //!< Store size of last SDU passed to Send ()
    };

    PacketFragment m_lFragment; //!< Fragmented packet state
    PacketFragment m_cFragment; //!< Fragmented packet state

    Ptr<RandomVariableStream> m_requestTimeVariable;

    /**
     * The device makes use of FIFO DropTailQueues to store data that is
     * notionally being prepared for transmission one or more symbols later.
     */
    Ptr<DropTailQueue<QueueDiscItem>> m_cQueue;
    Ptr<DropTailQueue<QueueDiscItem>> m_lQueue;

    uint32_t m_cQueueFramedBytes{0}; //!< Accounts for MAC Frame bytes
    uint32_t m_cQueuePduBytes{0};    //!< Accounts for Data PDU bytes
    uint32_t m_lQueueFramedBytes{0}; //!< Accounts for MAC Frame bytes
    uint32_t m_lQueuePduBytes{0};    //!< Accounts for Data PDU bytes

    uint32_t m_cAqmFramedBytes{0}; //!< Accounts for MAC Frame bytes
    uint32_t m_cAqmPduBytes{0};    //!< Accounts for Data PDU bytes
    uint32_t m_lAqmFramedBytes{0}; //!< Accounts for MAC Frame bytes
    uint32_t m_lAqmPduBytes{0};    //!< Accounts for Data PDU bytes

    // Trace sinks for queue events
    void InternalClassicEnqueueCallback(Ptr<const QueueDiscItem> item);
    void InternalClassicDequeueCallback(Ptr<const QueueDiscItem> item);
    void InternalClassicDropCallback(Ptr<const QueueDiscItem> item);
    void InternalLlEnqueueCallback(Ptr<const QueueDiscItem> item);
    void InternalLlDequeueCallback(Ptr<const QueueDiscItem> item);
    void InternalLlDropCallback(Ptr<const QueueDiscItem> item);
    void RootDropAfterDequeueCallback(Ptr<const QueueDiscItem> item, const char* reason);

    /**
     * The trace source fired when a packet is sent out, to report on the
     * internal state of the token bucket.
     */
    TracedCallback<uint32_t, uint32_t, uint32_t, uint32_t> m_stateTrace;
    /**
     * Trace source fired to report classic SFID state at end of MAP interval
     */
    TracedCallback<CGrantState> m_cGrantStateTrace;
    /**
     * Trace source fired to report low latency SFID state at end of MAP interval
     */
    TracedCallback<LGrantState> m_lGrantStateTrace;
    /**
     * The trace source fired when a grant for the classic service flow
     * is received (units of bytes).
     */
    TracedCallback<uint32_t> m_traceCGrantReceived;
    /**
     * The trace source fired when a grant for the low latency service flow
     * is received (units of bytes).
     */
    TracedCallback<uint32_t> m_traceLGrantReceived;
    /**
     * The trace source fired when a classic packet is directly dequeued from
     * the Dual Queue
     */
    TracedCallback<Time> m_traceClassicSojourn; //!< Classic sojourn time
    /**
     * The trace source fired when a LL packet is directly dequeued from
     * the Dual Queue
     */
    TracedCallback<Time> m_traceLlSojourn; //!< LL sojourn time
    /**
     * The trace source fired when a classic request is made
     */
    TracedCallback<uint32_t> m_traceCRequest;
    /**
     * The trace source fired when a low latency request is made
     */
    TracedCallback<uint32_t> m_traceLRequest;
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_CM_NET_DEVICE_H */
