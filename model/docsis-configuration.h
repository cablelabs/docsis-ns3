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

#ifndef DOCSIS_CONFIGURATION_H
#define DOCSIS_CONFIGURATION_H

#include "ns3/data-rate.h"
#include "ns3/ipv4-header.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

#include <limits>
#include <list>

namespace ns3
{

namespace docsis
{

// This header defines types and objects used across multiple DOCSIS models

const uint8_t CLASSIC_SFID = 1;     //!< service flow ID for classic ServiceFlow
const uint8_t LOW_LATENCY_SFID = 2; //!< service flow ID for low latency ServiceFlow

/**
 * \brief Structure to hold values for the IP Type Of Service (DSCP) Overwrite
 *
 * The values are applied to an IPv4 header DS field according to section
 * C.2.2.7.9 in the DOCSIS specification:
 * \code
 * new-ip-tos = ((orig-ip-tos AND tos-and-mask) OR tos-or-mask)
 * \endcode
 */
struct DscpOverwrite
{
    uint8_t m_tosAndMask{0xff}; //!< AND mask value
    uint8_t m_tosOrMask{0x00};  //!< OR mask value
};

/**
 * This class corresponds to service flow encodings found in Section C.2.2
 * of the specification.  Separate classes are not defined for upstream
 * and downstream, nor for classic or low latency definitions; a more
 * generic class is defined and some of the fields are only valid
 * depending on the use.
 *
 * SF and ASF reference values and service identifiers are not modeled; the
 * SF identifier serves as a unique identifier.  The convention used is to
 * encode a specific SFID for classic (CLASSIC_SFID) and low latency
 * (LOW_LATENCY_SFID).  If the model evolves in the future to handle more than
 * two service flows, this convention will need to be changed and other
 * indicators of service flow type (such as AqmAlgorithm) may need to be
 * used.
 *
 * Parameters not presently used by the model are labeled as 'for future use'
 * or omitted.
 *
 * A current limitation of the model is that QoS parameters for Low Latency
 * Service Flows are unused (not used to rate shape any best effort requests).
 * These parameters (m_maxSustainedTrafficRate, m_peakRate, m_maxTrafficBurst)
 * should be left as defaults for the Low Latency Service Flow definition,
 * or the simulation will error exit.
 */
class ServiceFlow : public Object
{
  public:
    /**
     * \brief Get the TypeId
     * \return The TypeId for this class
     */
    static TypeId GetTypeId();
    /**
     * Constructor for ServiceFlow
     * \param sfId service flow ID
     */
    ServiceFlow(uint8_t sfId);
    /**
     * Destructor for ServiceFlow
     */
    ~ServiceFlow() override;

    // Individual parameters

    uint16_t m_sfid{
        0}; //!< C.2.2.5.2: Service Flow Identifier; either CLASSIC_SFID or LOW_LATENCY_SFID
    uint8_t m_priority{0};                    //!< C.2.2.7.1: Traffic Priority; for future use
    DataRate m_maxSustainedRate{DataRate(0)}; //!< C.2.2.7.2: Maximum Sustained Traffic Rate
    uint32_t m_maxTrafficBurst{3044};         //!< C.2.2.7.3: Maximum Traffic Burst
    DataRate m_minReservedRate{
        DataRate(0)}; //!< C.2.2.7.4:  Minimum Reserved Traffic Rate; for future use
    uint32_t m_minReservedRatePacketSize{
        0}; //!< C.2.2.7.5 Assumed Minimum Reserved Rate Packet Size; for future use
    Ipv4Header::DscpType m_tosOverwrite{
        Ipv4Header::DscpDefault};     //!< C.2.2.7.9 IP Type Of Service (DSCP) Overwrite
    DataRate m_peakRate{DataRate(0)}; //!< C.2.2.7.10: Peak Traffic Rate
    uint32_t m_targetBuffer{0};       //!< C.2.2.7.11.4: Target Buffer (bytes); if set to 0 on SF
                                //!< instantiation, defaults to expression (3) in MULPI C.2.2.7.11.4
                                //!< for LL SFs and 100 ms * (A)MSR for Classic or Individual SFs
    bool m_aqmDisable{false};      //!< C.2.2.7.15.1: SF AQM Disable
    uint8_t m_classicAqmTarget{0}; //!< C.2.2.7.15.2 Classic AQM Latency Target (ms); set to
                                   //!< non-zero value to override DualQueue default

    enum AqmAlgorithm
    {
        DEFAULT,    //!< Default for SF
        DOCSIS_PIE, //!< DOCSIS PIE
        IAQM,       //!< IAQM
    };

    AqmAlgorithm m_aqmAlgorithm{DEFAULT}; //!< C.2.2.7.15.3 AQM Algorithm; for future use
    uint16_t m_iaqmMaxThresh{0}; //!< C.2.2.7.15.4 Immediate AQM Max Threshold (us); set to non-zero
                                 //!< value to override DualQueue default
    uint8_t m_iaqmRangeExponent{
        0}; //!< C.2.2.7.15.5: Immediate AQM Range Exponent of Ramp Function; set to non-zero value
            //!< to override DualQueue default

    enum ServiceFlowSchedulingType
    {
        RESERVED,
        BEST_EFFORT = 2,             //!< value for best effort
        PROACTIVE_GRANT_SERVICE = 7, //!< value for PGS
    };

    ServiceFlowSchedulingType m_serviceFlowSchedulingType{
        RESERVED}; //!< C.2.2.8.2 Service Flow Scheduling Type; for future use
    DataRate m_guaranteedGrantRate{
        DataRate(0)}; //!< C.2.2.8.13 Guaranteed Grant Rate; for upstream PGS flow
    uint16_t m_guaranteedGrantInterval{
        0}; //!< C.2.2.8.14 Guaranteed Grant Interval (us); for upstream PGS flow; if zero, defaults
            //!< to an interval of one frame
  private:
    /**
     * This constructor is disabled to force users to initialize service flow ID
     */
    ServiceFlow() = delete;
};

/**
 * This class corresponds to aggregate service flow encodings found in
 * Section C.2.2.7 of the specification.
 *
 * This object should typically contain two (CLASSIC and LOW_LATENCY)
 * service flows, since there is API to accept a single ServiceFlow object.
 * However, an AggregateServiceFlow with a single service flow is acceptable.
 * In that case, the values for maximum sustained rate, peak rate, and maximum
 * traffic burst are stored at the AggregateServiceFlow level (possibly
 * duplicated in the ServiceFlow object).
 *
 * Parameters not presently used by the model are labeled as 'for future use'
 * or omitted.
 */
class AggregateServiceFlow : public Object
{
  public:
    /**
     * \brief Get the TypeId
     * \return The TypeId for this class
     */
    static TypeId GetTypeId();
    /**
     * Constructor for AggregateServiceFlow
     */
    AggregateServiceFlow();
    /**
     * Destructor for AggregateServiceFlow
     */
    ~AggregateServiceFlow() override;
    /**
     * \brief return the number of service flows stored
     * \return number of service flows
     */
    uint32_t GetNumServiceFlows() const;
    /**
     * \brief Set the classic service flow (overwriting any existing one)
     * \param sf the service flow
     */
    void SetClassicServiceFlow(Ptr<ServiceFlow> sf);
    /**
     * \brief Set the low latency service flow (overwriting any existing one)
     * \param sf the service flow
     */
    void SetLowLatencyServiceFlow(Ptr<ServiceFlow> sf);
    /**
     * \brief get a const pointer to the classic service flow, if present
     * \return the service flow
     */
    Ptr<const ServiceFlow> GetClassicServiceFlow() const;
    /**
     * \brief get a const pointer to the low latency service flow, if present
     * \return the service flow
     */
    Ptr<const ServiceFlow> GetLowLatencyServiceFlow() const;

    uint16_t m_asfid{0}; //!< C.2.2.5.11: Aggregate Service Flow Identifier; for future use

    enum TriState
    {
        FALSE,      //!< False value
        TRUE,       //!< True value
        UNSPECIFIED //!< Value to use to indicate that no value is specified
    };

    TriState m_coupled{UNSPECIFIED};          //!< Set to change COUPLED behavior of Annex M.
    uint8_t m_priority{0};                    //!< C.2.2.7.1: Traffic Priority; for future use
    DataRate m_maxSustainedRate{DataRate(0)}; //!< C.2.2.7.2: Maximum Sustained Traffic Rate
    uint32_t m_maxTrafficBurst{3044};         //!< C.2.2.7.3: Maximum Traffic Burst
    DataRate m_minReservedRate{
        DataRate(0)}; //!< C.2.2.7.4:  Minimum Reserved Traffic Rate; for future use
    uint32_t m_minReservedRatePacketSize{
        0}; //!< C.2.2.7.5 Assumed Minimum Reserved Rate Packet Size; for future use
    DataRate m_peakRate{DataRate(0)}; //!< C.2.2.7.10: Peak Traffic Rate
    uint8_t m_aqmCouplingFactor{
        255}; //!< C.2.2.7.17.5: AQM Coupling Factor (tenths); ranges from 0-25.4.  The value of 255
              //!< is a reserved value that will not change DualQueue configuration.
    uint8_t m_schedulingWeight{
        0}; //!< C.2.2.7.17.6: Scheduling Weight (range from 1-255).  The value of 0 is a reserved
            //!< value that will not change DualQueue configuration.
    TriState m_queueProtectionEnable{
        UNSPECIFIED}; //!< C.2.2.7.17.7: Queue Protection Enable; the value UNSPECIFIED will not
                      //!< change the DualQueue configuration.
    uint16_t m_qpLatencyThreshold{
        65535}; //!< C.2.2.7.17.8: QPLatencyThreshold (us); if set to 0 on instantiation, default to
                //!< the effective value of MAXTH (after floor calculation).  The value 65535 is a
                //!< reserved value that will not change the QueueProtection configuration.
    uint16_t m_qpQueuingScoreThreshold{
        65535}; //!< C.2.2.7.17.9: QPQueuingScoreThreshold (us); the value 65535 is a reserved value
                //!< that will not change QueueProtection configuration.
    uint8_t m_qpDrainRateExponent{
        0}; //!< C.2.2.7.17.10: QPDrainRateExponent; the value of 0 is a reserved value that will
            //!< not change QueueProtection configuration.

  private:
    Ptr<ServiceFlow> m_classicServiceFlow{nullptr};
    Ptr<ServiceFlow> m_lowLatencyServiceFlow{nullptr};
};

/**
 * A structure to encapsulate token bucket state
 */
struct TokenBucketState
{
    int32_t m_msrTokens{0}; // allow this variable to go negative
    uint32_t m_peakTokens{0};
    Time m_lastIncrement{Seconds(0)};
    Time m_lastDrain{Seconds(0)};
};

/**
 * A structure to encapsulate grant requests.  In this model, the minislot
 * time counter (request time) is a full 32-bit counter that wraps back to zero
 */
struct GrantRequest
{
    uint16_t m_sfid{0};
    uint32_t m_bytes{0};
    uint32_t m_requestTime{0}; //!< time represented in units of minislots
};

/**
 * A structure to encapsulate grant definitions
 */
struct Grant
{
    uint16_t m_sfid{0};
    uint32_t m_offset{0}; //!< offset represented in units of minislots
    uint32_t m_length{0}; //!< length represented in units of minislots
};

} // namespace docsis
} // namespace ns3
#endif /* DOCSIS_CONFIGURATION */
