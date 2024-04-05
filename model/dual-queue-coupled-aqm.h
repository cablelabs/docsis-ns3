/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
 * Copyright (c) 2017-2020 Cable Television Laboratories, Inc. (DOCSIS changes)
 *
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
 *
 * Authors: Shravya K.S. <shravya.ks0@gmail.com>
 *          Tom Henderson <tomh@tomh.org> (Extensions by CableLabs LLD project)
 */

#ifndef DOCSIS_DUAL_QUEUE_COUPLED_AQM_H
#define DOCSIS_DUAL_QUEUE_COUPLED_AQM_H

#include "docsis-configuration.h"

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/queue-disc.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/timer.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/traced-callback.h"
#include "ns3/traffic-control-layer.h"

#include <bitset>
#include <deque>
#include <utility>

namespace ns3
{

namespace docsis
{

class QueueProtection;
class DocsisQueueDiscItem;

/**
 * \brief Enumeration of the queue protection outcomes
 */
enum class QueueProtectionOutcome
{
    SUCCESS,  /*!< Success (i.e. permit) */
    SANCTION, /*!< Sanction */
};

/**
 * \ingroup docsis
 *
 * \brief Implements DualQ Coupled AQM queue discipline, DOCSIS variant
 *
 * This implementation corresponds to Annexes N through P of
 * DOCSIS 3.1 MAC and Upper Layer Protocols Interface Specification
 * CM-SP-MULPIv3.1-119
 *
 * This model is designed to run only within the context of a DocsisNetDevice
 * object (i.e. not as part of the ns-3 traffic control layer).
 */
class DualQueueCoupledAqm : public QueueDisc
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief DualQueueCoupledAqm Constructor
     */
    DualQueueCoupledAqm();

    /**
     * \brief DualQueueCoupledAqm Destructor
     */
    ~DualQueueCoupledAqm() override;

    /**
     * \brief Latency estimation policy for LL queue
     * The approach to estimating future latency for L queue is
     * attribute value 'LlEstimator'
     */
    enum LlEstimatorPolicy
    {
        QDELAY_COUPLED_L, /*!< Low Latency DOCSIS qdelayCoupledL */
        QDELAY_COUPLED_V, /*!< Low Latency DOCSIS qdelayCoupledV */
    };

    /**
     * \brief Get the current size of the Low Latency queue in bytes
     *
     * This method will return either the total Data PDU bytes in the
     * low latency queue (if the argument passed to the includeMacHeaders
     * parameter is false), or the total MAC Frame bytes in the low
     * latency queue (if the argument passed to the includeMacHeaders
     * is true).  By default, if no argument is passed, the MAC frame
     * bytes will be counted.  Data PDU and MAC Frame terminology corresponds
     * to Figure 18 of the MULPIv3.1 specification.
     *
     * \param includeMacHeaders whether to include per-PDU MAC header bytes
     * \returns The Low Latency queue size in bytes
     */
    uint32_t GetLowLatencyQueueSize(bool includeMacHeaders = true) const;

    /**
     * \brief Get the current size of the classic queue in bytes
     *
     * This method will return either the total Data PDU bytes in the
     * classic queue (if the argument passed to the includeMacHeaders
     * parameter is false), or the total MAC Frame bytes in the
     * classic queue (if the argument passed to the includeMacHeaders
     * is true).  By default, if no argument is passed, the MAC frame
     * bytes will be counted.  Data PDU and MAC Frame terminology corresponds
     * to Figure 18 of the MULPIv3.1 specification.
     *
     * \param includeMacHeaders whether to include per-PDU MAC header bytes
     * \returns The classic queue size in bytes
     */
    uint32_t GetClassicQueueSize(bool includeMacHeaders = true) const;

    /**
     * \brief Set a Queue Protection pointer.  Overwrites existing pointer
     *        if already set.
     *
     * \param qp The QueueProtection pointer
     */
    void SetQueueProtection(Ptr<QueueProtection> qp);

    /**
     * \brief Get a Queue Protection pointer.
     *
     * \return The QueueProtection pointer
     */
    Ptr<QueueProtection> GetQueueProtection() const;

    /**
     * Provide an external method to provide qdelaySingle () method that
     * relies on the rate shaping state of the underlying device.
     *
     * \param qDelaySingleCallback the callback function
     */
    void SetQDelaySingleCallback(Callback<Time> qDelaySingleCallback);

    /**
     * Provide an external method to provide loopDelay () method for
     * smoothed estimate of the multiple access delay.
     *
     * \param loopDelayCallback the callback function
     */
    void SetLoopDelayCallback(Callback<std::pair<Time, uint32_t>> loopDelayCallback);

    /**
     * Add aggregate service flow definition.  This operation must be done before
     * the simulation is started.
     *
     * Either an AggregateServiceFlow or a single ServiceFlow object should
     * be present, but not both.  It is a simulation error (misconfiguration)
     * to try to set both.
     *
     * \param asf pointer to the AggregateServiceFlow object
     */
    void SetAsf(Ptr<AggregateServiceFlow> asf);

    /**
     * Add single service flow definition.  This operation must be done before
     * the simulation is started.
     *
     * Either an AggregateServiceFlow or a single ServiceFlow object should
     * be present, but not both.  It is a simulation error (misconfiguration)
     * to try to set both.
     *
     * \param sf pointer to the ServiceFlow object
     */
    void SetSf(Ptr<ServiceFlow> sf);

    /**
     *  Get most recent estimate of the classic queuing delay (updated every interval)
     * \param size size (bytes) of additional packet to consider in delay
     * \return queuing delay
     */
    Time GetClassicQueuingDelay(uint32_t size = 0) const;

    /**
     * Get current estimate of the Low Latency queuing delay.
     *
     * Depending on the estimator, this method will return
     * QDelayCoupledL() or QDelayCoupledV().
     */
    Time GetLowLatencyQueuingDelay();

    /**
     *  \brief Return the Low Latency queue delay estimate
     *
     * This corresponds to qdelayCoupledL(byte_length) in Annex O of the
     * specification.  In the specification, the units are ns, but in ns-3,
     * a Time object is returned, which can be converted to integer ns
     * via GetNanoSeconds ().
     *
     * \param byteLength byte length to consider in delay
     * \return first-in Low Latency queuing delay
     */
    Time QDelayCoupledL(uint32_t byteLength) const;

    /**
     * \brief Get the classic drop probability.
     * Note:  may be > 1
     * \return classic drop probability
     */
    double GetClassicDropProbability() const;

    /**
     * \brief Calculate the Low Latency internal marking probability (probNative)
     *
     * Implements the function defined in Annex O.1 of the specification.
     * The delay is passed in as a parameter so that the caller can control
     * the value of delay to use in the calculation.
     * \param qdelay Low Latency queue delay to use
     * \return Low Latency internal marking probability
     */
    double CalcProbNative(Time qdelay) const;

    /**
     * \brief Get the Low Latency internal marking probability (probNative)
     * \return Low Latency internal marking probability
     */
    double GetProbNative() const;

    /**
     * \brief Get the Low Latency coupled marking probability from C queue (probCL)
     * \return Low Latency coupled marking probability
     */
    double GetProbCL() const;

    // Reasons for dropping packets
    static constexpr const char* UNFORCED_CLASSIC_DROP =
        "Unforced drop in classic queue"; //!< Early probability drops: proactive
    static constexpr const char* FORCED_DROP =
        "Forced drop"; //!< Drops due to queue limit: reactive
    static constexpr const char* UNFORCED_CLASSIC_MARK =
        "Unforced classic mark"; //!< Unforced mark in classic queue
    static constexpr const char* UNFORCED_LL_MARK =
        "Unforced mark in Low Latency queue"; //!< Unforced mark in classic queue
    static constexpr const char* UNFORCED_LL_DROP =
        "Unforced drop in Low Latency queue"; //!< Unforced drop in classic queue
    static constexpr const char* UNCLASSIFIED_DROP =
        "Unclassified drop"; //!< No packet filter able to classify packet

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
     * Attempt to dequeue from Classic queue
     * \return the item dequeued, or 0 if unsuccessful
     */
    Ptr<QueueDiscItem> ClassicDequeue();

    /**
     * Attempt to dequeue from Low Latency queue
     * \return the item dequeued, or 0 if unsuccessful
     */
    Ptr<QueueDiscItem> LowLatencyDequeue();

    /**
     * \brief Divide the integer SchedulingWeight (1..255) by 256 to yield
     * a floating point scheduling weight.
     *
     * \return value between 0 and 1 representing the Low Latency scheduling weight
     */
    double GetWeight() const;

    /**
     * Set the value of DscpOverwrite for the L-queue
     * \param overwrite the DscpOverwrite value
     */
    void SetLowLatencyDscpOverwrite(DscpOverwrite overwrite);

    /**
     * Get the value of DscpOverwrite for the L-queue
     * \return the DscpOverwrite value
     */
    DscpOverwrite GetLowLatencyDscpOverwrite() const;

    /**
     * Set the value of DscpOverwrite for the C-queue
     * \param overwrite the DscpOverwrite value
     */
    void SetClassicDscpOverwrite(DscpOverwrite overwrite);

    /**
     * Get the value of DscpOverwrite for the C-queue
     * \return the DscpOverwrite value
     */
    DscpOverwrite GetClassicDscpOverwrite() const;

    /**
     * \brief Burst types
     */
    enum BurstStateT
    {
        NO_BURST,
        IN_BURST,
        IN_BURST_PROTECTING,
    };

    /**
     * Callback signature for enqueue state
     * \param qDelay queue delay
     * \param qSize queue size
     * \param dropProb drop probability
     * \param burstReset burst reset counter
     * \param state burst state
     */
    typedef void (*DualQEnqueueStateTracedCallback)(Time qDelay,
                                                    uint32_t qSize,
                                                    double dropProb,
                                                    uint32_t burstReset,
                                                    enum BurstStateT state);

    /**
     * Callback signature for CalculateP() state
     * \param qDelay queue delay
     * \param qDelayOld old queue delay
     * \param p p value
     * \param classicProb classic drop probability
     * \param baseProb base probability
     * \param probCL probCL coupled probability
     */
    typedef void (*CalculatePStateTracedCallback)(Time qDelay,
                                                  Time qDelayOld,
                                                  double p,
                                                  double classicProb,
                                                  double baseProb,
                                                  double probCL);

    /**
     * Callback signature for Low Latency queue delay trace
     * \param qDelay queue delay of LL queue
     * \param size size of packet that was added to queue delay calculation
     */
    typedef void (*DualQLlQueueDelayTracedCallback)(Time qDelay, uint32_t size);

    /**
     * Callback signature for Low Latency queue dequeue trace
     * \param sojourn Sojourn time of packet
     * \param delayEstimate Queue delay estimate of packet at time of enqueue
     * \param classicDelay Classic queueing delay estimate upon dequeue
     * \param isMarked Whether the packet is CE marked or not
     * \param size size of packet
     */
    typedef void (*DualQLlDequeueTracedCallback)(Time sojourn,
                                                 Time delayEstimate,
                                                 Time classicDelay,
                                                 bool isMarked,
                                                 uint32_t size);

    /**
     * TracedCallback signature for AlignVq trace
     *
     * \param [in] actualLen Current actual queue length (bytes)
     * \param [in] Time Current virtual queue (time)
     * \param [in] avgLen Current average queue length (bytes)
     * \param [in] allowedLen Current allowed average queue length (bytes)
     * \param [in] aligned Whether virtual queue was aligned
     */
    typedef void (*AlignVqTracedCallback)(uint32_t actualLen,
                                          Time vq,
                                          uint32_t avgLen,
                                          uint32_t allowedLen,
                                          bool aligned);

  protected:
    /**
     * \brief Dispose of the object
     */
    void DoDispose() override;

  private:
    // Four methods declared in QueueDisc base class (traffic-control module)
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    Ptr<const QueueDiscItem> DoPeek() override;
    bool CheckConfig() override;

    bool SelectLlQueue();

    Time GetMinTh() const;
    Time GetMaxTh() const;

    /**
     * \brief Enumeration of the queue types, for internal code readability
     */
    enum QueueType
    {
        CLASSIC, /*!< Index for classic queue */
        LL,      /*!< Index for low latency queue */
    };

    /**
     * Drain the virtual queue (if bytes exist to drain) and align the
     * virtual queue to the difference between average (actual) queue and
     * allowed average queue, if necessary.
     */
    void AlignVq();

    /**
     * Immediate AQM Data Path (DOCSIS specification Annex N.4)
     * \param item QueueDiscItem to operate on
     * \param probNative probNative value from the algorithm
     * \return true if EXIT_CE, false if EXIT_FWD
     */
    bool Iaqm(Ptr<QueueDiscItem> item, double probNative);

    /**
     * Whether to repeat an operation (e.g. dropping or marking)
     * so that it will recur with a certain likelihood.  This method
     * averages likelihood over all invocations.
     *
     * \param likelihood the desired likelihood
     * \return whether the operation should be performed
     */
    bool Recur(double likelihood);

    /**
     * \brief Initialize the queue parameters.
     */
    void InitializeParams() override;

    /**
     * \brief Return the classic queue delay estimate
     *
     * This corresponds to qdelayCoupledV(psize) in Annex O of the
     * specification.  In the specification, the units are s, but in ns-3,
     * a Time object is returned, which can be converted to floating point
     * seconds via GetSeconds(), and integer seconds via C++ round() or floor()
     *
     * This method adds pSize bytes to the internal virtual queue length.
     *
     * \param pSize packet size to add to the virtual queue length
     * \return virtual queue delay estimate
     */
    Time QDelayCoupledV(uint32_t pSize);

    /**
     * \brief Return the classic queue delay estimate
     *
     * This corresponds to qdelayCoupledC(byte_length) in Annex O of the
     * specification.  In the specification, the units are s, but in ns-3,
     * a Time object is returned, which can be converted to floating point
     * seconds via GetSeconds(), and integer seconds via C++ round() or floor()
     *
     * \param byteLength queue length in bytes
     * \return classic queue delay estimate
     */
    Time QDelayCoupledC(uint32_t byteLength);

    /**
     * \brief Periodically update dropping and marking probabilities (DOCSIS)
     */
    void CalculateDropProb();

    /**
     * \brief M.3 PIE AQM Data Path
     * \param item QueueDiscItem to consider
     * \return true if item should be dropped
     */
    bool DropEarly(Ptr<QueueDiscItem> item);

    /**
     * \brief Attempt to enqueue a classic packet
     * \param docsisItem (classic) DocsisQueueDiscItem to enqueue
     * \return true if item was successfully enqueued
     */
    bool ClassicEnqueue(Ptr<DocsisQueueDiscItem> docsisItem);

    /**
     * \brief Decrement the internal byte count of the virtual queue
     * \param pSize size (in bytes) to decrement
     */
    void DecreaseVq(uint32_t pSize);

    // ** Variables supplied by user
    bool m_iaqmOn;                          //!< Corresponds to IAQM_ON in spec
    Time m_latencyTarget;                   //!< Queue delay target for Classic traffic
    Time m_sUpdate;                         //!< Start time of the update timer
    Time m_interval;                        //!< Interval for calling CalculateP ()
    QueueSize m_classicBufferSize;          //!< Queue size for classic queue
    QueueSize m_lowLatencyConfigBufferSize; //!< Queue size for low latency queue
    double m_alpha;                         //!< Parameter to PI Square controller
    double m_beta;                          //!< Parameter to PI Square controller
    uint16_t m_lgRange;                     //!< Log2 (IAQM ramp range in ns)
    Time m_maxTh;                           //!< Maximum latency for internal AQM ramp function
    Time m_minTh;                           //!< Minimum latency for internal AQM ramp function
    bool m_coupled;                         //!< COUPLED parameter
    double m_couplingFactor;                //!< Coupling factor
    uint32_t m_schedulingWeight;            //!< Weight for weighted DRR

    // ** Variables maintained by DualQ Coupled PI Square
    TracedValue<double> m_baseProb;        //!< Base probability
    TracedValue<double> m_classicDropProb; //!< Classic drop probability
    TracedValue<double> m_probCL;          //!< Coupled marking probability
    TracedValue<double> m_probNative;      //!< Internal ramp AQM for Low Latency queue
    Time m_prevq;                          //!< Previous value of curq (qDelayOld)
    EventId m_updateEvent; //!< Event used to decide the decision of interval of drop probability
                           //!< calculation
    Ptr<UniformRandomVariable> m_uv;        //!< Rng stream
    double m_count;                         //!< Internal counter in recur method
    Ptr<QueueProtection> m_queueProtection; //!< Queue protection pointer

    Callback<Time> m_qDelaySingleCallback;                   //!< Callback for qDelaySingle
    Callback<std::pair<Time, uint32_t>> m_loopDelayCallback; //!< Callback for loop delay
    Ptr<AggregateServiceFlow> m_asf{nullptr};                //!< Pointer to ASF
    Ptr<ServiceFlow> m_sf{nullptr};                          //!< Pointer to single SF

    std::bitset<2> m_drrQueues;   //!< bitset for weighted DRR
    uint32_t m_drrQuantum;        //!< quantum for weighted DRR
    uint32_t m_maxFrameSize;      //!< MAX_FRAME_SIZE (bytes)
    uint32_t m_classicDeficit;    //!< deficit counter for DRR
    uint32_t m_llDeficit;         //!< deficit counter for DRR
    uint32_t m_intervalBitsL;     //!< count of _bits_ for rate estimation
    Time m_cqEstimateAtUpdate;    //!< latency estimate
    uint32_t m_llDataPduBytes;    //!< LL queue size w/o MAC hdrs
    DataRate m_maxRate;           //!< MAX_RATE from Annex O
    Time m_vq;                    //!< Variable VQ (virtual queue) from Annex 0
    Time m_tLast;                 //!< Variable t_last from Annex 0
    uint32_t m_averageAqBytes{0}; //!< For EWMA (bytes)

    // Traces
    TracedValue<uint32_t> m_traceClassicBytes;          //!< Bytes in Classic queue
    TracedValue<uint32_t> m_traceLlBytes;               //!< Bytes in Low Latency queue
    TracedValue<uint32_t> m_tracePieQueueBytes;         //!< queue_.byte_length()
    TracedCallback<Time> m_traceClassicSojourn;         //!< Classic sojourn time
    TracedCallback<Time> m_traceLlSojourn;              //!< LL sojourn time
    TracedCallback<Time, uint32_t> m_llQueueDelayTrace; //!< LL queue delay
    TracedCallback<Time, Time, Time, bool, uint32_t> m_llDequeueTrace; //!< LL queue dequeue trace
    TracedCallback<Time, uint32_t, double, uint32_t, enum BurstStateT> m_enqueueStateTrace;
    TracedCallback<Time, Time, double, double, double, double> m_calculatePStateTrace;
    TracedCallback<uint32_t, Time, uint32_t, uint32_t, bool> m_alignVqTrace; //!< alignVQ trace

    // variables for DOCSIS PIE mode
    Time m_burstAllowance;    //!< Current max burst value in seconds that is allowed before random
                              //!< drops kick in
    Time m_maxBurst;          //!< Maximum burst allowed before random early dropping kicks in
    uint32_t m_burstReset;    //!< Used to reset value of burst allowance
    BurstStateT m_burstState; //!< Used to determine the current state of burst
    Time m_burstResetTimeout; //!< Time to wait to reset to INACTIVE
    double m_accuProb;        //!< Early drop variable
    uint32_t m_meanPktSize;   //!< Average packet size in bytes
    double m_probLow;         //!< De-randomization
    double m_probHigh;        //!< De-randomization

    DscpOverwrite m_llDscpOverwrite;      //!< L-queue DSCP overwrite
    DscpOverwrite m_classicDscpOverwrite; //!< C-queue DSCP overwrite

    TracedCallback<uint32_t> m_llArrivalTrace;      //!< Trace arrival of LL packet
    TracedCallback<uint32_t> m_classicArrivalTrace; //!< Trace arrival of C packet

    LlEstimatorPolicy m_llEstimator; //!< LL delay estimate policy
    Time m_vqInterval;               //!< VQ interval for alignment
    Timer m_alignVqTimer;            //!< Timer to schedule alignVQ()
    double m_lgVqEwmaAlpha;          //!< LG_VQ_EWMA_ALPHA in spec
    double m_aqConstant;             //!< Multiplicative term for GGR*GGI term
    Time m_vqMapInterval;            //!< MAP interval used in allowed AQ calculation
    uint16_t m_vqFramesPerMap;       //!< Frames per MAP used in allowed AQ calculation
    Time m_pgsGrantInterval;         //!< PGS grant interval used in allowed AQ calculation
};

} // namespace docsis
} // namespace ns3

#endif
