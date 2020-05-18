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
 * Tom Henderson <tomh@tomh.org>
 */

#include "dual-queue-test.h"

using namespace ns3;
using namespace docsis;

DualQueueTestItem::DualQueueTestItem (Ptr<Packet> p, const Address & source, const Address & dest, uint16_t protocol, uint32_t macHeaderSize, bool isLowLatency)
  : DocsisQueueDiscItem (p, source, dest, protocol, macHeaderSize),
    m_isLowLatency (isLowLatency)
{
}

DualQueueTestItem::~DualQueueTestItem ()
{
}

void
DualQueueTestItem::AddHeader (void)
{
}

bool
DualQueueTestItem::Mark (void)
{
  return true;
}

bool
DualQueueTestItem::IsLowLatency (void) const
{
  return m_isLowLatency;
}

///////////////////////////////////////////////////////////////////////////

TypeId
DualQueueTestFilter::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::DualQueueTetstFilter")
    .SetParent<PacketFilter> ()
    .SetGroupName ("Docsis")
  ;
  return tid;
}

bool
DualQueueTestFilter::CheckProtocol (Ptr<QueueDiscItem> item) const
{
  if (DynamicCast<DualQueueTestItem> (item) != 0)
    {
      return true;
    }
  else
    {
      return false;
    }
}

int32_t
DualQueueTestFilter::DoClassify (Ptr<QueueDiscItem> item) const
{
  Ptr<DualQueueTestItem> qitem = DynamicCast<DualQueueTestItem> (item);
  if (qitem->IsLowLatency ())
    {
      return 1;
    }
  else
    {
      return 0;
    }
}
