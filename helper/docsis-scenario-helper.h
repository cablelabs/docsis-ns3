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
   * \param upstreamMsr Upstream MSR
   * \param downstreamMsr Downstream MSR
   * \param classicQueueDepthTime classic queue size (expressed in time)
   */
  void CreateBasicNetwork (DataRate upstreamMsr, DataRate downstreamMsr, Time classicQueueDepthTime);

  /**
   * \brief Create a basic DOCSIS network topology with 16 client nodes
   *        on the LAN and 16 server nodes on the WAN, and a default classic
   *        queue depth time of 250ms
   * \param upstreamMsr Upstream MSR
   * \param downstreamMsr Downstream MSR
   */
  void CreateBasicNetwork (DataRate upstreamMsr, DataRate downstreamMsr);

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
   * \param scenarioId prefix of pcap file name
   */
  void EnablePcap (std::string prefix);

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
