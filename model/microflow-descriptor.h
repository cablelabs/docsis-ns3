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
 *  Authors:
 *  Tom Henderson <tomh@tomh.org>
 */

#ifndef DOCSIS_MICROFLOW_DESCRIPTOR_H
#define DOCSIS_MICROFLOW_DESCRIPTOR_H

#include "ns3/address.h"

namespace ns3 {
namespace docsis {

/**
 * \brief Structure for a microflow descriptor.  In the DOCSIS specification,
 * this corresponds to 'uflw'.
 */
struct Uflw
{
  Address source;                 //!< Source address
  Address destination;            //!< Destination address
  uint8_t protocol {0};           //!< Protocol
  uint16_t sourcePort {0};        //!< Source port if present
  uint16_t destinationPort {0};   //!< Destination port if present
  uint32_t h32 {0};               //!< Resulting 32-bit hash value
};

inline std::ostream & operator << (std::ostream & os, const Uflw & h)
{
  os << "src: " << h.source << " srcPort: " << h.sourcePort << " dst: "
     << h.destination << " dstPort: " << h.destinationPort << " proto: "
     << (uint16_t) h.protocol << " h32: " << h.h32;
  return os;
}

inline bool operator == (const Uflw &rhs, const Uflw &lhs)
{
  return (rhs.source == lhs.source &&
          rhs.destination == lhs.destination &&
          rhs.protocol == lhs.protocol &&
          rhs.sourcePort == lhs.sourcePort &&
          rhs.destinationPort == lhs.destinationPort &&
          rhs.h32 == lhs.h32);
}

}    // namespace docsis
}    // namespace ns3
#endif
