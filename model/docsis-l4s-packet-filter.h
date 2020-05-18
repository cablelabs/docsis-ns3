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

#ifndef DOCSIS_LOW_LATENCY_PACKET_FILTER_H
#define DOCSIS_LOW_LATENCY_PACKET_FILTER_H

#include "ns3/object.h"
#include "ns3/packet-filter.h"
#include "ns3/traffic-control-layer.h"
#include "microflow-descriptor.h"

namespace ns3 {

namespace docsis {

/**
 * \ingroup docsis
 *
 * Packet filter for Dual Queue Coupled AQM to classify packets into the
 * low latency queue.  In general, the LL queue of the dual queue is
 * mainly designed for so-called 'L4S' flows-- TCP flows that mark ECT(1)
 * and that implement a scalable congestion control.  However, we also
 * classify DSCP EF-marked packets as low latency, as well as (optionally)
 * ECT(0) if needed (if Linux DCTCP is configured to only send ECT(0)).
 */
class DocsisLowLatencyPacketFilter: public PacketFilter {
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  
  static Uflw GenerateHash32 (Ptr<const QueueDiscItem> item, uint32_t perturbation);

  DocsisLowLatencyPacketFilter ();
  virtual ~DocsisLowLatencyPacketFilter ();

private:
  /**
   * \return true if IPv4 packet, false otherwise
   * \param item QueueDiscItem to inspect
   */
  virtual bool CheckProtocol (Ptr<QueueDiscItem> item) const;
  /**
   * \return 0 if Classic packet, 1 if LowLatency (Scalable) packet, and
   *         PacketFilter::PF_NO_MATCH if IPv4 header not found
   * \param item QueueDiscItem to inspect
   */
  virtual int32_t DoClassify (Ptr<QueueDiscItem> item) const;
  bool m_classifyEct0AsLl {false}; /// Classify ECT(0) as LowLatency
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_LOW_LATENCY_PACKET_FILTER */
