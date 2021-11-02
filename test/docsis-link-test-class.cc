/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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

#include "docsis-link-test-class.h"
#include "ns3/log.h"
#include "ns3/object.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/simulator.h"
#include "ns3/docsis-helper.h"
#include "ns3/cm-net-device.h"
#include "ns3/cmts-upstream-scheduler.h"
#include "ns3/cmts-net-device.h"
#include "ns3/docsis-channel.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/net-device-container.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ipv4-interface.h"
#include "ns3/arp-cache.h"
#include "ns3/simple-net-device-helper.h"
#include "ns3/simple-net-device.h"
#include "ns3/queue-protection.h"
#include "ns3/bridge-helper.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DocsisLinkTestClass");

namespace docsis {

DocsisLinkTestCase::DocsisLinkTestCase (std::string name)
  : TestCase (name),
    m_upstreamBytesInQueue (0),
    m_downstreamBytesInQueue (0),
    m_upstreamRate (DataRate (50000000)),
    m_downstreamRate (DataRate (200000000))
{
}

DocsisLinkTestCase::~DocsisLinkTestCase ()
{
}

void
DocsisLinkTestCase::TraceBytesInQueue (std::string context, uint32_t oldValue, uint32_t newValue)
{
  if (context == "upstream")
    {
      m_upstreamBytesInQueue = newValue;
    }
  else if (context == "downstream")
    {
      m_downstreamBytesInQueue = newValue;
    }
}

void
DocsisLinkTestCase::SetUpstreamRate (DataRate upstreamRate)
{
  m_upstreamRate = upstreamRate;
}

Ptr<AggregateServiceFlow>
DocsisLinkTestCase::GetUpstreamAsf (void) const
{
  Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow> ();
  asf->m_asfid = 1;
  asf->m_maxSustainedRate = m_upstreamRate;
  asf->m_peakRate = DataRate (2 * m_upstreamRate.GetBitRate ());
  asf->m_maxTrafficBurst = static_cast<uint32_t> (m_upstreamRate.GetBitRate () * 0.1 / 8); // 100ms at MSR
  Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  sf->m_targetBuffer = static_cast<uint32_t> (m_upstreamRate.GetBitRate () * 0.25 / 8); // 250 ms at MSR
  asf->SetClassicServiceFlow (sf);
  Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
  asf->SetLowLatencyServiceFlow (sf2);
  return asf;
}

Ptr<AggregateServiceFlow>
DocsisLinkTestCase::GetDownstreamAsf (void) const
{
  Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow> ();
  asf->m_asfid = 1;
  asf->m_maxSustainedRate = m_downstreamRate;
  asf->m_peakRate = DataRate (2 * m_downstreamRate.GetBitRate ());
  asf->m_maxTrafficBurst = static_cast<uint32_t> (m_downstreamRate.GetBitRate () * 0.1 / 8); // 100ms at MSR
  Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
  sf->m_targetBuffer = static_cast<uint32_t> (m_downstreamRate.GetBitRate () * 0.25 / 8); // 250 ms at MSR
  asf->SetClassicServiceFlow (sf);
  Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
  asf->SetLowLatencyServiceFlow (sf2);
  return asf;
}

void
DocsisLinkTestCase::SetupFourNodeTopology (void)
{
  m_lanNode = CreateObject<Node> (); // Node ID 0
  m_cmNode = CreateObject<Node> (); // Node ID 1
  m_cmtsNode = CreateObject<Node> (); // Node ID 2
  m_wanNode = CreateObject<Node> (); // Node ID 3

  // Use SimpleNetDevice (to avoid csma module dependency)
  SimpleNetDeviceHelper simple;
  NodeContainer simple1 (m_lanNode);
  simple1.Add (m_cmNode);
  NodeContainer simple2 (m_cmtsNode);
  simple2.Add (m_wanNode);
  NetDeviceContainer n0n1Device = simple.Install (simple1);
  NetDeviceContainer n2n3Device = simple.Install (simple2);

  // Configure a small amount of channel delay to improve trace readability
  Ptr<SimpleNetDevice> simpleDevice = n0n1Device.Get (0)->GetObject<SimpleNetDevice> ();
  simpleDevice->GetChannel ()->SetAttribute ("Delay", TimeValue (MicroSeconds (1)));
  simpleDevice = n2n3Device.Get (0)->GetObject<SimpleNetDevice> ();
  simpleDevice->GetChannel ()->SetAttribute ("Delay", TimeValue (MicroSeconds (1)));

  DocsisHelper docsis;
  docsis.SetChannelAttribute ("Delay", StringValue ("1ms"));
  // Install arguments for DocsisHelper: (downstream (cmts), upstream (cm)). 
  NetDeviceContainer n2n1Device = docsis.Install (m_cmtsNode, m_cmNode);
  m_upstream = docsis.GetUpstream (n2n1Device);
  m_downstream = docsis.GetDownstream (n2n1Device);
  m_channel = docsis.GetChannel (n2n1Device);

  // Set the random variables to avoid unwanted perturbations of random values
  m_upstream->AssignStreams (1);
  m_downstream->AssignStreams (11);
  m_upstream->GetCmtsUpstreamScheduler ()->AssignStreams (21);

  // Add an IP stack
  InternetStackHelper internet;
  internet.Install (m_lanNode);
  internet.Install (m_wanNode);

  // bridge the SimpleNetDevice and DocsisDevice on the two nodes
  BridgeHelper bridge;
  NetDeviceContainer cmDevices (n0n1Device.Get (1));
  cmDevices.Add (n2n1Device.Get (1));
  bridge.Install (m_cmNode, cmDevices);
  NetDeviceContainer cmtsDevices (n2n1Device.Get (0));
  cmtsDevices.Add (n2n3Device.Get (0));
  bridge.Install (m_cmtsNode, cmtsDevices);

  // Create a special device container with the NetDevices from node 0 and 3
  // for installing IP addresses
  NetDeviceContainer endpointDevices;
  endpointDevices.Add (n0n1Device.Get (0));
  endpointDevices.Add (n2n3Device.Get (1));

  // Add IP addresses to the endpoints
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.252");
  Ipv4InterfaceContainer ipv4Interfaces = ipv4.Assign (endpointDevices);

  // Configure static entries in ARP table; avoids address resolution delay
  std::pair<Ptr<Ipv4>, uint32_t> ifacePair0 = ipv4Interfaces.Get (0);
  std::pair<Ptr<Ipv4>, uint32_t> ifacePair1 = ipv4Interfaces.Get (1);
  Ptr<Ipv4Interface> iface0 = ifacePair0.first->GetObject<Ipv4L3Protocol> ()->GetInterface (ifacePair0.second);
  Ptr<Ipv4Interface> iface1 = ifacePair1.first->GetObject<Ipv4L3Protocol> ()->GetInterface (ifacePair1.second);
  Ptr<ArpCache> cache0 = iface0->GetArpCache ();
  Ptr<ArpCache> cache1 = iface1->GetArpCache ();
  ArpCache::Entry* entry0 = cache0->Add (Ipv4Address ("10.1.1.2"));
  entry0->SetMacAddress (iface1->GetDevice ()->GetAddress ());
  entry0->MarkPermanent ();
  ArpCache::Entry* entry1 = cache1->Add (Ipv4Address ("10.1.1.1"));
  entry1->SetMacAddress (iface0->GetDevice ()->GetAddress ());
  entry1->MarkPermanent ();
}

} // namespace docsis
} // namespace ns3
