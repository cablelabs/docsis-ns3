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
 */

#include "ns3/log.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/node.h"
#include "ns3/ipv4-header.h"
#include "docsis-queue-disc-item.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DocsisQueueDiscItem");

namespace docsis {

DocsisQueueDiscItem::DocsisQueueDiscItem (Ptr<Packet> p, const Address& source, const Address& dest, uint16_t protocol, uint32_t macHeaderLength)
  : QueueDiscItem (p, dest, protocol),
    m_source (source),
    m_macHeaderLength (macHeaderLength),
    m_value (0)
{
}

DocsisQueueDiscItem::~DocsisQueueDiscItem (void)
{
}

void
DocsisQueueDiscItem::AddHeader (void)
{
  // Intentionally null
}

bool
DocsisQueueDiscItem::Mark (void)
{
  NS_LOG_FUNCTION (this);
  if (GetProtocol () == 0x0800)
    {
      Ipv4Header ipv4Header;
      GetPacket ()->PeekHeader (ipv4Header);
      if (ipv4Header.GetEcn () == Ipv4Header::ECN_ECT0 || ipv4Header.GetEcn () == Ipv4Header::ECN_ECT1)
        {
          GetPacket ()->RemoveHeader (ipv4Header);
          ipv4Header.SetEcn (Ipv4Header::ECN_CE);
          NS_LOG_DEBUG ("Marked packet");
          if (Node::ChecksumEnabled ())
            {
              ipv4Header.EnableChecksum ();
            }
          GetPacket ()->AddHeader (ipv4Header);
          return true;
        }
    }
  NS_LOG_DEBUG ("Unable to mark packet");
  return false;
}

Address
DocsisQueueDiscItem::GetSource (void) const
{
  return m_source;
}

uint32_t
DocsisQueueDiscItem::GetMacHeaderLength (void) const
{
  return m_macHeaderLength;
}

uint32_t
DocsisQueueDiscItem::GetSize (void) const
{
  NS_ASSERT (GetPacket () != 0);
  uint32_t size = std::max<uint32_t> (GetPacket ()->GetSize (), 46) + 14 + 4;
  size += GetMacHeaderLength ();
  return size;
}

bool
DocsisQueueDiscItem::SetUint8Value (QueueItem::Uint8Values field, const uint8_t& value)
{
  bool ret = false;

  switch (field)
    {
    case IP_DSFIELD:
      if (GetProtocol () == 0x0800)
        {
          Ipv4Header ipv4Header;
          GetPacket ()->RemoveHeader (ipv4Header);
          ipv4Header.SetTos (value);
          if (Node::ChecksumEnabled ())
            {
              ipv4Header.EnableChecksum ();
            }
          GetPacket ()->AddHeader (ipv4Header);
          ret = true;
        }
      break;
    }

  return ret;
}

bool
DocsisQueueDiscItem::GetUint8Value (QueueItem::Uint8Values field, uint8_t& value) const
{
  bool ret = false;

  switch (field)
    {
    case IP_DSFIELD:
      if (GetProtocol () == 0x0800)
        {
          Ipv4Header ipv4Header;
          GetPacket ()->PeekHeader (ipv4Header);
          value = ipv4Header.GetTos ();
          ret = true;
        }
      break;
    }
  return ret;
}

uint32_t
DocsisQueueDiscItem::GetValue (void) const
{
  return m_value;
}

void
DocsisQueueDiscItem::SetValue (uint32_t value)
{
  NS_LOG_FUNCTION (this << value);
  m_value = value;
}


} // namespace docsis
} // namespace ns3
