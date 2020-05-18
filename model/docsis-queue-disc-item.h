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

#ifndef DOCSIS_QUEUE_DISC_ITEM_H
#define DOCSIS_QUEUE_DISC_ITEM_H

#include "ns3/address.h"
#include "ns3/ptr.h"
#include "ns3/queue-item.h"

namespace ns3 {

class Packet;

namespace docsis {

/**
 * \ingroup docsis
 * \class DocsisQueueDiscItem
 * The Docsis device holds a queue of packets that are pending transmission.
 * The ns-3 Queue class requires objects of type QueueDiscItem (more generalized
 * than simply packets).  The below specialization holds the packet as well
 * as the destination MAC address and protocol number and DOCSIS MAC header length.
 */
class DocsisQueueDiscItem : public QueueDiscItem {
public:
  DocsisQueueDiscItem (Ptr<Packet> p, const Address & source, const Address & dest, uint16_t protocol, uint32_t macHeaderLength);
  virtual ~DocsisQueueDiscItem ();
  virtual void AddHeader (void);
  virtual bool Mark (void);
  Address GetSource (void) const;
  uint32_t GetMacHeaderLength (void) const;

  // Document these four specialized
  bool SetUint8Value (Uint8Values field, const uint8_t &value);
  bool GetUint8Value (Uint8Values field, uint8_t &value) const;
  uint32_t GetValue (void) const;
  void SetValue (uint32_t value);

  /**
   * \brief Size of the packet within the queue disc, including Ethernet and DOCSIS MAC header
   *
   * \note The encapsulated packet should not have already added the
   * Ethernet framing; this method will add Ethernet framing bytes to
   * the value that it retrieves from Packet::GetSize ().
   *
   * \return the size of the DOCSIS packet (inc. Ethernet and DOCSIS header) included in this item, in bytes
   */
  virtual uint32_t GetSize (void) const;

private:
  Address m_source;
  uint32_t m_macHeaderLength;
  uint32_t m_value;
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_QUEUE_DISC_ITEM_H */
