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
 *   Greg White <g.white@cablelabs.com>
 *   Karthik Sundaresan <k.sundaresan@cablelabs.com>
 */

#include "docsis-configuration.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/object.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DocsisConfiguration");

namespace docsis
{

NS_OBJECT_ENSURE_REGISTERED(AggregateServiceFlow);
NS_OBJECT_ENSURE_REGISTERED(ServiceFlow);

TypeId
ServiceFlow::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::docsis::ServiceFlow").SetParent<Object>().SetGroupName("Docsis");
    return tid;
}

ServiceFlow::ServiceFlow(uint8_t sfid)
    : m_sfid(sfid)
{
    NS_ABORT_MSG_IF(sfid == 0 || sfid > 2, "Only values of 1 and 2 supported");
}

ServiceFlow::~ServiceFlow()
{
    NS_LOG_FUNCTION(this);
}

TypeId
AggregateServiceFlow::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::docsis::AggregateServiceFlow").SetParent<Object>().SetGroupName("Docsis");
    return tid;
}

AggregateServiceFlow::AggregateServiceFlow()
{
}

AggregateServiceFlow::~AggregateServiceFlow()
{
    NS_LOG_FUNCTION(this);
}

uint32_t
AggregateServiceFlow::GetNumServiceFlows() const
{
    if (m_classicServiceFlow && m_lowLatencyServiceFlow)
    {
        return 2;
    }
    else if (m_classicServiceFlow || m_lowLatencyServiceFlow)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void
AggregateServiceFlow::SetClassicServiceFlow(Ptr<ServiceFlow> sf)
{
    m_classicServiceFlow = sf;
}

void
AggregateServiceFlow::SetLowLatencyServiceFlow(Ptr<ServiceFlow> sf)
{
    m_lowLatencyServiceFlow = sf;
}

Ptr<const ServiceFlow>
AggregateServiceFlow::GetClassicServiceFlow() const
{
    return m_classicServiceFlow;
}

Ptr<const ServiceFlow>
AggregateServiceFlow::GetLowLatencyServiceFlow() const
{
    return m_lowLatencyServiceFlow;
}

} // namespace docsis
} // namespace ns3
