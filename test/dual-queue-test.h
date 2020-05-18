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
 *
 */

#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/packet-filter.h"
#include "ns3/address.h"
#include "ns3/docsis-queue-disc-item.h"

using namespace ns3;
using namespace docsis;

// Define QueueDiscItem and PacketFilter for use in the tests (avoiding
// a dependence on the internet module)

class DualQueueTestItem : public DocsisQueueDiscItem
{
public:
  DualQueueTestItem (Ptr<Packet> p, const Address & source, const Address & dest, uint16_t protocol, uint32_t macHeaderSize, bool isLowLatency);
  virtual ~DualQueueTestItem ();
  virtual void AddHeader (void);
  virtual bool Mark (void);
  bool IsLowLatency (void) const;

private:
  DualQueueTestItem ();
  DualQueueTestItem (const DualQueueTestItem &);
  DualQueueTestItem &operator = (const DualQueueTestItem &);
  bool m_isLowLatency;
};

class DualQueueTestFilter : public PacketFilter
{
public:
  static TypeId GetTypeId (void);
  DualQueueTestFilter () {}
  virtual ~DualQueueTestFilter () {}
private:
  // DoClassify() and CheckProtocol() are required to be defined
  virtual bool CheckProtocol (Ptr<QueueDiscItem> item) const;
  virtual int32_t DoClassify (Ptr<QueueDiscItem> item) const;
};
