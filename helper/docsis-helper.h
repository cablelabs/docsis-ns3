/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Tom Henderson <tomh@tomh.org> (Extensions by CableLabs LLD project)
 */
#ifndef DOCSIS_HELPER_H
#define DOCSIS_HELPER_H

#include <string>
#include <utility>
#include <ostream>

#include "ns3/object-factory.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/ipv4-header.h"

#include "ns3/trace-helper.h"

namespace ns3 {

class NetDevice;
class Node;
class Ipv4Address;
class FileTransferApplication;
class OnOffApplication;
class UdpClient;
class UdpServer;
class GameClient;
class DataRate;
class QueueSize;
class QueueDiscItem;

namespace docsis {

class CmNetDevice;
class CmtsNetDevice;
class CmtsUpstreamScheduler;
class DocsisNetDevice;
class DocsisChannel;
class DualQueueCoupledAqm;
class QueueProtection;
/**
 * \ingroup docsis
 * \brief Helper code for constructing DOCSIS simulations 
 *
 * This helper is used to add a DOCSIS link between a pair of
 * nodes, one notional CMTS node and one notional CM node.  It
 * installs a CmtsNetDevice and a CmNetDevice on the nodes, but does
 * not install any other nodes or links.  The CmNetDevice and CmtsNetDevice
 * are L2 bridged devices; the user of this helper must bridge
 * them to another device on the same node.
 *
 * This helper also provides methods to install and configure the dual queue
 * and queue protection objects, and also to install selected applications
 * on client and server nodes.
 *
 * This helper does not maintain internal state and is primarily designed
 * around the Install() helper pattern in use in other parts of the
 * ns-3 code.  Another helper (DocsisScenarioHelper) is stateful and
 * constructs a larger network with client and server application nodes.
 *
 * \sa ns3::DocsisScenarioHelper
 */
class DocsisHelper : public PcapHelperForDevice, public AsciiTraceHelperForDevice
{
public:
  /**
   * Constructor
   */
  DocsisHelper ();
  /**
   * Destructor
   */
  virtual ~DocsisHelper () {}

  /**
   * Set an attribute value for the CmNetDevice device to be installed.
   * This only has effect on devices installed by future calls to the
   * Install() method.
   *
   * \param name the name of the attribute to set
   * \param value the value of the attribute to set
   *
   */
  void SetCmAttribute (std::string name, const AttributeValue &value);

  /**
   * Set an attribute value for the CmtsNetDevice to be installed.
   * This only has effect on devices installed by future calls to the
   * Install() method.
   *
   * \param name the name of the attribute to set
   * \param value the value of the attribute to set
   *
   */
  void SetCmtsAttribute (std::string name, const AttributeValue &value);

  /**
   * Set an attribute value for the DocsisChannel to be installed.
   * This only has effect on channels installed by future calls to the
   * Install() method.
   *
   * \param name the name of the attribute to set
   * \param value the value of the attribute to set
   *
   */
  void SetChannelAttribute (std::string name, const AttributeValue &value);

  /**
   * Helper method to install a DocsisChannel between the two nodes,
   * and to install a CmtsNetDevice on the CMTS node, and a CmNetDevice
   * on the CM node.  
   *
   * To complete the configuration, a DualQueue configuration must be
   * installed on both devices (see InstallLldCoupledQueue)
   *
   * \param cmts The CMTS node
   * \param cm A single CM node
   * \return a NetDeviceContainer (first element is the CmtsNetDevice, second is the CmNetDevice)
   * \sa InstallLldCoupledQueue()
   */
  NetDeviceContainer Install (Ptr<Node> cmts, Ptr<Node> cm);

  /**
   * \brief Utility function to return properly casted device pointer
   * \return pointer to a CmNetDevice
   * \param device NetDevice container, with CMTS in slot 0 and CM in slot 1
   */
  Ptr<CmNetDevice> GetUpstream (const NetDeviceContainer& device) const;

  /**
   * \brief Utility function to return properly casted device pointer
   * \return pointer to a CmtsNetDevice
   * \param device NetDevice container, with CMTS in slot 0 and CM in slot 1
   */
  Ptr<CmtsNetDevice> GetDownstream (const NetDeviceContainer& device) const;

  /**
   * \brief Utility function to return properly casted device pointer
   * \return pointer to a DocsisChannel
   * \param device NetDevice container, with CMTS in slot 0 and CM in slot 1
   */
  Ptr<DocsisChannel> GetChannel (const NetDeviceContainer& device) const;

  /**
   * Install and configure the depth of a DualQ Coupled PI2 queue on a
   * DOCSIS device (either CmNetDevice or CmtsNetDevice).
   * \param device the device to install
   * \param maxSize queue size to configure
   * \param amsr AMSR configured for this queue
   * \return a pointer to the DualQueueCoupledAqm installed
   */
  Ptr<DualQueueCoupledAqm> InstallLldCoupledQueue (Ptr<DocsisNetDevice> device, QueueSize maxSize, DataRate amsr);

  /**
   * Add a notional game flow (UDP packet stream from client to server) with
   * the following properties:  1) mean interarrival time of 33 ms, std. dev.
   * of 3 ms, according to a Normal distribution, 2) mean packet size of 
   * 128 bytes, std. dev. of 20 bytes, according to a Normal distribution.
   * The packet size is computed at the Ethernet level (i.e. with Ethernet
   * framing included), so the actual application data unit size is backed
   * off for headers.  Packets are IPv4 and marked as DSCP EF unless the
   * DSCP value is optionally overridden.
   *
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param startTime Start time
   * \param stopTime Stop time
   * \param dscpValue (optional) DSCP value for the socket, if not DSCP_EF
   * \return pointer to the traffic generating GameClient
   */
  Ptr<GameClient> AddUpstreamGameSession (Ptr<Node> client, Ptr<Node> server,
    uint16_t serverPort, Time startTime, Time stopTime,
    Ipv4Header::DscpType dscpVal = Ipv4Header::DSCP_EF) const;

  /**
   * Add a notional game flow (UDP packet stream from client to server) with
   * the following properties:  1) mean interarrival time of 33 ms, std. dev.
   * of 5 ms, according to a Normal distribution, 2) mean packet size of 
   * 450 bytes, std. dev. of 120 bytes, according to a Normal distribution.
   * The packet size is computed at the Ethernet level (i.e. with Ethernet
   * framing included), so the actual application data unit size is backed
   * off for headers.  Packets are IPv4 and marked as DSCP EF unless the
   * DSCP value is optionally overridden.
   *
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param startTime Start time
   * \param stopTime Stop time
   * \param dscpValue (optional) DSCP value for the socket, if not DSCP_EF
   * \return pointer to the traffic generating GameClient
   */
  Ptr<GameClient> AddDownstreamGameSession (Ptr<Node> client, Ptr<Node> server,
    uint16_t serverPort, Time startTime, Time stopTime,
    Ipv4Header::DscpType dscpVal = Ipv4Header::DSCP_EF) const;

  /**
   * Add a notional MPEG DASH session.
   * Use reading time (FTP model 2).  The DASH model here allows for
   * a connection 'on time' of 2.5 seconds followed by a 'reading time' of
   * 2.485 seconds, with the goal of having DASH obtain about 6 Mb/s
   * throughput but with variability (rate adaptation).  The configured
   * RTT and competing connections will cause this throughput to vary.
   * 2.485 seconds read time allows 15 ms for the connection to close
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param startTime Start time
   * \param stopTime Stop time
   * \param tcpSocketFactoryType TypeId corresponding to the TCP type
   * \param maxDashBytes number of bytes per notional segment
   */
  void AddDashSession (Ptr<Node> client, Ptr<Node> server,
    uint16_t serverPort, Time startTime, Time stopTime,
    std::string tcpSocketFactoryType, uint32_t maxDashBytes) const;

  /**
   * Add a notional FTP session.
   *
   * If 'fileModel' is unlimited, this model will make an arbitrarily large
   * single file trasfer
   *
   * If 'fileModel' is empirical, this model uses a random file size 
   * according to a LogNormal random variable with mu=14.8 and sigma=2
   * (matched to an empirical dataset).
   *
   * If 'fileModel' is speedtest, this model will make successive transfers
   * of 'fileSize' bytes (the special value of zero is for unlimited size).
   *
   * The reading time between successive transfers is 100ms.
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param startTime Start time
   * \param stopTime Stop time
   * \param tcpSocketFactoryType TypeId corresponding to the TCP type
   * \param fileModel Model for FTP files (empirical, speedtest, unlimited)
   * \return pointer to file transfer application created by this method
   */
  Ptr<FileTransferApplication> AddFtpSession (Ptr<Node> client, Ptr<Node> server,
    uint16_t serverPort, Time startTime,
    Time stopTime, std::string tcpSocketFactoryType, std::string fileModel) const;

  /**
   * Add a UDP stream that sends a packet train of fixed-sized UDP/IPv4 packets
   * (with configurable frame size and data rate), starting at a prescribed
   * time and for a given duration.  The frame size specified corresponds
   * to an IP packet and including also 18 bytes of Ethernet header/trailer.
   * The start time is the time of the first packet generation, and the
   * data rate (as measured at the Ethernet level) determines the fixed 
   * frame interarrival interval.  For example, a specified frame size of
   * 125 bytes and specified data rate of 1 Mbps will cause UDP/IPv4 packets 
   * with payload size of (125-18-20-8) = 79 bytes to be generated at 
   * intervals of 1 ms.
   *
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param clientPort Client port
   * \param dataRate data rate during burst, measured at Ethernet level
   * \param frameSize Ethernet frame size
   * \param startTime time that first packet of burst is sent
   * \param duration duration of packet generation
   * \param dscpValue DSCP value for the IP packets
   * \param ecnValue ECN value for the IP packets
   * \return pointers to the UdpClient and UdpServer
   */
  std::pair<Ptr<UdpClient>, Ptr<UdpServer> > AddPacketBurstDuration (Ptr<Node> client, Ptr<Node> server,  
    uint16_t serverPort, uint16_t clientPort, DataRate dataRate,
    uint16_t frameSize, Time startTime, Time duration, 
    Ipv4Header::DscpType dscpValue, Ipv4Header::EcnType ecnValue) const;

  /**
   * Add a UDP stream that sends a packet train of fixed-sized UDP/IPv4 packets
   * (with configurable frame size and data rate), starting at a prescribed
   * time and until a specified count is reached.  This is a variant of
   * AddPacketBurstDuration ().
   *
   * \sa DocsisHelper::AddPacketBurstDuration
   *
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param clientPort Client port
   * \param dataRate data rate during burst, measured at Ethernet level
   * \param frameSize Ethernet frame size
   * \param startTime time that first packet of burst is sent
   * \param frameCount number of frames to send
   * \param dscpValue DSCP value for the socket
   * \param ecnValue ECN value for the socket
   * \return pointers to the UdpClient and UdpServer
   */
  std::pair<Ptr<UdpClient>, Ptr<UdpServer> > AddPacketBurstCount (Ptr<Node> client, Ptr<Node> server,
    uint16_t serverPort, uint16_t clientPort, DataRate dataRate,
    uint16_t frameSize, Time startTime, uint32_t frameCount,
    Ipv4Header::DscpType dscpValue, Ipv4Header::EcnType ecnValue) const;

  /**
   * Add a UDP stream that sends IP packets of size 'packetSize'
   * in a bursty manner ('on' for dutyCycle * period, 'off' for
   * (1 - dutyCycle) * period) at a specified rate during the 'on' time.
   * The packet size is computed at the UDP payload level (i.e. application
   * data unit) so the Ethernet frame size will include the addition of 
   * UDP header, IPv4 header, and Ethernet header/trailer to the configured
   * size.
   *
   * Packets are IPv4 and marked as DSCP EF.  Packet size should
   * be between 14 and 1472 bytes to result in Ethernet frame sizes of between
   * 64 and 1518 bytes.
   *
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param clientPort Client port
   * \param rate data rate for the on period
   * \param packetSize target application packet size (UDP payload)
   * \param onTime On time
   * \param offTime Off time
   * \param startTime Start time
   * \param stopTime Stop time
   * \param dscpValue (optional) DSCP value for the socket
   * \return pointer to the sending-side OnOffApplication
   */
  Ptr<OnOffApplication>
  AddDutyCycleUdpStream (Ptr<Node> client, Ptr<Node> server,
    uint16_t serverPort, uint16_t clientPort, DataRate rate,
    uint16_t packetSize, Time onTime, Time offTime, Time startTime, 
    Time stopTime, Ipv4Header::DscpType dscpVal = Ipv4Header::DscpDefault,
    Ipv4Header::EcnType ecnVal = Ipv4Header::ECN_ECT1) const;

  /**
   * Add a TCP file transfer consisting of an ns3::FileTransferApplication
   * on the sending side, and an ns3::PacketSink on the receive side.
   *
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param startTime Start time
   * \param stopTime Stop time
   * \param fileSize file size (bytes)
   * \param useReadingTime whether to use readingTime and repeat the transfer 
   * \param dscpValue (optional) DSCP value for the socket
   * \return pointer to the sending-side file transfer application
   */
  Ptr<FileTransferApplication> AddFileTransfer (Ptr<Node> client, 
    Ptr<Node> server, uint16_t serverPort, 
    Time startTime, Time stopTime, uint32_t fileSize, 
    bool useReadingTime, Ipv4Header::DscpType dscpVal = Ipv4Header::DscpDefault) const;

private:
  /**
   * Find and return the IPv4 address on the endpoint node.  The node must
   * have only one interface and one IPv4 address.
   * \param node the node to query
   * \return IPv4 address for the node's interface
   */
  Ipv4Address GetEndpointIpv4Address (Ptr<Node> node) const;
  /**
   * Add a notional game flow (UDP packet stream from client to server) with
   * the following properties:  1) mean interarrival time of 33 ms, std. dev.
   * of 3 ms, according to a Normal distribution, 2) mean packet size of 
   * 128 bytes, std. dev. of 20 bytes, according to a Normal distribution.
   * The packet size is computed at the Ethernet level (i.e. with Ethernet
   * framing included), so the actual application data unit size is backed
   *  off for headers.  Packets are IPv4 and marked as DSCP EF unless the
   * DSCP value is optionally overridden.
   *
   * \param client Node pointer to client (packet sender)
   * \param server Node pointer to server (packet receiver)
   * \param serverPort Server port
   * \param iaTime mean interarrival time
   * \param iaStdDev interarrival standard deviation
   * \param pktSize mean packet size (bytes)
   * \param pktStdDev packet size standard deviation (bytes)
   * \param startTime Start time
   * \param stopTime Stop time
   * \param dscpValue (optional) DSCP value for the socket, if not DSCP_EF
   * \return pointer to the traffic generating GameClient
   */
  Ptr<GameClient> AddGameSession (Ptr<Node> client, Ptr<Node> server, 
    uint16_t serverPort, Time iaTime,
    Time iaStdDev, uint32_t pktSize, uint32_t pktStdDev,
    Time startTime, Time stopTime, 
    Ipv4Header::DscpType dscpVal = Ipv4Header::DSCP_EF) const;

  /**
   * \brief Enable pcap output the indicated net device.
   *
   * NetDevice-specific implementation mechanism for hooking the trace and
   * writing to the trace file.
   *
   * \param prefix Filename prefix to use for pcap files.
   * \param nd Net device for which you want to enable tracing.
   * \param promiscuous If true capture all possible packets available at the device.
   * \param explicitFilename Treat the prefix as an explicit filename if true
   */
  virtual void EnablePcapInternal (std::string prefix, Ptr<NetDevice> nd, bool promiscuous, bool explicitFilename);

  /**
   * \brief Enable ascii trace output on the indicated net device.
   *
   * NetDevice-specific implementation mechanism for hooking the trace and
   * writing to the trace file.
   *
   * \param stream The output stream object to use when logging ascii traces.
   * \param prefix Filename prefix to use for ascii trace files.
   * \param nd Net device for which you want to enable tracing.
   * \param explicitFilename Treat the prefix as an explicit filename if true
   */
  virtual void EnableAsciiInternal (
    Ptr<OutputStreamWrapper> stream,
    std::string prefix,
    Ptr<NetDevice> nd,
    bool explicitFilename);

  ObjectFactory m_channelFactory;       //!< Channel Factory
  ObjectFactory m_upstreamDeviceFactory;        //!< Device Factory
  ObjectFactory m_downstreamDeviceFactory;        //!< Device Factory
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_HELPER_H */
