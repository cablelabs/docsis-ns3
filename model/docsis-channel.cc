/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007, 2008 University of Washington
 *               2017-2020 Cable Television Laboratories, Inc. (DOCSIS changes)
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
 * Authors:
 *   Tom Henderson <tomh@tomh.org> (adapted PointToPoint to Docsis channel)
 */

#include "docsis-channel.h"
#include "docsis-net-device.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DocsisChannel");

namespace docsis {

NS_OBJECT_ENSURE_REGISTERED (DocsisChannel);

TypeId 
DocsisChannel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::docsis::DocsisChannel")
    .SetParent<Channel> ()
    .SetGroupName ("Docsis")
    .AddConstructor<DocsisChannel> ()
    .AddAttribute ("Delay", "Propagation delay through the channel",
                   TimeValue (Seconds (0)),
                   MakeTimeAccessor (&DocsisChannel::m_delay),
                   MakeTimeChecker ())
    .AddTraceSource ("TxRxDocsis",
                     "Trace source indicating start of transmission of packet "
                     "on the DocsisChannel",
                     MakeTraceSourceAccessor (&DocsisChannel::m_txrxDocsis),
                     "ns3::DocsisChannel::TxRxDocsisCallback")
  ;
  return tid;
}

DocsisChannel::DocsisChannel()
  :
    Channel (),
    m_delay (Seconds (0.)),
    m_nDevices (0)
{
  NS_LOG_FUNCTION (this);
}

void
DocsisChannel::Attach (Ptr<DocsisNetDevice> device)
{
  NS_LOG_FUNCTION (this << device);
  NS_ASSERT_MSG (m_nDevices < N_DEVICES, "Only two devices permitted");
  NS_ASSERT (device != 0);

  m_link[m_nDevices++].m_src = device;
  if (m_nDevices == N_DEVICES)
    {
      m_link[0].m_dst = m_link[1].m_src;
      m_link[1].m_dst = m_link[0].m_src;
    }
}

bool
DocsisChannel::TransmitStart (Ptr<Packet> p, Ptr<DocsisNetDevice> src,
  Time txTime)
{
  NS_LOG_FUNCTION (this << p << src);
  NS_LOG_LOGIC ("UID is " << p->GetUid () << ")");

  uint32_t wire = src == m_link[0].m_src ? 0 : 1;

  NS_LOG_DEBUG ("Transmit time " << txTime.GetSeconds () << " delay " << m_delay.GetSeconds ());
  Simulator::ScheduleWithContext (m_link[wire].m_dst->GetNode ()->GetId (),
                                  txTime + m_delay, &DocsisNetDevice::Receive,
                                  m_link[wire].m_dst, p);

  // Call the tx trace source callback
  m_txrxDocsis (p, src, m_link[wire].m_dst, txTime, txTime + m_delay);
  return true;
}

std::size_t
DocsisChannel::GetNDevices (void) const
{
  return m_nDevices;
}

Ptr<DocsisNetDevice>
DocsisChannel::GetDocsisDevice (std::size_t i) const
{
  NS_ASSERT (i < 2);
  return m_link[i].m_src;
}

Ptr<NetDevice>
DocsisChannel::GetDevice (std::size_t i) const
{
  return GetDocsisDevice (i);
}

void
DocsisChannel::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  for (uint32_t i = 0; i < N_DEVICES; i++)
    {
      m_link[i].m_src = 0;
      m_link[i].m_dst = 0;
    }
}

Time
DocsisChannel::GetDelay (void) const
{
  return m_delay;
}

Ptr<DocsisNetDevice>
DocsisChannel::GetSource (uint32_t i) const
{
  return m_link[i].m_src;
}

Ptr<DocsisNetDevice>
DocsisChannel::GetDestination (uint32_t i) const
{
  return m_link[i].m_dst;
}

bool
DocsisChannel::IsInitialized (void) const
{
  return true;
}

} // namespace docsis
} // namespace ns3
