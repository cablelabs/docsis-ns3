/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#include "ns3/docsis-configuration.h"
#include "ns3/object.h"
#include "ns3/test.h"

namespace ns3
{

class Node;

namespace docsis
{

class CmNetDevice;
class CmtsNetDevice;
class DocsisChannel;

// Supporting class to set up a DOCSIS test network
//
//  n0 <-----> n1 <----> n2 <------> n3
//  10.1.1.1                        10.1.1.2
//             CM       CMTS
//  traffic                          traffic
//  endpoint                         endpoint
//  (LAN node)                       (WAN node)
//
class DocsisLinkTestCase : public TestCase
{
  public:
    DocsisLinkTestCase(std::string name);
    ~DocsisLinkTestCase() override;

    void TraceBytesInQueue(std::string context, uint32_t oldValue, uint32_t newValue);

  protected:
    virtual void SetupFourNodeTopology();
    void SetUpstreamRate(DataRate upstreamRate);
    Ptr<AggregateServiceFlow> GetUpstreamAsf() const;
    Ptr<AggregateServiceFlow> GetDownstreamAsf() const;

    Ptr<CmNetDevice> m_upstream;
    Ptr<CmtsNetDevice> m_downstream;
    Ptr<DocsisChannel> m_channel;
    Ptr<Node> m_cmtsNode;
    Ptr<Node> m_cmNode;
    Ptr<Node> m_lanNode;
    Ptr<Node> m_wanNode;
    uint32_t m_upstreamBytesInQueue;
    uint32_t m_downstreamBytesInQueue;
    DataRate m_upstreamRate;
    DataRate m_downstreamRate;

  private:
};

} // namespace docsis
} // namespace ns3
