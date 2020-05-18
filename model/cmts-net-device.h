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

#ifndef DOCSIS_CMTS_NET_DEVICE_H
#define DOCSIS_CMTS_NET_DEVICE_H

#include "ns3/docsis-net-device.h"
#include "ns3/docsis-configuration.h"
#include "ns3/traced-value.h"
#include "ns3/traced-callback.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/queue-item.h"

namespace ns3 {

class RandomVariableStream;

namespace docsis {

/**
 * \ingroup docsis
 * \class CmtsNetDevice
 *
 * This DocsisNetDevice class models a downstream (CMTS) device.  It is 
 * a layer-2 device and must be bridged to another device (CsmaNetDevice);
 * i.e. this model assumes the CMTS is a layer-2 device.
 */
class CmtsNetDevice : public DocsisNetDevice
{
public:
  /**
   * \brief Get the TypeId
   *
   * \return The TypeId for this class
   */
  static TypeId GetTypeId (void);

  /**
   * Constructor for CmtsNetDevice
   */
  CmtsNetDevice ();

  /**
   * Destructor for CmtsNetDevice
   */
  virtual ~CmtsNetDevice ();

  virtual bool SendFrom (Ptr<Packet> packet, const Address& source, const Address& dest, uint16_t protocolNumber);

  virtual bool Send (Ptr<Packet> packet, const Address &dest, uint16_t protocolNumber);

  /**
   * TracedCallback signature for state reporting
   *
   * \param [in] tokensUsed tokens
   * \param [in] tokensRemaining tokens remaining
   * \param [in] peakTokensRemaining peak tokens remaining
   * \param [in] internalBytes internally queued bytes waiting for transmission
   * \param [in] feederBytes feeder queue bytes waiting for transmission
   * \param [in] pipeline pipeline of data scheduled for transmission
    */
  typedef void (* StateTracedCallback) (double tokensUsed, double tokensRemaining, double peakTokensRemaining, uint32_t internalBytes, uint32_t feederBytes, uint32_t pipeline);

  void Reset (void);

  // Documented in DocsisNetDevice class
  Time ExpectedDelay (void) const;

  /**
   * Add downstream aggregate service flow.  This operation must be done before
   * the simulation is started.
   * 
   * Either an AggregateServiceFlow or a single ServiceFlow object should
   * be present, but not both.  It is a simulation error (misconfiguration)
   * to try to set both.
   *
   * \param asf pointer to the AggregateServiceFlow object
   */
  void SetDownstreamAsf (Ptr<AggregateServiceFlow> asf);

  /**
   * Add downstream single service flow.  This operation must be done before
   * the simulation is started.
   *
   * Either an AggregateServiceFlow or a single ServiceFlow object should
   * be present, but not both.  It is a simulation error (misconfiguration)
   * to try to set both.
   * 
   * \param sf pointer to the ServiceFlow object
   */
  void SetDownstreamSf (Ptr<ServiceFlow> sf);

  /**
   * Get downstream aggregate service flow (if present).
   * \return pointer to the AggregateServiceFlow object, if present
   */
  Ptr<const AggregateServiceFlow> GetDownstreamAsf (void) const;

  /**
   * Get downstream service flow (if present).
   * \return pointer to the ServiceFlow object, if present
   */
  Ptr<const ServiceFlow> GetDownstreamSf (void) const;

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
  virtual void DoInitialize (void);

private:

  void AskForNextPacket (Time howSoon);

  void SendSymbol (uint32_t eligibleBytes, uint32_t symbolState);
  uint32_t SendOut (Ptr<Packet> packet, const Address &src, const Address &dest, uint16_t protocolNumber);
  void HandleSymbolBoundary (void);

  uint32_t GetPipelineData (void) const;
  uint32_t GetEligiblePipelineData (void) const;
  void PushToPipeline (uint32_t newData);
  void PopFromPipeline (uint32_t oldData);
  uint32_t GetFramingSize (void) const;
#if 0
  /**
   * Peek the feeder queue and return the expected Ethernet frame size
   * for the next packet in queue.  Return 0 if queue empty.
   *
   * \return frame size (including header, trailer, padding) of next frame
   */
  uint32_t GetPendingFrameSize (void) const;
#endif

  uint32_t SendFragment (void);
  void AdvanceSymbolState (uint32_t bytes);
  void AdvanceTokenState (uint32_t bytes);
  uint32_t BytesRemainingInSymbol (void) const;

  // Used in Docsis 3.1 models
  virtual void StartDecoding (Ptr<Packet> packet);
  void EndDecoding (Ptr<Packet> packet);

  virtual void RemoveReceivedDocsisHeader (Ptr<Packet> packet);
  virtual void AddDocsisHeader (Ptr<Packet> packet);
  uint32_t GetMacFrameSize (uint32_t sduSize) const;

  void IncrementTokens (Time elapsed);
  Time GetTransmissionTime (uint32_t bytes) const;
  uint32_t GetEligibleTokens (void) const;

  void SendImmediatelyIfAvailable (void);
  void TransmitComplete (void);

  uint32_t GetInternallyQueuedBytes (void) const;

  /**
   * Use free capacity and free capacity variation variables to calculate
   * the amount of notional free capacity in the symbol
   *
   * \return amount of free capacity in this symbol (bytes)
   */
  uint32_t CalculateFreeCapacity (void);

  // DOCSIS configuration variables tied to attributes
  uint32_t m_freeCapacityMean;  //!< Model congestion on the DOCSIS link by limiting available downstream capacity (bps)
  uint32_t m_freeCapacityVariation;     //!< Model congestion variation on the DOCSIS link by specifying a percentage bound (RANGE: 0 - 100)

  DataRate m_maxSustainedRate;  //!<  Maximum Sustained Traffic Rate
  DataRate m_peakRate;          //!<  Peak Traffic Rate
  uint32_t m_maxTrafficBurst;   //!<  Maximum Traffic Burst
  uint32_t m_maxPdu;            //!<  MaxPDU parameter in Peak Traffic Rate
  TracedValue<double> m_tokens; //!<  Accumulated tokens
  double m_peakTokens;          //!<  Accumulated peak rate tokens
  Time m_lastUpdateTime;
  bool m_pointToPointMode {false};
  bool m_pointToPointBusy {false};

  Ptr<AggregateServiceFlow> m_asf {nullptr};
  Ptr<ServiceFlow> m_sf {nullptr};
  Ptr<const ServiceFlow> m_classicSf {nullptr};
  Ptr<const ServiceFlow> m_llSf {nullptr};

  uint32_t* m_req;           //!< Pipeline
  uint32_t m_reqCount;       //!< Pipeline length
  Ptr<Packet> m_fragPkt;    //!< Fragmented packet across multiple symbols
  Ptr<Packet> m_fragSdu;    //!< Copy of original SDU for emulation use
  uint32_t m_fragSentBytes; //!< # of bytes Fragment
  Address m_fragSrc;       //!< Store value of argument to SendFrom ()
  Address m_fragDest;       //!< Store value of argument to SendFrom ()
  uint16_t m_fragProtocolNumber;  //!< Store value of argument to Send ()
  // This variable is needed to report a transmission to the higher layer
  uint32_t m_lastSduSize;   //!< Store size of last SDU passed to Send ()

  uint32_t m_scheduledBytes; //!< scheduled amount of data for current symbol
  uint32_t m_symbolState;  //!< byte position for next packet in symbol

  Ptr<RandomVariableStream> m_congestionVariable;

  /**
   * The device makes use of a FIFO DropTailQueue to store data that is 
   * notionally being prepared for transmission one or more symbols later.
   */
  Ptr<DropTailQueue<QueueDiscItem> > m_deviceQueue {nullptr};
  uint32_t m_queueFramedBytes; //!< Account for Ethernet framing/padding

  /**
   * The trace source fired when a packet is sent out, to report on the
   * internal state of the token bucket.
   */
  TracedCallback<double, double, double, uint32_t, uint32_t, uint32_t> m_state;

};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_CMTS_NET_DEVICE_H */
