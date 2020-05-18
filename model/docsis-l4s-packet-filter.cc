/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
 *               2016 University of Washington
 *               2017-2020 Cable Television Laboratories, Inc.
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
 * Authors:  Stefano Avallone <stavallo@unina.it>
 *           Tom Henderson <tomhend@u.washington.edu>
 *           Pasquale Imputato <p.imputato@gmail.com>
 */

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/ipv4-header.h"
#include "ns3/queue-item.h"
#include "ns3/packet-filter.h"
#include "ns3/packet.h"
#include "docsis-l4s-packet-filter.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DocsisLowLatencyPacketFilter");

namespace docsis {

NS_OBJECT_ENSURE_REGISTERED (DocsisLowLatencyPacketFilter);

TypeId 
DocsisLowLatencyPacketFilter::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::DocsisLowLatencyPacketFilter")
    .SetParent<PacketFilter> ()
    .SetGroupName ("Docsis")
    .AddAttribute ("ClassifyEct0AsLowLatency", "Classify ECT(0) also as Low Latency",
                   BooleanValue (false),
                   MakeBooleanAccessor (&DocsisLowLatencyPacketFilter::m_classifyEct0AsLl),
                   MakeBooleanChecker ())
  ;
  return tid;
}

DocsisLowLatencyPacketFilter::DocsisLowLatencyPacketFilter ()
{
  NS_LOG_FUNCTION (this);
}

DocsisLowLatencyPacketFilter::~DocsisLowLatencyPacketFilter()
{
  NS_LOG_FUNCTION (this);
}

bool
DocsisLowLatencyPacketFilter::CheckProtocol (Ptr<QueueDiscItem> item) const
{
  NS_LOG_FUNCTION (this << item);
  if (item->GetProtocol () == 0x0800)
    {
      return true;
    }
  return false;
}

Uflw
DocsisLowLatencyPacketFilter::GenerateHash32 (Ptr<const QueueDiscItem> item, uint32_t perturbation)
{
  NS_LOG_FUNCTION_NOARGS ();
  uint16_t protocol = item->GetProtocol ();
  if (protocol != 0x0800)
    {
      Uflw h;
      h.h32 = 0;
      return h; // may be ARP or other non-IP packet
    }
  // We must create a copy of the packet to remove/peek headers
  Ptr<Packet> pkt = item->GetPacket ()->Copy ();
  Ipv4Header hdr;
  pkt->RemoveHeader (hdr);
  Ipv4Address src = hdr.GetSource ();
  Ipv4Address dst = hdr.GetDestination ();
  uint8_t prot = hdr.GetProtocol ();

  uint16_t srcPort = 0;
  uint16_t dstPort = 0;

  TcpHeader tcpHdr;
  UdpHeader udpHdr;
  uint16_t fragOffset = hdr.GetFragmentOffset ();

  if (prot == 6 && fragOffset == 0) // TCP
    {
      pkt->PeekHeader (tcpHdr);
      srcPort = tcpHdr.GetSourcePort ();
      dstPort = tcpHdr.GetDestinationPort ();
    }
  else if (prot == 17 && fragOffset == 0) // UDP
    {
      pkt->PeekHeader (udpHdr);
      srcPort = udpHdr.GetSourcePort ();
      dstPort = udpHdr.GetDestinationPort ();
    }
  /* serialize the 5-tuple and the perturbation in buf */
  uint8_t buf[17];
  src.Serialize (buf);
  dst.Serialize (buf + 4);
  buf[8] = prot;
  buf[9] = (srcPort >> 8) & 0xff;
  buf[10] = srcPort & 0xff;
  buf[11] = (dstPort >> 8) & 0xff;
  buf[12] = dstPort & 0xff;
  buf[13] = (perturbation >> 24) & 0xff;
  buf[14] = (perturbation >> 16) & 0xff;
  buf[15] = (perturbation >> 8) & 0xff;
  buf[16] = perturbation & 0xff;

  // murmur3 hash in ns-3 core module (hash-murmur3.cc)
  uint32_t hash = Hash32 ((char*) buf, 17);

  NS_LOG_DEBUG ("Generate hash for 5-tuple src: " << src << " dst: " <<
                dst << " proto: " << (uint16_t) prot << " srcPort: " <<
                srcPort << " dstPort: "<< dstPort << " hash: " << hash);
  Uflw h;
  h.source = src;
  h.destination = dst;
  h.protocol = prot;
  h.sourcePort = srcPort;
  h.destinationPort = dstPort;
  h.h32 = hash;
  return h;
}

int32_t
DocsisLowLatencyPacketFilter::DoClassify (Ptr<QueueDiscItem> item) const
{
  NS_LOG_FUNCTION (this << item);
  uint16_t protocol = item->GetProtocol ();
  if (protocol != 0x0800)
    {
      return PacketFilter::PF_NO_MATCH;
    }
  Ipv4Header hdr;
  item->GetPacket ()->PeekHeader (hdr);
  if ((hdr.GetEcn () == Ipv4Header::ECN_ECT1) ||
      (hdr.GetEcn () == Ipv4Header::ECN_CE) ||
      (hdr.GetDscp () == Ipv4Header::DSCP_EF))
    {
      NS_LOG_DEBUG ("IPv4 LL-eligible packet found");
      return 1;
    }
  else if (m_classifyEct0AsLl && hdr.GetEcn () == Ipv4Header::ECN_ECT0)
    {
      NS_LOG_DEBUG ("IPv4 LL-eligible packet (ECT(0)) found");
      return 1;
    }
  else
    {
      NS_LOG_DEBUG ("IPv4 found but not LL");
      return 0;
    }
}

} // namespace docsis
} // namespace ns3
