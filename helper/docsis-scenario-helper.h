/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017-2020 Cable Television Laboratories, Inc.
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
 */
#ifndef DOCSIS_SCENARIO_HELPER_H
#define DOCSIS_SCENARIO_HELPER_H

#include <string>

#include "ns3/net-device-container.h"
#include "ns3/ipv4-header.h"

namespace ns3 {

class NetDevice;
class Node;
class Ipv4Address;
class DataRate;
class QueueSize;
class QueueDiscItem;
class GameClient;

namespace docsis {

class CmNetDevice;
class CmtsNetDevice;
class CmtsUpstreamScheduler;
class DualQueueCoupledAqm;
class QueueProtection;
class AggregateServiceFlow;
class ServiceFlow;

/**
 * \ingroup docsis
 * \brief A helper to create a basic DOCSIS network with clients and servers
 *
 * This helper creates and maintains state of a basic DOCSIS network with
 * sixteen clients and servers, easing configuration of a common case
 * DOCSIS simulation scenario.
 *
 * \sa ns3::DocsisHelper
 */
class DocsisScenarioHelper
{
public:
  /**
   * Constructor
   */
  DocsisScenarioHelper ();
  /**
   * Destructor
   */
  virtual ~DocsisScenarioHelper () {}

  /**
   * \brief Create a basic DOCSIS network topology with 16 client nodes
   *        on the LAN and 16 server nodes on the WAN
   *
   * This method requires the caller to later add upstream and downstream
   * service flow configuration.
   */
  void CreateBasicNetwork ();

  /**
   * \brief Create a basic DOCSIS network topology with 16 client nodes
   *        on the LAN and 16 server nodes on the WAN
   * \param upstreamAsf Upstream ASF 
   * \param downstreamAsf Downstream ASF 
   */
  void CreateBasicNetwork (Ptr<AggregateServiceFlow> upstreamAsf, Ptr<AggregateServiceFlow> downstreamAsf);

  /**
   * \brief Create a basic DOCSIS network topology with 16 client nodes
   *        on the LAN and 16 server nodes on the WAN
   * \param upstreamAsf Upstream ASF 
   * \param downstreamSf Downstream SF 
   */
  void CreateBasicNetwork (Ptr<AggregateServiceFlow> upstreamAsf, Ptr<ServiceFlow> downstreamSf);

  /**
   * \brief Create a basic DOCSIS network topology with 16 client nodes
   *        on the LAN and 16 server nodes on the WAN
   * \param upstreamSf Upstream SF 
   * \param downstreamAsf Downstream ASF 
   */
  void CreateBasicNetwork (Ptr<ServiceFlow> upstreamSf, Ptr<AggregateServiceFlow> downstreamAsf);

  /**
   * \brief Create a basic DOCSIS network topology with 16 client nodes
   *        on the LAN and 16 server nodes on the WAN
   * \param upstreamSf Upstream SF 
   * \param downstreamSf Downstream SF 
   */
  void CreateBasicNetwork (Ptr<ServiceFlow> upstreamSf, Ptr<ServiceFlow> downstreamSf);

  /**
   * Add upstream aggregate service flow.  This operation must be done after
   * CreateBasicNetwork has been called, and is intended for use with the
   * variant of CreateBasicNetwork that takes no arguments.  
   * 
   * Either an AggregateServiceFlow or a single ServiceFlow object should
   * be present, but not both.  It is a simulation error (misconfiguration)
   * to try to set both.  If an ASF has already been set, this method will
   * overwrite the previous ASF.  If a SF has already been set, this method
   * will abort the simulation.
   *
   * \param asf pointer to the AggregateServiceFlow object
   */
  void SetUpstreamAsf (Ptr<AggregateServiceFlow> asf);

  /**
   * Add downstream aggregate service flow.  This operation must be done after
   * CreateBasicNetwork has been called, and is intended for use with the
   * variant of CreateBasicNetwork that takes no arguments.  
   * 
   * Either an AggregateServiceFlow or a single ServiceFlow object should
   * be present, but not both.  It is a simulation error (misconfiguration)
   * to try to set both.  If an ASF has already been set, this method will
   * overwrite the previous ASF.  If a SF has already been set, this method
   * will abort the simulation.
   *
   * \param asf pointer to the AggregateServiceFlow object
   */
  void SetDownstreamAsf (Ptr<AggregateServiceFlow> asf);

  /**
   * Add upstream service flow.  This operation must be done after
   * CreateBasicNetwork has been called, and is intended for use with the
   * variant of CreateBasicNetwork that takes no arguments.  
   * 
   * Either an AggregateServiceFlow or a single ServiceFlow object should
   * be present, but not both.  It is a simulation error (misconfiguration)
   * to try to set both.  If an SF has already been set, this method will
   * overwrite the previous SF.  If an ASF has already been set, this method
   * will abort the simulation.
   *
   * \param sf pointer to the ServiceFlow object
   */
  void SetUpstreamSf (Ptr<ServiceFlow> sf);

  /**
   * Add downstream service flow.  This operation must be done after
   * CreateBasicNetwork has been called, and is intended for use with the
   * variant of CreateBasicNetwork that takes no arguments.  
   * 
   * Either an AggregateServiceFlow or a single ServiceFlow object should
   * be present, but not both.  It is a simulation error (misconfiguration)
   * to try to set both.  If an SF has already been set, this method will
   * overwrite the previous SF.  If a ASF has already been set, this method
   * will abort the simulation.
   *
   * \param sf pointer to the ServiceFlow object
   */
  void SetDownstreamSf (Ptr<ServiceFlow> sf);

  /**
   * \brief Utility function to return properly casted device pointer
   * \return pointer to a CmNetDevice
   */
  Ptr<CmNetDevice> GetCmNetDevice (void) const;

  /**
   * \brief Utility function to return properly casted device pointer
   * \return pointer to a CmNetDevice
   */
  Ptr<CmtsNetDevice> GetCmtsNetDevice (void) const;

  /**
   * Get pointer to i'th client node
   * Must be called after CreateBasicNetwork () 
   * \param index index of requested client node
   * \return pointer to requested client node
   */
  Ptr<Node> GetClient (uint32_t index) const;

  /**
   * Get pointer to i'th server node
   * Must be called after CreateBasicNetwork () 
   * \param index index of requested server node
   * \return pointer to requested server node
   */
  Ptr<Node> GetServer (uint32_t index) const;

  /**
   * Get pointer to upstream dual queue object
   * Must be called after CreateBasicNetwork () 
   * \return pointer to upstream DualQueueCoupledAqm object
   */
  Ptr<DualQueueCoupledAqm> GetUpstreamDualQueue (void) const;

  /**
   * Get pointer to downstream dual queue object
   * Must be called after CreateBasicNetwork () 
   * \return pointer to downstream DualQueueCoupledAqm object
   */
  Ptr<DualQueueCoupledAqm> GetDownstreamDualQueue (void) const;

  /**
   * Get pointer to upstream dual queue object
   * Must be called after CreateBasicNetwork () 
   * \return pointer to upstream QueueProtection object
   */
  Ptr<QueueProtection> GetUpstreamQueueProtection (void) const;

  /**
   * Get pointer to downstream queue protection object
   * Must be called after CreateBasicNetwork () 
   * \return pointer to downstream QueueProtection object
   */
  Ptr<QueueProtection> GetDownstreamQueueProtection (void) const;

  /**
   * \brief Wrapper to hook a callback to the Drop trace of the dual queue
   *
   * To use, create a function or method that returns void and takes a 
   * Ptr<const QueueDiscItem> argument and wrap it using MakeCallback()
   *
   * Note:  Drops are registered in the parent dual-queue queue disc.  This
   * trace will not distinguish between a drop due to C-queue early drop
   * or overflow, or a tail drop from the L-queue if any such exist.
   * 
   * \param cb Callback to connect
   */
  void TraceUpstreamQueueDrop (Callback<void, Ptr<const QueueDiscItem> > cb);

  /**
   * \brief Wrapper to hook a callback to the Enqueue trace of the upstream
   *        dual queue
   *
   * To use, create a function or method that returns void and takes a 
   * Ptr<const QueueDiscItem> argument and wrap it using MakeCallback()
   *
   * \param cb Callback to connect
   */
  void TraceUpstreamQueueEnqueue (Callback<void, Ptr<const QueueDiscItem> > cb);
  /**
   * \brief Wrapper to hook a callback to the sojourn time trace of the 
   *        upstream classic queue.
   *
   * To use, create a function or method that returns void and takes a 
   * Ptr<Time> argument and wrap it using MakeCallback()
   *
   * \param cb Callback to connect
   */
  void TraceUpstreamClassicSojournTime (Callback<void, Time> cb);

  // Can add similar traces for upstream LQueue Mark and Enqueue,
  // and downstream variants

  /**
   * Configure pcap tracing on the LAN side of the CM and the WAN side
   * of the CMTS.  Must be called after CreateBasicNetwork () 
   *
   * \param prefix of pcap file name
   */
  void EnablePcap (std::string prefix);

  /**
  * Assign a fixed random variable stream number to the random variables
  * used by the DOCSIS objects that have been instantiated by
  * CreateBasicNetwork().  This prevents the random variable stream
  * assignments from being perturbed by other, unrelated configuration
  * changes in the program.
  *
  * The argument passed in should be a non-negative integer < 2^63.  The
  * value returned is the number of stream indices that have been used;
  * in general, stream assignments should not be reused in a simulation.
  *
  * \param stream first stream index to use
  * \return the number of stream indices assigned by this helper
  */
  int64_t AssignStreams (int64_t stream);

private:
  DocsisHelper m_docsisHelper;
  Ptr<Node> m_cm    {nullptr};
  Ptr<Node> m_cmts  {nullptr};
  Ptr<Node> m_router  {nullptr};
  Ptr<Node> m_bridge  {nullptr};
  Ptr<NetDevice> m_cmCsmaDevice {nullptr};
  Ptr<DualQueueCoupledAqm> m_upstreamDualQueue {nullptr};
  Ptr<DualQueueCoupledAqm> m_downstreamDualQueue {nullptr};
  Ptr<QueueProtection> m_upstreamQueueProtection {nullptr};
  Ptr<QueueProtection> m_downstreamQueueProtection {nullptr};
  std::vector<Ptr<Node> > m_clients = std::vector<Ptr<Node> > (16);
  std::vector<Ptr<Node> > m_servers = std::vector<Ptr<Node> > (16);
  NetDeviceContainer m_docsisDevices;
  std::map<Ptr<Node>, Ipv4Address> m_serverIpAddresses;
  Ptr<NetDevice> m_cmtsCsmaDevice {nullptr};
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_SCENARIO_HELPER_H */
