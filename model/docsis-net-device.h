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

#ifndef DOCSIS_NET_DEVICE_H
#define DOCSIS_NET_DEVICE_H

#include <cstring>
#include "ns3/address.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/queue-disc.h"
#include "ns3/callback.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
#include "ns3/ptr.h"
#include "ns3/mac48-address.h"

namespace ns3 {

class FdNetDevice;
class ErrorModel;

namespace docsis {

class DocsisChannel;
class CmtsUpstreamScheduler;
class DualQueueCoupledAqm;

/**
 * \defgroup docsis DOCSIS models for cable access networks
 */

/**
 *\ingroup docsis
 * 
 * A MAP Information Element.
 */
struct MapIe
{
  uint16_t m_sfid;
  uint32_t m_offset; //!< Minislot offset
};

/**
 * A MAP message.  The minislot numbering (Alloc Start Time and ACK Time)
 * is a full 32-bit counter that wraps back to zero.
 */
struct MapMessage
{
  bool m_dataGrantPending;
  uint32_t m_numIe;
  uint32_t m_allocStartTime;
  uint32_t m_ackTime;
  std::list<MapIe> m_mapIeList;
};

/**
 * \ingroup docsis
 * \class DocsisNetDevice
 * \brief A base class for devices hooked to a DocsisChannel
 *
 * This DocsisNetDevice class specializes the NetDevice abstract
 * base class.  Together with a DocsisChannel (and a peer 
 * DocsisNetDevice), the class models, with some level of 
 * abstraction, a generic DOCSIS cable link.  It is further
 * specialized by a CmNetDevice and a CmtsNetDevice.
 * 
 * This version provides support for low-latency DOCSIS (DOCSIS 3.1 LLD)
 * with one or two service flows.
 */
class DocsisNetDevice : public NetDevice
{
public:
  /**
   * \brief Get the TypeId
   *
   * \return The TypeId for this class
   */
  static TypeId GetTypeId (void);

  /**
   * Constructor for DocsisNetDevice
   */
  DocsisNetDevice ();

  /**
   * Destructor for DocsisNetDevice
   */
  virtual ~DocsisNetDevice ();

  /**
   * Attach the device to a channel.
   *
   * \param ch Ptr to the channel to which this object is being attached.
   * \return true if the operation was successfull (always true actually)
   */
  bool Attach (Ptr<DocsisChannel> ch);

  /**
   * Receive a packet from a connected DocsisChannel.
   *
   * The DocsisNetDevice receives packets from its connected channel
   * and forwards them up the protocol stack.  This is the public method
   * used by the channel to indicate that the last bit of a packet has 
   * arrived at the device.
   *
   * \param p Ptr to the received packet.
   */
  void Receive (Ptr<Packet> p);

  /**
   * Receive a packet from an underlying NetDevice
   *
   * An alternative way to receive packets, in an emulation context, is
   * to use the ReceiveFromDevice method as a callback.
   *
   * \param device Ptr to the incoming device
   * \param packet Ptr to the received packet
   * \param protocol protocol number
   * \param source source address
   * \param dest destination address
   * \param packetType type of packet received (broadcast/multicast/unicast/otherhost)
   * \param promiscuous Whether to receive packets in promiscuous mode
   *
   * \return whether the reception succeeds
   */
  bool ReceiveFromDevice (Ptr<NetDevice> device, Ptr<const Packet> packet, uint16_t protocol, Address const &source, Address const &dest, enum PacketType packetType, bool promiscuous);

  /**
   * Receive a packet from an underlying NetDevice
   *
   * Method to hook to a device's PromiscReceiveCallback
   *
   * \param device Ptr to the incoming device
   * \param packet Ptr to the received packet
   * \param protocol protocol number
   * \param source source address
   * \param dest destination address
   * \param packetType type of packet received (broadcast/multicast/unicast/otherhost)
   *
   * \return whether the reception succeeds
   */
  bool PromiscReceiveFromDevice (Ptr<NetDevice> device, Ptr<const Packet> packet, uint16_t protocol, Address const &source, Address const &dest, enum PacketType packetType);

  /**
   * Receive a packet from an underlying NetDevice
   *
   * Method to hook to a device's ReceiveCallback
   *
   * \param device Ptr to the incoming device
   * \param packet Ptr to the received packet
   * \param protocol protocol number
   * \param source source address
   *
   * \return whether the reception succeeds
   */
  bool NonPromiscReceiveFromDevice (Ptr<NetDevice> device, Ptr<const Packet> packet, uint16_t protocol, Address const &source);
  /**
   * Forward packet up from the Phy.  Called after Receive() with any
   * delay necessary to model Phy processing delay.
   *
   * \param p Ptr to the received packet.
   */
  void ForwardUp (Ptr<Packet> p);

  void SetFdNetDevice (Ptr<FdNetDevice> device);

  Ptr<FdNetDevice> GetFdNetDevice (void) const;

  // The remaining methods are documented in ns3::NetDevice*

  virtual void SetIfIndex (const uint32_t index);
  virtual uint32_t GetIfIndex (void) const;

  virtual Ptr<Channel> GetChannel (void) const;

  virtual void SetAddress (Address address);
  virtual Address GetAddress (void) const;

  virtual bool SetMtu (const uint16_t mtu);
  virtual uint16_t GetMtu (void) const;

  virtual bool IsLinkUp (void) const;

  virtual void AddLinkChangeCallback (Callback<void> callback);

  virtual bool IsBroadcast (void) const;
  virtual Address GetBroadcast (void) const;

  virtual bool IsMulticast (void) const;
  virtual Address GetMulticast (Ipv4Address multicastGroup) const;

  virtual bool IsPointToPoint (void) const;
  virtual bool IsBridge (void) const;

  virtual bool SendFrom (Ptr<Packet> packet, const Address& source, const Address& dest, uint16_t protocolNumber);

  virtual Ptr<Node> GetNode (void) const;
  virtual void SetNode (Ptr<Node> node);

  virtual bool NeedsArp (void) const;

  virtual void SetReceiveCallback (NetDevice::ReceiveCallback cb);

  virtual Address GetMulticast (Ipv6Address addr) const;

  virtual void SetPromiscReceiveCallback (PromiscReceiveCallback cb);
  virtual bool SupportsSendFrom (void) const;

  // Public method specific to DocsisNetDevices

  /**
   * Provide the DualQueueCoupledAqm to the device
   * \param dualQueue pointer to the queue
   */
  void SetQueue (Ptr<DualQueueCoupledAqm> dualQueue);
  /**
   * Fetch a pointer to the DualQueueCoupledAqm in the device
   * \return pointer to the queue
   */
  Ptr<DualQueueCoupledAqm> GetQueue (void) const;

  /**
   * \brief set pointer to CmtsUpstreamScheduler object
   * \param scheduler pointer to scheduler
   */
  void SetCmtsUpstreamScheduler (Ptr<CmtsUpstreamScheduler> scheduler);
  /**
   * \brief return pointer to CmtsUpstreamScheduler object
   * \return pointer to scheduler
   */
  Ptr<CmtsUpstreamScheduler> GetCmtsUpstreamScheduler (void) const;
  /**
   * \brief Return predicted queueing delay
   *
   * Method to be used by the AQM to query for predicted queueing delay
   * (RFC 8034 section 4.2, qdelaySingle from Annex M of specification).
   */
  virtual Time ExpectedDelay (void) const = 0;

  /**
   * \brief Get the actual map interval time value
   *
   * \return map interval
   */
  Time GetActualMapInterval (void) const;
  Time GetCmMapProcTime (void) const;
  Time GetCmtsMapProcTime (void) const;
  Time GetDsSymbolTime (void) const;
  Time GetDsIntlvDelay (void) const;
  Time GetRtt (void) const;
  uint32_t GetMinislotCapacity (void) const;
  uint32_t GetFramesPerMap (void) const;
  uint32_t GetMinislotsPerMap (void) const;
  uint32_t GetMinislotsPerFrame (void) const;
  Time GetFrameDuration (void) const;
  double GetUsCapacity (void) const;
  uint32_t GetUsMacHdrSize (void) const;
  uint32_t GetDsMacHdrSize (void) const;

  /*
   * In ns-3, simulation time is measured as a unsigned integer, starting
   * at time 0, for a given time base (by default, nanoseconds). In DOCSIS,
   * the alloc start time counter is defined in minislots and is maintained
   * as a 32-bit unsigned number.
   *
   * This method converts from an ns-3 simulation time to the number of
   * minislots represented by that time, based on the configuration defined
   * for the link. Time is rounded down to the nearest frame start time.
   *
   * The DOCSIS alloc start time counter is 32 bits long, so input time
   * values that result in a minislot value that exceeds the size of an
   * unsigned 32 bit number will roll over.  With typical configuration
   * values, this will roll over for simulation times on the order of
   * two to three thousand seconds.
   * 
   * \param simulationTime the simulation time to convert
   * \return The number of minislots, modulo 2^32
   */
  uint32_t TimeToMinislots (Time simulationTime) const;

  /*
   * \param minislots the minislots to convert
   * \return the simulation time at the start of the minislot
   */
  Time MinislotsToTime (uint32_t minislots) const;
  /**
   * Attach a receive ErrorModel to the DocsisNetDevice
   *
   * The device may optionally include an ErrorModel in
   * the data path.  This does not affect the notional control
   * channel.
   *
   * \see ErrorModel
   * \param em Ptr to the ErrorModel.
   */
  void SetReceiveErrorModel (Ptr<ErrorModel> em);

protected:
  virtual void NotifyNewAggregate (void);

  /*
   * Start the CmtsUpstreamScheduler
   * \return AllocStartTime of first MAP message that will be generated
   */
  uint32_t StartScheduler (void);
  /**
   * Calculate the size of an eventual Ethernet frame based on the input
   * SDU size (by adding 14 header bytes, 4 trailer bytes, and accounting
   * for 64-byte minimum frame size).  The IP packet is the SDU.
   * See Figure 18 (Generic MAC Frame Format) of specification
   *
   * \param sduSize size in bytes of SDU (IP packet)
   * \return size of frame in bytes
   */
  uint32_t GetDataPduSize (uint32_t sduSize) const;

  /**
   * Calculate the size of an eventual upstream MAC frame based on the input
   * SDU size (by any Ethernet framing and padding, and the upstream MAC
   * header).
   *
   * \param sduSize size in bytes of SDU (IP packet)
   * \return size of frame in bytes
   */
  uint32_t GetUsMacFrameSize (uint32_t sduSize) const;

  /**
   * Calculate the size of an eventual downstream MAC frame based on the input
   * SDU size (by any Ethernet framing and padding, and the downstream MAC
   * header).
   *
   * \param sduSize size in bytes of SDU (IP packet)
   * \return size of frame in bytes
   */
  uint32_t GetDsMacFrameSize (uint32_t sduSize) const;

  /**
   * \param p Packet to which Ethernet header and trailer should be added
   * \param source MAC source address from which packet should be sent
   * \param dest MAC destination address to which packet should be sent
   * \param protocolNumber protocol number
   */
  void AddHeaderTrailer (Ptr<Packet> p, Mac48Address source, Mac48Address dest, uint16_t protocolNumber);

  /**
   * \brief Dispose of the object
   */
  virtual void DoDispose (void);

  /**
   * \brief Initialize the object
   */
  virtual void DoInitialize (void);

  DataRate GetDataRate (void) const;

  Ptr<DocsisChannel> GetDocsisChannel (void) const;

  /**
   * If attribute is set to remark TCP ECT0 to ECT1, perform operation on 
   * packet; otherwise, perform no operation.  This method is provided
   * to allow TCP senders (Linux DCTCP) that send scalable cong. control
   * packets as ECT0 to be remarked as ECT1 so that the DualQ can handle
   * appropriately.
   *
   * \param packet the packet to check
   * \return whether packet was remarked
   */
  bool RemarkTcpEcnValue (Ptr<Packet> packet);

private:

  /**
   * \brief Assign operator
   *
   * The method is private, so it is DISABLED.
   *
   * \param o Other NetDevice
   * \return New instance of the NetDevice
   */
  DocsisNetDevice& operator = (const DocsisNetDevice &o);

  /**
   * \brief Copy constructor
   *
   * The method is private, so it is DISABLED.

   * \param o Other NetDevice
   */
  DocsisNetDevice (const DocsisNetDevice &o);

private:

  /**
   * \returns the address of the remote device connected to this device
   * through the point to point channel.
   */
  Address GetRemote (void) const;

  /**
   * \brief Make the link up and running
   *
   * It calls also the linkChange callback.
   */
  void NotifyLinkUp (void);

  /**
   * Enumeration of the states of the transmit machine of the net device.
   */
  enum TxMachineState
  {
    READY,   /**< The transmitter is ready to begin transmission of a packet */
    BUSY     /**< The transmitter is busy transmitting a packet */
  };
  /**
   * The state of the Net Device transmit state machine.
   */
  TxMachineState m_txMachineState;

  /**
   * The data rate that the Net Device uses to simulate packet transmission
   * timing.
   */
  DataRate       m_bps;

  /**
   * The DocsisChannel to which this DocsisNetDevice has been
   * attached.
   */
  Ptr<DocsisChannel> m_channel;

protected:
  /**
   * The trace source fired when packets come into the "top" of the device
   * at the L3/L2 transition, before being queued for transmission.
   */
  TracedCallback<Ptr<const Packet> > m_macTxTrace;

  /**
   * The trace source fired when packets coming into the "top" of the device
   * at the L3/L2 transition are dropped before being queued for transmission.
   */
  TracedCallback<Ptr<const Packet> > m_macTxDropTrace;

  /**
   * The trace source fired for packets successfully received by the device
   * immediately before being forwarded up to higher layers (at the L2/L3 
   * transition).  This is a promiscuous trace (which doesn't mean a lot here
   * in the DOCSIS device).
   */
  TracedCallback<Ptr<const Packet> > m_macPromiscRxTrace;

  /**
   * The trace source fired for packets successfully received by the device
   * immediately before being forwarded up to higher layers (at the L2/L3 
   * transition).  This is a non-promiscuous trace (which doesn't mean a lot 
   * here in the DOCSIS device).
   */
  TracedCallback<Ptr<const Packet> > m_macRxTrace;

  /**
   * TracedCallback signature for Ptr<Packet>
   *
   * \param [in] packet The packet.
   */
  typedef void (* DeviceEgressCallback) (Ptr<Packet> packet);

  /**
   * A non-const trace source fired for packets successfully received by 
   * the device, immediately before being forwarded up to higher layers 
   * (at the L2/L3 transition).  This is a non-promiscuous trace (which 
   * doesn't mean a lot here in the DOCSIS device).
   */
  TracedCallback<Ptr< Packet> > m_deviceEgressTrace;

  /**
   * The trace source fired for packets successfully received by the device
   * but dropped before being forwarded up to higher layers (at the L2/L3 
   * transition).
   */
  TracedCallback<Ptr<const Packet> > m_macRxDropTrace;

  /**
   * The trace source fired when a packet begins the transmission process on
   * the medium.
   */
  TracedCallback<Ptr<const Packet> > m_phyTxBeginTrace;

  /**
   * The trace source fired when a packet ends the transmission process on
   * the medium.
   */
  TracedCallback<Ptr<const Packet> > m_phyTxEndTrace;

  /**
   * The trace source fired when the phy layer drops a packet before it tries
   * to transmit it.
   */
  TracedCallback<Ptr<const Packet> > m_phyTxDropTrace;

  /**
   * The trace source fired when a packet begins the reception process from
   * the medium -- when the simulated first bit(s) arrive.
   */
  TracedCallback<Ptr<const Packet> > m_phyRxBeginTrace;

  /**
   * The trace source fired when a packet ends the reception process from
   * the medium.
   */
  TracedCallback<Ptr<const Packet> > m_phyRxEndTrace;

  /**
   * The trace source fired when the phy layer drops a packet it has received.
   * This happens if the receiver is not enabled or the error model is active
   * and indicates that the packet is corrupt.
   */
  TracedCallback<Ptr<const Packet> > m_phyRxDropTrace;

  /**
   * A trace source that emulates a non-promiscuous protocol sniffer connected 
   * to the device.  Unlike your average everyday sniffer, this trace source 
   * will not fire on PACKET_OTHERHOST events.
   *
   * On the transmit size, this trace hook will fire after a packet is dequeued
   * from the device queue for transmission.  In Linux, for example, this would
   * correspond to the point just before a device \c hard_start_xmit where 
   * \c dev_queue_xmit_nit is called to dispatch the packet to the PF_PACKET 
   * ETH_P_ALL handlers.
   *
   * On the receive side, this trace hook will fire when a packet is received,
   * just before the receive callback is executed.  In Linux, for example, 
   * this would correspond to the point at which the packet is dispatched to 
   * packet sniffers in \c netif_receive_skb.
   */
  TracedCallback<Ptr<const Packet> > m_snifferTrace;

  /**
   * A trace source that emulates a promiscuous mode protocol sniffer connected
   * to the device.  This trace source fire on packets destined for any host
   * just like your average everyday packet sniffer.
   *
   * On the transmit size, this trace hook will fire after a packet is dequeued
   * from the device queue for transmission.  In Linux, for example, this would
   * correspond to the point just before a device \c hard_start_xmit where 
   * \c dev_queue_xmit_nit is called to dispatch the packet to the PF_PACKET 
   * ETH_P_ALL handlers.
   *
   * On the receive side, this trace hook will fire when a packet is received,
   * just before the receive callback is executed.  In Linux, for example, 
   * this would correspond to the point at which the packet is dispatched to 
   * packet sniffers in \c netif_receive_skb.
   */
  TracedCallback<Ptr<const Packet> > m_promiscSnifferTrace;

protected:
  // Variables directly handled by subclasses
  bool UseDocsisChannel (void) const;
  Time GetMapInterval (void) const;
  uint32_t GetScPerMinislot (void) const;
  uint32_t GetMinReqGntDelay (void) const;
  double GetUsGrantsPerSecond (void) const;
  double GetAvgIesPerMap (void) const;
  double GetAvgMapSize (void) const;
  double GetAvgMapDatarate (void) const;
  double GetAvgMapOhPerSymbol (void) const;
  double GetScPerNcp (void) const;
  double GetScPerCw (void) const;
  double GetDsCodewordsPerSymbol (void) const;
  double GetDsSymbolCapacity (void) const;
  double GetDsCapacity (void) const;
  uint32_t GetCmtsDsPipelineFactor (void) const;
  uint32_t GetCmDsPipelineFactor (void) const;
  uint32_t GetCmtsUsPipelineFactor (void) const;
  uint32_t GetCmUsPipelineFactor (void) const;

private:
  Ptr<Node> m_node;         //!< Node owning this NetDevice
  Ptr<DualQueueCoupledAqm> m_queue;    //!< DualQueue for internal queue
  Mac48Address m_address;   //!< Mac48Address of this NetDevice
  NetDevice::ReceiveCallback m_rxCallback;   //!< Receive callback
  NetDevice::PromiscReceiveCallback m_promiscCallback;  //!< Receive callback
                                                        //   (promisc data)
  uint32_t m_ifIndex; //!< Index of the interface
  bool m_linkUp;      //!< Identify if the link is up or not
  TracedCallback<> m_linkChangeCallbacks;  //!< Callback for the link change event

  bool m_useDocsisChannel; //!< Use DOCSIS or emulation channel
  bool m_remarkTcpEct0ToEct1; //!< Remark TCP ECT(0) to ECT(1)
  bool m_useConfigurationCache; //!< whether to cache configuration values
  Ptr<FdNetDevice> m_fdNetDevice; //!< Emulation device
  Ptr<CmtsUpstreamScheduler> m_cmtsUpstreamScheduler; //!< CMTS upstream scheduler
  Ptr<ErrorModel> m_receiveErrorModel; //!< Optional receive error model.

  // DOCSIS 3.1 parameters
  // Upstream
  Time m_mapInterval;    //! MAP interval
  double m_usScSpacing; //! Upstream subcarrier spacing 
  uint32_t m_numUsSc; //! Number of upstream subcarriers
  uint32_t m_symbolsPerFrame;  //! valid values 6-36
  double m_usSpectralEfficiency; //! bps/Hz; valid values 1.0-12.0 (float)
  uint32_t m_usCpLen; //! US cyclic prefix length (Ncp).  Valid values:  96,128,160,192,224,256,288,320,384,512,640
  uint32_t m_usMacHdrSize;   //! upstream MAC header size (bytes)
  uint32_t m_usSegHdrSize;   //! upstream segment header size (bytes)
  Time m_cmtsMapProcTime;    //! includes any "guard time" the CMTS wants to factor in
  uint32_t m_cmtsUsPipelineFactor; //! symbol times in advance of tx that encoding begins
  uint32_t m_cmUsPipelineFactor; //! symbol times after rx completes that decoding complets
  // Downstream
  double m_dsScSpacing; //! Downstream subcarrier spacing 
  uint32_t m_numDsSc; //! Number of downstream subcarriers
  double m_dsSpectralEfficiency; //! bps/Hz (modulation choice); valid values 4.0-14.0 (float)
  uint32_t m_dsIntlvM; //! M=1 means off, otherwise, values 1-32 (50e3 spacing) or 1-16 (25e3 spacing)
  uint32_t m_dsCpLen; //! DS cyclic prefix length (Ncp).  Valid values:  192,256,512,768,1024
  uint32_t m_cmtsDsPipelineFactor; //! symbol times in advance of tx that encoding begins
  uint32_t m_cmDsPipelineFactor; //! symbol times after rx completes that decoding complets
  uint32_t m_dsMacHdrSize; //! bytes (typically 10 for no channel bonding, 16 for channel bonding)
  double m_averageCodewordFill; //! factor to account for 0xFF padding bytes between frames, shortened codewords due to profile changes, etc.
  uint32_t m_ncpModulation; //! allowed values:  2,4,6
  uint32_t m_numUsChannels; //! number of US channels managed by this DS channel
  uint32_t m_averageUsBurst; //! bytes.  average size of an upstream burst
  double m_averageUsUtilization; //! average US utilization (ratio)
  double m_maximumDistance;   //! plant km from furthest CM to CMTS (max 80km)

  // Cache values
  uint32_t m_cachedFramesPerMap;  //<! cached frames per map
  double m_cachedDsSymbolCapacity;  //<! cached DsSymbolCapacity value
  double m_cachedUsCapacity;  //<! cached UsCapacity value
  Time m_cachedDsSymbolTime;  //<! cached DsSymbolTime value
  Time m_cachedCmMapProcTime; //<! cached CmMapProcTime value
  Time m_cachedFrameDuration; //<! cached FrameDuration value
  uint32_t m_cachedMinReqGntDelay; //<! cached MinReqGntDelay value
  uint32_t m_cachedMinislotCapacity; //<! cached MinislotCapacity value
  bool m_cacheInitialized;  //<! flag for noting whether cached values are valid
  
  // Private setters/getters for enforcing pre- and post-conditions
  void SetUsScSpacing (double spacing);
  double GetUsScSpacing (void) const;  
  void SetDsScSpacing (double spacing);
  double GetDsScSpacing (void) const;  
  void SetMaximumDistance (double distance);
  double GetMaximumDistance (void) const;  

  // Specialized methods
  virtual void StartDecoding (Ptr<Packet> p) = 0;
  virtual void RemoveReceivedDocsisHeader (Ptr<Packet> p) = 0;
  virtual void AddDocsisHeader (Ptr<Packet> p) = 0;
  /**
   * Calculate the size of an eventual MAC frame based on the input
   * SDU size (due to Ethernet framing and padding, and the MAC
   * header).
   * 
   * Subclasses are expected to define this to pick the applicable upstream
   * or downstream MAC header size.
   *
   * \param sduSize size in bytes of SDU (IP packet)
   * \return size of frame in bytes
   */
  virtual uint32_t GetMacFrameSize (uint32_t sduSize) const = 0;

  static const uint16_t DEFAULT_MTU = 1500; //!< Default MTU

  /**
   * \brief The Maximum Transmission Unit
   *
   * This corresponds to the maximum 
   * number of bytes that can be transmitted as seen from higher layers.
   * This corresponds to the 1500 byte MTU size often seen on IP over 
   * Ethernet.
   */
  uint32_t m_mtu;
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_NET_DEVICE_H */
