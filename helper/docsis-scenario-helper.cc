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

#include "docsis-scenario-helper.h"

#include "ns3/abort.h"
#include "ns3/application-container.h"
#include "ns3/arp-cache.h"
#include "ns3/bridge-helper.h"
#include "ns3/cm-net-device.h"
#include "ns3/cmts-net-device.h"
#include "ns3/csma-helper.h"
#include "ns3/csma-net-device.h"
#include "ns3/docsis-channel.h"
#include "ns3/docsis-configuration.h"
#include "ns3/docsis-net-device.h"
#include "ns3/dual-queue-coupled-aqm.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ipv4-interface.h"
#include "ns3/log.h"
#include "ns3/names.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue-disc-container.h"
#include "ns3/queue-protection.h"
#include "ns3/queue-size.h"
#include "ns3/simulator.h"
#include "ns3/string.h"

#include <algorithm>
#include <iomanip>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DocsisScenarioHelper");

namespace docsis
{

DocsisScenarioHelper::DocsisScenarioHelper()
{
}

void
DocsisScenarioHelper::CreateBasicNetwork()
{
    // Create the nodes required by the topology
    m_cm = CreateObject<Node>();
    Names::Add("CM", m_cm);
    m_cmts = CreateObject<Node>();
    Names::Add("CMTS", m_cmts);
    m_router = CreateObject<Node>();
    Names::Add("router", m_router);
    m_bridge = CreateObject<Node>();
    Names::Add("bridge", m_bridge);
    for (uint16_t i = 0; i < 16; i++)
    {
        m_clients[i] = CreateObject<Node>();
        m_servers[i] = CreateObject<Node>();
    }

    // Create node containers grouping nodes that need similar configuration
    NodeContainer lanClients;
    for (uint16_t i = 0; i < 16; i++)
    {
        lanClients.Add(m_clients[i]);
    }
    NodeContainer lanNodes = lanClients;
    // The CM also has a port on the bridge
    lanNodes.Add(m_cm);
    NodeContainer cmtsRouterNodes(m_cmts, m_router);
    std::vector<NodeContainer> routerServerNodes(16);
    for (uint16_t i = 0; i < 16; i++)
    {
        routerServerNodes[i].Add(m_router);
        routerServerNodes[i].Add(m_servers[i]);
    }

    // Use device helpers to create link and device configurations on the
    // above NodeContainers.

    // 1 Gbps and 1 microsecond delay for CSMA channels that connect
    // clients and the CM to the bridge
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(1)));
    NetDeviceContainer lanDevices;
    NetDeviceContainer bridgeDevices;
    NetDeviceContainer lanLink;
    for (uint32_t i = 0; i < lanNodes.GetN() - 1; i++)
    {
        // install a CSMA link between the ith LAN node and the bridge node
        lanLink = csma.Install(NodeContainer(lanNodes.Get(i), m_bridge));
        lanDevices.Add(lanLink.Get(0));
        bridgeDevices.Add(lanLink.Get(1));
    }
    // Add the Nth node (CM) to the bridge but not to the lanDevices container
    lanLink = csma.Install(NodeContainer(lanNodes.Get(lanNodes.GetN() - 1), m_bridge));
    bridgeDevices.Add(lanLink.Get(1));
    m_cmCsmaDevice = lanLink.Get(0);

    BridgeHelper bridgeHelper;
    bridgeHelper.Install(m_bridge, bridgeDevices);

    // The CMTS device will be indexed at 0, the CM at index 1
    m_docsisDevices = m_docsisHelper.Install(m_cmts, m_cm);

    NetDeviceContainer cmtsRouterDevices = csma.Install(cmtsRouterNodes);
    m_cmtsCsmaDevice = cmtsRouterDevices.Get(0);
    // Add the router interface to the container used for IP subnet addressing
    lanDevices.Add(cmtsRouterDevices.Get(1));
    // Save the MAC address for this last device
    Ptr<CsmaNetDevice> routerDevice = cmtsRouterDevices.Get(1)->GetObject<CsmaNetDevice>();
    Address routerMacAddress = routerDevice->GetAddress();

    NetDeviceContainer cmDevices;
    cmDevices.Add(m_docsisDevices.Get(1));
    cmDevices.Add(lanLink.Get(0));

    NetDeviceContainer cmtsDevices;
    cmtsDevices.Add(m_docsisDevices.Get(0));
    cmtsDevices.Add(cmtsRouterDevices.Get(0));

    bridgeHelper.Install(m_cm, cmDevices);
    bridgeHelper.Install(m_cmts, cmtsDevices);

    PointToPointHelper p2pHelper;
    p2pHelper.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    p2pHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
    p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(1500));
    QueueSize queueSize(QueueSizeUnit::PACKETS, 100);
    p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", QueueSizeValue(queueSize));
    std::vector<NetDeviceContainer> routerServerDevices(16);
    for (uint16_t i = 0; i < 16; i++)
    {
        routerServerDevices[i] = p2pHelper.Install(routerServerNodes[i]);
    }

    // Add internet stack to the nodes that are not CM or CMTS
    InternetStackHelper internet;
    internet.Install(lanClients);
    internet.Install(m_router);
    for (uint16_t i = 0; i < 16; i++)
    {
        internet.Install(m_servers[i]);
    }

    // Install dual queues, without service flow configuration yet
    Ptr<DualQueueCoupledAqm> dual = CreateObject<DualQueueCoupledAqm>();
    dual->SetAttribute("LlEstimator", StringValue("QDelayCoupledV"));
    dual->AddPacketFilter(CreateObject<DocsisLowLatencyPacketFilter>());
    Ptr<DocsisNetDevice> device = m_docsisDevices.Get(1)->GetObject<DocsisNetDevice>();
    device->SetQueue(dual);
    dual->SetQDelaySingleCallback(MakeCallback(&DocsisNetDevice::ExpectedDelay, device));
    dual->SetLoopDelayCallback(MakeCallback(&DocsisNetDevice::GetLoopDelayEstimate, device));
    Ptr<QueueProtection> queueProtection = CreateObject<QueueProtection>();
    queueProtection->SetHashCallback(MakeCallback(&DocsisLowLatencyPacketFilter::GenerateHash32));
    queueProtection->SetQueue(dual);
    dual->SetQueueProtection(queueProtection);
    m_upstreamDualQueue = dual;

    dual = CreateObject<DualQueueCoupledAqm>();
    dual->SetAttribute("LlEstimator", StringValue("QDelayCoupledL"));
    dual->AddPacketFilter(CreateObject<DocsisLowLatencyPacketFilter>());
    device = m_docsisDevices.Get(0)->GetObject<DocsisNetDevice>();
    device->SetQueue(dual);
    dual->SetQDelaySingleCallback(MakeCallback(&DocsisNetDevice::ExpectedDelay, device));
    dual->SetLoopDelayCallback(MakeCallback(&DocsisNetDevice::GetLoopDelayEstimate, device));
    queueProtection = CreateObject<QueueProtection>();
    queueProtection->SetHashCallback(MakeCallback(&DocsisLowLatencyPacketFilter::GenerateHash32));
    queueProtection->SetQueue(dual);
    dual->SetQueueProtection(queueProtection);
    m_downstreamDualQueue = dual;

    //
    // Assign IPv4 addresses
    //
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer lanInterfaces = ipv4.Assign(lanDevices);
    // Save the IP address of the router interface facing the CMTS, for later use
    Ipv4Address routerIpv4Address = lanInterfaces.GetAddress(lanInterfaces.GetN() - 1);
    std::vector<Ipv4InterfaceContainer> wanInterfaces(16);
    for (uint16_t i = 0; i < 16; i++)
    {
        ipv4.NewNetwork();
        wanInterfaces[i] = ipv4.Assign(routerServerDevices[i]);
        Ptr<Node> server = routerServerDevices[i].Get(1)->GetNode();
        Ipv4Address addr = wanInterfaces[i].GetAddress(1);
        m_serverIpAddresses.insert(std::pair<Ptr<Node>, Ipv4Address>(server, addr));
    }

    // To avoid packet drop due to delayed ARP reply across the DOCSIS link
    // (because ns-3 does not support proxy ARP at the CM), populate each
    // client's ARP cache with the entry for the router on the far-side
    // of the link
    std::pair<Ptr<Ipv4>, uint32_t> ifacePair1 = lanInterfaces.Get(0);
    Ptr<Ipv4Interface> iface1 =
        ifacePair1.first->GetObject<Ipv4L3Protocol>()->GetInterface(ifacePair1.second);
    Ptr<ArpCache> cache1 = iface1->GetArpCache();
    ArpCache::Entry* entry1 = cache1->Add(routerIpv4Address);
    entry1->SetMacAddress(routerMacAddress);
    entry1->MarkPermanent();
    std::pair<Ptr<Ipv4>, uint32_t> ifacePair2 = lanInterfaces.Get(1);
    Ptr<Ipv4Interface> iface2 =
        ifacePair2.first->GetObject<Ipv4L3Protocol>()->GetInterface(ifacePair2.second);
    Ptr<ArpCache> cache2 = iface2->GetArpCache();
    ArpCache::Entry* entry2 = cache2->Add(routerIpv4Address);
    entry2->SetMacAddress(routerMacAddress);
    entry2->MarkPermanent();
    std::pair<Ptr<Ipv4>, uint32_t> ifacePair3 = lanInterfaces.Get(2);
    Ptr<Ipv4Interface> iface3 =
        ifacePair3.first->GetObject<Ipv4L3Protocol>()->GetInterface(ifacePair3.second);
    Ptr<ArpCache> cache3 = iface3->GetArpCache();
    ArpCache::Entry* entry3 = cache3->Add(routerIpv4Address);
    entry3->SetMacAddress(routerMacAddress);
    entry3->MarkPermanent();

    //
    // Add global routing tables after the topology is fully constructed
    //
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
}

void
DocsisScenarioHelper::CreateBasicNetwork(Ptr<AggregateServiceFlow> upstreamAsf,
                                         Ptr<AggregateServiceFlow> downstreamAsf)
{
    CreateBasicNetwork();
    SetUpstreamAsf(upstreamAsf);
    SetDownstreamAsf(downstreamAsf);
}

void
DocsisScenarioHelper::CreateBasicNetwork(Ptr<AggregateServiceFlow> upstreamAsf,
                                         Ptr<ServiceFlow> downstreamSf)
{
    CreateBasicNetwork();
    SetUpstreamAsf(upstreamAsf);
    SetDownstreamSf(downstreamSf);
}

void
DocsisScenarioHelper::CreateBasicNetwork(Ptr<ServiceFlow> upstreamSf,
                                         Ptr<AggregateServiceFlow> downstreamAsf)
{
    CreateBasicNetwork();
    SetUpstreamSf(upstreamSf);
    SetDownstreamAsf(downstreamAsf);
}

void
DocsisScenarioHelper::CreateBasicNetwork(Ptr<ServiceFlow> upstreamSf, Ptr<ServiceFlow> downstreamSf)
{
    CreateBasicNetwork();
    SetUpstreamSf(upstreamSf);
    SetDownstreamSf(downstreamSf);
}

void
DocsisScenarioHelper::SetUpstreamAsf(Ptr<AggregateServiceFlow> asf)
{
    m_docsisDevices.Get(1)->GetObject<CmNetDevice>()->SetUpstreamAsf(asf);
}

void
DocsisScenarioHelper::SetDownstreamAsf(Ptr<AggregateServiceFlow> asf)
{
    m_docsisDevices.Get(0)->GetObject<CmtsNetDevice>()->SetDownstreamAsf(asf);
}

void
DocsisScenarioHelper::SetUpstreamSf(Ptr<ServiceFlow> sf)
{
    m_docsisDevices.Get(1)->GetObject<CmNetDevice>()->SetUpstreamSf(sf);
}

void
DocsisScenarioHelper::SetDownstreamSf(Ptr<ServiceFlow> sf)
{
    m_docsisDevices.Get(0)->GetObject<CmtsNetDevice>()->SetDownstreamSf(sf);
}

Ptr<CmNetDevice>
DocsisScenarioHelper::GetCmNetDevice() const
{
    NS_ABORT_MSG_UNLESS(m_docsisDevices.Get(1), "No upstream device found");
    return m_docsisDevices.Get(1)->GetObject<CmNetDevice>();
}

Ptr<CmtsNetDevice>
DocsisScenarioHelper::GetCmtsNetDevice() const
{
    NS_ABORT_MSG_UNLESS(m_docsisDevices.Get(0), "No downstream device found");
    return m_docsisDevices.Get(0)->GetObject<CmtsNetDevice>();
}

Ptr<Node>
DocsisScenarioHelper::GetClient(uint32_t index) const
{
    return m_clients[index];
}

Ptr<Node>
DocsisScenarioHelper::GetServer(uint32_t index) const
{
    return m_servers[index];
}

Ptr<DualQueueCoupledAqm>
DocsisScenarioHelper::GetUpstreamDualQueue() const
{
    return m_upstreamDualQueue;
}

Ptr<DualQueueCoupledAqm>
DocsisScenarioHelper::GetDownstreamDualQueue() const
{
    return m_downstreamDualQueue;
}

Ptr<QueueProtection>
DocsisScenarioHelper::GetUpstreamQueueProtection() const
{
    return m_upstreamDualQueue->GetQueueProtection();
}

Ptr<QueueProtection>
DocsisScenarioHelper::GetDownstreamQueueProtection() const
{
    return m_downstreamDualQueue->GetQueueProtection();
}

void
DocsisScenarioHelper::EnablePcap(std::string prefix)
{
    NS_LOG_FUNCTION(this << prefix);
    CsmaHelper csma;
    NS_ABORT_MSG_UNLESS(m_cmtsCsmaDevice && m_cmCsmaDevice, "Devices not found");
    csma.EnablePcap(prefix, m_cmtsCsmaDevice, true);
    csma.EnablePcap(prefix, m_cmCsmaDevice, true);
}

int64_t
DocsisScenarioHelper::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    NS_ABORT_MSG_UNLESS(m_cm && m_cmts, "Error: Called before CreateBasicNetwork ()");
    return m_docsisHelper.AssignStreams(m_docsisDevices, stream);
}

void
DocsisScenarioHelper::TraceUpstreamQueueDrop(Callback<void, Ptr<const QueueDiscItem>> cb)
{
    NS_LOG_FUNCTION(this);
    bool connected = m_upstreamDualQueue->TraceConnectWithoutContext("Drop", cb);
    NS_ASSERT_MSG(connected, "Failed to connect callback");
}

void
DocsisScenarioHelper::TraceUpstreamQueueEnqueue(Callback<void, Ptr<const QueueDiscItem>> cb)
{
    NS_LOG_FUNCTION(this);
    bool connected = m_upstreamDualQueue->TraceConnectWithoutContext("Enqueue", cb);
    NS_ASSERT_MSG(connected, "Failed to connect callback");
}

void
DocsisScenarioHelper::TraceUpstreamClassicSojournTime(Callback<void, Time> cb)
{
    NS_LOG_FUNCTION(this);
    m_docsisDevices.Get(1)->GetObject<CmNetDevice>()->TraceConnectWithoutContext(
        "ClassicSojournTime",
        cb);
}

} // namespace docsis
} // namespace ns3
