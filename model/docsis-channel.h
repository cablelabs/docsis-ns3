/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 University of Washington
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

#ifndef DOCSIS_CHANNEL_H
#define DOCSIS_CHANNEL_H

#include "ns3/channel.h"
#include "ns3/data-rate.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"

#include <list>

namespace ns3
{

class Packet;

namespace docsis
{

class DocsisNetDevice;

/**
 * \ingroup docsis
 * \brief DOCSIS channel object
 *
 * This class is used to connect two DocsisNetDevice instances.  In the
 * current ns-3 abstraction, one CmNetDevice is connected to one
 * CmtsNetDevice.  The channel model does not support multi-access
 * of multiple upstream devices; only one upstream is permitted.  It is
 * similar to the ns-3 PointToPointChannel model.
 *
 * The channel object is responsible for scheduling received packets
 * for arrival on the far-side NetDevice, based on transmission time
 * (the serialization delay, based on the data rate of the link--
 * this is passed in by the NetDevice), and the propagation delay (configured
 * as an attribute of this object).
 */
class DocsisChannel : public Channel
{
  public:
    /**
     * \brief Get the TypeId
     *
     * \return The TypeId for this class
     */
    static TypeId GetTypeId();

    /**
     * \brief Create a DocsisChannel
     */
    DocsisChannel();

    /**
     * \brief Attach a given DocsisNetDevice to this channel
     * \param device pointer to the DocsisNetDevice to attach to the channel
     */
    void Attach(Ptr<DocsisNetDevice> device);

    /**
     * \brief Transmit a packet over this channel
     * \param p Packet to transmit
     * \param src Source DocsisNetDevice
     * \param txTime Transmit time to apply
     * \returns true if successful (currently always true)
     */
    virtual bool TransmitStart(Ptr<Packet> p, Ptr<DocsisNetDevice> src, Time txTime);

    /**
     * \brief Get number of devices on this channel
     * \returns number of devices on this channel
     */
    std::size_t GetNDevices() const override;

    /**
     * \brief Get DocsisNetDevice corresponding to index i on this channel
     * \param i Index number of the device requested
     * \returns Ptr to DocsisNetDevice requested
     */
    Ptr<DocsisNetDevice> GetDocsisDevice(std::size_t i) const;

    /**
     * \brief Get NetDevice corresponding to index i on this channel
     * \param i Index number of the device requested
     * \returns Ptr to NetDevice requested
     */
    Ptr<NetDevice> GetDevice(std::size_t i) const override;

  protected:
    /**
     * Perform any object release functionality required to break reference
     * cycles in reference counted objects held by the device.
     */
    void DoDispose() override;

    /**
     * \brief Get the delay associated with this channel
     * \returns Time delay
     */
    Time GetDelay() const;

    /**
     * \brief Check to make sure the link is initialized
     * \returns true if initialized, asserts otherwise
     */
    bool IsInitialized() const;

    /**
     * \brief Get the net-device source
     * \param i the link requested
     * \returns Ptr to DocsisNetDevice source for the
     * specified link
     */
    Ptr<DocsisNetDevice> GetSource(uint32_t i) const;

    /**
     * \brief Get the net-device destination
     * \param i the link requested
     * \returns Ptr to DocsisNetDevice destination for
     * the specified link
     */
    Ptr<DocsisNetDevice> GetDestination(uint32_t i) const;

    /**
     * TracedCallback signature for packet transmission animation events.
     *
     * \param [in] packet The packet being transmitted.
     * \param [in] txDevice the transmitting NetDevice.
     * \param [in] rxDevice the receiving NetDevice.
     * \param [in] duration The amount of time to transmit the packet.
     * \param [in] lastBitTime Last bit receive time (relative to now)
     */
    typedef void (*TxRxDocsisCallback)(Ptr<Packet> packet,
                                       Ptr<const NetDevice> txDevice,
                                       Ptr<const NetDevice> rxDevice,
                                       Time duration,
                                       Time lastBitTime);

  private:
    /** Each point to point link has exactly two net devices. */
    static const int N_DEVICES = 2;

    Time m_delay;       //!< Propagation delay
    int32_t m_nDevices; //!< Devices of this channel

    /**
     * The trace source for the packet transmission animation events that the
     * device can fire.
     * Arguments to the callback are the packet, transmitting
     * net device, receiving net device, transmission time and
     * packet receipt time.
     *
     * Ptr<Packet> is non-const to allow for removal of tags
     *
     * \see class CallBackTraceSource
     */
    TracedCallback<Ptr<Packet>,          // Packet being transmitted
                   Ptr<const NetDevice>, // Transmitting NetDevice
                   Ptr<const NetDevice>, // Receiving NetDevice
                   Time,                 // Amount of time to transmit the pkt
                   Time                  // Last bit receive time (relative to now)
                   >
        m_txrxDocsis;

    /**
     * \brief Wire model for the DocsisChannel
     */
    class Link
    {
      public:
        /** \brief Create the link, it will be in INITIALIZING state
         *
         */
        Link()
            : m_src(nullptr),
              m_dst(nullptr)
        {
        }

        Ptr<DocsisNetDevice> m_src; //!< First NetDevice
        Ptr<DocsisNetDevice> m_dst; //!< Second NetDevice
    };

    Link m_link[N_DEVICES]; //!< Link model
};

} // namespace docsis
} // namespace ns3

#endif /* DOCSIS_CHANNEL_H */
