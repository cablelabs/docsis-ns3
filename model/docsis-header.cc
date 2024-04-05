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

#include "docsis-header.h"

#include "ns3/header.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DocsisHeader");

namespace docsis
{

NS_OBJECT_ENSURE_REGISTERED(DocsisHeader);

TypeId
DocsisHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::docsis::DocsisHeader")
                            .SetParent<Header>()
                            .SetGroupName("Docsis")
                            .AddConstructor<DocsisHeader>();
    return tid;
}

DocsisHeader::DocsisHeader(uint16_t extendedHeaderSize)
    : m_extendedHeaderSize(extendedHeaderSize)
{
}

DocsisHeader::DocsisHeader()
    : m_extendedHeaderSize(0)
{
}

TypeId
DocsisHeader::GetInstanceTypeId() const
{
    NS_LOG_FUNCTION(this);
    return GetTypeId();
}

void
DocsisHeader::Print(std::ostream& os) const
{
    NS_LOG_FUNCTION(this << &os);

    os << "DOCSIS Dummy Header 0x00:00:00:00:";
    for (uint16_t i = 0; i < m_extendedHeaderSize; i++)
    {
        os << "00:";
    }
    os << "00:00";
}

uint32_t
DocsisHeader::GetSerializedSize() const
{
    NS_LOG_FUNCTION(this);
    return 6 + m_extendedHeaderSize;
}

void
DocsisHeader::Serialize(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;
    i.WriteU8(0);      // FCS
    i.WriteU8(0);      // MAC_PARAM
    i.WriteHtonU16(0); // LEN
    for (uint16_t j = 0; j < m_extendedHeaderSize; j++)
    {
        i.WriteU8(0);
    }
    i.WriteHtonU16(0); // HCS
}

uint32_t
DocsisHeader::Deserialize(Buffer::Iterator start)
{
    NS_LOG_FUNCTION(this << &start);
    Buffer::Iterator i = start;
    i.ReadU8();      // FCS
    i.ReadU8();      // MAC_PARAM
    i.ReadNtohU16(); // LEN
    for (uint16_t j = 0; j < m_extendedHeaderSize; j++)
    {
        i.ReadU8();
    }
    i.ReadNtohU16(); // HCS
    return GetSerializedSize();
}

} // namespace docsis
} // namespace ns3
