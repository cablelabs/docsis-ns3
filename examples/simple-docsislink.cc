/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2012-2020 Cable Television Laboratories, Inc.
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
 *   Greg White <g.white@cablelabs.com>
 *   Joey Padden <j.padden@cablelabs.com>
 *   Takashi Hayakawa <t.hayakawa@cablelabs.com>
 *
 * Modified for ns-3 and LLD DOCSIS by Tom Henderson <tomh@tomh.org>
 */

//
// This is a port of ns-2's simple-docsislink.tcl.  This example is designed
// to illustrate DOCSIS link behavior when the CMTS scheduler is congested
// and not able to make consistent bandwidth grants at the specified Maximum
// Sustained Rate (MSR). The downstream direction is not subjected to
// congestion, and only upstream data flows are defined.
//
// In ns-2, the program uses only the DOCSIS 3.1 standard (single service
// flow), and outputs following when run with no arguments:
//
// upstream link mean capacity = 2000000.0
// CMTS downstream buffer 166
// upstream starts at max grant of 500.0
// change at 0 to 500.0 and mean capacity of 2000000.0
// change at 5 to 412.5 and mean capacity of 1650000.0
// change at 10 to 500.0 and mean capacity of 2000000.0
// change at 15 to 562.5 and mean capacity of 2250000.0
// change at 20 to 412.5 and mean capacity of 1650000.0
// change at 25 to 562.5 and mean capacity of 2250000.0
// change at 30 to 500.0 and mean capacity of 2000000.0
// change at 35 to 625.0 and mean capacity of 2500000.0
// change at 40 to 412.5 and mean capacity of 1650000.0
// change at 45 to 625.0 and mean capacity of 2500000.0
// change at 50 to 562.5 and mean capacity of 2250000.0
// change at 55 to 625.0 and mean capacity of 2500000.0
// change at 60 to 500.0 and mean capacity of 2000000.0
//
// n0   <-------> n1 <---------> n2 <---------> n3
//                CM            CMTS
//
// Data flow:  n0 ---> n3 (a 5 Mbps CBR flow and a FTP flow)
//
// The dynamic bandwidth link effects are meant to show the effects
// of notional upstream congestion, in which the CMTS is not able to
// always grant at the MSR of 2 Mbps.  Actual grants are the mean
// capacity for the time interval, +/- a random capacity variation
// up to 20% of MSR.
//

// For ns-3, we have the same topology and the same basic congestion
// abstraction, but the program is extended to allow users to experiment
// with a single service flow (DOCSIS 3.1 PIE AQM) or two service flow
// (DOCSIS 3.1 LLD) configuration.  The MSR/AMSR has been raised to 10 Mbps
// in the upstream direction, and the congestion scaled up by the same factor.
// The MAP interval is also reduced from 2ms to 1ms.
//
// IP addressing (note that the CMTS is a layer-2 bridge):
// n0-0: 10.1.1.1 (client:  UDP or FTP sender)
// n3-0: 10.1.1.2 (server:  UDP or FTP receiver)
//
// The program provides the following command-line arguments:
//    --simulationEndTime:      Time to end the simulation [+61e+09ns]
//    --numServiceFlows:        Number of service flows (1 or 2) [1]
//    --congestion:             toggle DOCSIS congestion model (0-3) [1]
//    --dynamicInterval:        time spent at each throughput rate in congestion model (sec)
//    [+5e+09ns]
//    --freeCapacityVariation:  Free capacity variation (percentage) [20]
//    --enableFtp:              Enable the FTP flow [true]
//    --udpRate:                Data rate of the UDP flow [5000000bps]
//    --packetErrorRate:        Packet error rate of upstream data [0]
//

#include "ns3/applications-module.h"
#include "ns3/bridge-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/docsis-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <iomanip>

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE("SimpleDocsislink");

std::ofstream g_grantStream;
std::ofstream g_sojournStream;

std::vector<DataRate>
GetUpstreamRates(uint16_t congestion)
{
    std::vector<DataRate> dataRateVector;
    switch (congestion)
    {
    case 1:
        dataRateVector.emplace_back("8.25Mbps");
        dataRateVector.emplace_back("10.00Mbps");
        dataRateVector.emplace_back("11.25Mbps");
        dataRateVector.emplace_back("12.50Mbps");
        break;
    case 2:
        dataRateVector.emplace_back("9.25Mbps");
        dataRateVector.emplace_back("9.50Mbps");
        dataRateVector.emplace_back("10.00Mbps");
        dataRateVector.emplace_back("11.25Mbps");
        break;
    case 3:
        dataRateVector.emplace_back("5.00Mbps");
        dataRateVector.emplace_back("6.00Mbps");
        dataRateVector.emplace_back("9.00Mbps");
        dataRateVector.emplace_back("10.00Mbps");
        break;
    default:
        NS_ABORT_MSG("Invalid dynamic BW value: " << congestion);
        break;
    }
    return dataRateVector;
}

void
ScheduleCapacityChanges(Ptr<CmNetDevice> device,
                        const std::vector<std::pair<Time, DataRate>>& changes)
{
    std::string attributeName = "FreeCapacityMean";
    if (changes.empty())
    {
        NS_LOG_DEBUG("No dynamic bandwidth; use FreeCapacityMean default");
        return;
    }
    for (uint32_t i = 0; i < changes.size(); i++)
    {
        NS_LOG_DEBUG("Scheduling " << changes[i].second << " at " << changes[i].first.As(Time::S));
        Simulator::Schedule(changes[i].first,
                            &CmtsUpstreamScheduler::SetAttribute,
                            device->GetCmtsUpstreamScheduler(),
                            attributeName,
                            DataRateValue(changes[i].second));
    }
}

void
MapTraceCallback(CmtsUpstreamScheduler::MapReport report)
{
    double mapDuration = report.m_mapInterval.GetSeconds();
    g_grantStream << Simulator::Now().GetSeconds() << " "
                  << report.m_freeCapacityMean.GetBitRate() / 1e6 << " "
                  << report.m_freeCapacityVariation << " "
                  << report.m_maxGrant * 8 / mapDuration / 1e6 << " "
                  << report.m_adjustedMaxGrant * 8 / mapDuration / 1e6 << " "
                  << report.m_cGrantedBytes * 8 / mapDuration / 1e6 << " " << report.m_cGrantedBytes
                  << std::endl;
}

void
TraceClassicSojourn(Time sojourn)
{
    g_sojournStream << Simulator::Now().GetSeconds() << " " << sojourn.GetSeconds() * 1000
                    << std::endl;
}

int
main(int argc, char* argv[])
{
    bool verbose = false;
    bool enableFtp = true;
    DataRate udpRate("5Mbps");
    double packetErrorRate = 0;
    Time simulationEndTime = Seconds(61.0);
    // 0 = no congestion, 1 = light congestion, 2 = moderate congestion, 3 = heavy congestion
    uint16_t congestion = 1;
    Time dynamicInterval = Seconds(5);
    double freeCapacityVariation = 20; // percent; was 'mgVar_' in ns-2
    uint16_t numServiceFlows = 1;      // number of service flows

    // Suggested non-LLD configuration provided by Greg White
    Time mapInterval = MilliSeconds(1);
    DataRate upstreamMsr("10Mbps");
    DataRate upstreamPeakRate("20Mbps");
    DataRate guaranteedGrantRate(DataRate(0));
    DataRate downstreamMsr("200Mbps");
    DataRate downstreamPeakRate("400Mbps");
    Time upstreamMaxBurstTime = MilliSeconds(100);   // Used to set Maximum Traffic Burst
    Time downstreamMaxBurstTime = MilliSeconds(100); // Used to set Maximum Traffic Burst
    Time queueDepthTime(MilliSeconds(250));          // target latency for C-queue

    // Defaults here may be changed further by command-line arguments
    Config::SetDefault("ns3::docsis::DocsisNetDevice::MapInterval", TimeValue(mapInterval));
    Config::SetDefault("ns3::docsis::QueueProtection::QProtectOn", BooleanValue(false));

    CommandLine cmd;
    cmd.AddValue("simulationEndTime", "Time to end the simulation", simulationEndTime);
    cmd.AddValue("numServiceFlows", "Number of service flows (1 or 2)", numServiceFlows);
    cmd.AddValue("congestion", "toggle DOCSIS congestion model (0-3)", congestion);
    cmd.AddValue("dynamicInterval",
                 "time spent at each throughput rate in congestion model (sec)",
                 dynamicInterval);
    cmd.AddValue("freeCapacityVariation",
                 "Free capacity variation (percentage)",
                 freeCapacityVariation);
    cmd.AddValue("enableFtp", "Enable the FTP flow", enableFtp);
    cmd.AddValue("udpRate", "Data rate of the UDP flow", udpRate);
    cmd.AddValue("packetErrorRate", "Packet error rate of upstream data", packetErrorRate);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::docsis::CmtsUpstreamScheduler::FreeCapacityVariation",
                       DoubleValue(freeCapacityVariation));
    Config::SetDefault("ns3::docsis::CmtsUpstreamScheduler::FreeCapacityMean",
                       DataRateValue(upstreamMsr));
    // Prepare the changes to FreeCapacityMean; they will be scheduled later
    std::vector<std::pair<Time, DataRate>> capacityChanges;
    if (congestion > 0)
    {
        std::vector<DataRate> usChanges;
        std::vector<DataRate> usRates = GetUpstreamRates(congestion);
        usChanges.push_back(usRates[1]);
        usChanges.push_back(usRates[0]);
        usChanges.push_back(usRates[1]);
        usChanges.push_back(usRates[2]);
        usChanges.push_back(usRates[0]);
        usChanges.push_back(usRates[2]);
        usChanges.push_back(usRates[1]);
        usChanges.push_back(usRates[3]);
        usChanges.push_back(usRates[0]);
        usChanges.push_back(usRates[3]);
        usChanges.push_back(usRates[2]);
        usChanges.push_back(usRates[3]);

        uint16_t k = 1;
        for (Time changeTime(Seconds(0)); changeTime <= simulationEndTime;
             changeTime += dynamicInterval)
        {
            DataRate newCapacity = usChanges[k - 1];
            std::cout << "change at " << changeTime.As(Time::S) << " to mean free capacity "
                      << static_cast<double>(newCapacity.GetBitRate()) / 1e6 << " Mbps"
                      << std::endl;
            // The ns-2 code here is '$ns at $changeTime ... set maxgrant_ $newMG'
            // The ns-3 API has changed to set free capacity directly rather
            // than maxgrant; we store these changes and wait until the device
            // is instantiated, and then schedule (see below)
            capacityChanges.emplace_back(changeTime, newCapacity);
            k = k % 12 + 1;
        }
    }

    // Topology creation:  create nodes
    NodeContainer nodes;
    nodes.Create(4);
    Ptr<Node> n0 = nodes.Get(0);
    Ptr<Node> n1 = nodes.Get(1);
    Ptr<Node> n2 = nodes.Get(2);
    Ptr<Node> n3 = nodes.Get(3);

    // There is no mobility, but the below will help in rendering the
    // nodes linearly on a canvas, if animated
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    positionAlloc->Add(Vector(10.0, 0.0, 0.0));
    positionAlloc->Add(Vector(20.0, 0.0, 0.0));
    positionAlloc->Add(Vector(30.0, 0.0, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    SimpleNetDeviceHelper simple;
    simple.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    simple.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
    NodeContainer n0n1container(n0);
    n0n1container.Add(n1);
    NodeContainer n2n3container(n2);
    n2n3container.Add(n3);
    NetDeviceContainer n0n1Device = simple.Install(n0n1container);
    NetDeviceContainer n2n3Device = simple.Install(n2n3container);
    DocsisHelper docsis;
    NetDeviceContainer linkDocsis;
    docsis.SetCmAttribute("DataRate", StringValue("100Mbps"));
    docsis.SetCmtsAttribute("DataRate", StringValue("200Mbps"));
    docsis.SetChannelAttribute("Delay", TimeValue(MicroSeconds(40)));

    // Add DOCSIS service flow definitions
    if (numServiceFlows == 1)
    {
        NS_LOG_DEBUG("Adding single upstream (classic) service flow");
        Ptr<ServiceFlow> upstreamSf = CreateObject<ServiceFlow>(CLASSIC_SFID);
        upstreamSf->m_maxSustainedRate = upstreamMsr;
        upstreamSf->m_peakRate = upstreamPeakRate;
        upstreamSf->m_maxTrafficBurst =
            static_cast<uint32_t>(upstreamMsr.GetBitRate() * upstreamMaxBurstTime.GetSeconds() / 8);
        upstreamSf->m_targetBuffer =
            static_cast<uint32_t>(upstreamMsr.GetBitRate() * queueDepthTime.GetSeconds() / 8);
        NS_LOG_DEBUG("Adding single downstream (classic) service flow");
        Ptr<ServiceFlow> downstreamSf = CreateObject<ServiceFlow>(CLASSIC_SFID);
        downstreamSf->m_maxSustainedRate = downstreamMsr;
        downstreamSf->m_peakRate = downstreamPeakRate;
        downstreamSf->m_maxTrafficBurst = static_cast<uint32_t>(
            downstreamMsr.GetBitRate() * downstreamMaxBurstTime.GetSeconds() / 8);
        downstreamSf->m_targetBuffer =
            static_cast<uint32_t>(downstreamMsr.GetBitRate() * queueDepthTime.GetSeconds() / 8);
        linkDocsis = docsis.Install(n2, n1, upstreamSf, downstreamSf);
    }
    else
    {
        NS_LOG_DEBUG("Adding upstream aggregate service flow");
        Ptr<AggregateServiceFlow> upstreamAsf = CreateObject<AggregateServiceFlow>();
        upstreamAsf->m_maxSustainedRate = upstreamMsr;
        upstreamAsf->m_peakRate = upstreamPeakRate;
        upstreamAsf->m_maxTrafficBurst =
            static_cast<uint32_t>(upstreamMsr.GetBitRate() * upstreamMaxBurstTime.GetSeconds() / 8);
        Ptr<ServiceFlow> sf1 = CreateObject<ServiceFlow>(CLASSIC_SFID);
        upstreamAsf->SetClassicServiceFlow(sf1);
        Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
        if (guaranteedGrantRate.GetBitRate() != 0)
        {
            sf2->m_guaranteedGrantRate = guaranteedGrantRate;
        }
        upstreamAsf->SetLowLatencyServiceFlow(sf2);
        NS_LOG_DEBUG("Adding downstream aggregate service flow");
        Ptr<AggregateServiceFlow> downstreamAsf = CreateObject<AggregateServiceFlow>();
        downstreamAsf->m_maxSustainedRate = downstreamMsr;
        downstreamAsf->m_peakRate = downstreamPeakRate;
        downstreamAsf->m_maxTrafficBurst = static_cast<uint32_t>(
            downstreamMsr.GetBitRate() * downstreamMaxBurstTime.GetSeconds() / 8);
        sf1 = CreateObject<ServiceFlow>(CLASSIC_SFID);
        downstreamAsf->SetClassicServiceFlow(sf1);
        sf2 = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
        downstreamAsf->SetLowLatencyServiceFlow(sf2);
        linkDocsis = docsis.Install(n2, n1, upstreamAsf, downstreamAsf);
    }

    // Add an IP stack to the nodes n0 and n3
    InternetStackHelper internet;
    internet.Install(nodes);

    // Nodes n1 and n2 are bridged nodes

    // Add IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.252");
    NetDeviceContainer endpointDevices;
    endpointDevices.Add(n0n1Device.Get(0));
    endpointDevices.Add(n2n3Device.Get(1));
    Ipv4InterfaceContainer endpointIp = ipv4.Assign(endpointDevices);

    BridgeHelper bridge;
    NetDeviceContainer cmBridgeDevices;
    cmBridgeDevices.Add(n0n1Device.Get(1));
    cmBridgeDevices.Add(linkDocsis.Get(1));
    bridge.Install(n1, cmBridgeDevices);
    NetDeviceContainer cmtsBridgeDevices;
    cmtsBridgeDevices.Add(n2n3Device.Get(0));
    cmtsBridgeDevices.Add(linkDocsis.Get(0));
    bridge.Install(n2, cmtsBridgeDevices);

    // Simple global static routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // The CBR application, sending from n0 to n3
    uint16_t port = 9; // Discard port (RFC 863)
    Ipv4Address serverIp = endpointIp.GetAddress(1);
    OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(serverIp, port)));
    // The ns-2 trace showed CBR packets of 1000 bytes each every 0.0016 sec
    // for a data rate of 5 Mb/s
    // To approximate that here, we account for IP and UDP headers, and
    // adjust the sending rate to 4.86 Mb/s so that link rate ends up at 5 Mb/s
    uint32_t packetSize = 1000 - 20 - 8; // account for Udp
    // 1000 bytes
    packetSize = 1000;
    onoff.SetConstantRate(udpRate, packetSize);
    ApplicationContainer sourceCbr = onoff.Install(n0);
    sourceCbr.Start(Seconds(1));
    sourceCbr.Stop(simulationEndTime - Seconds(1) + TimeStep(1));

    // Create a packet sink to receive these packets
    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
    ApplicationContainer sinkCbr = sink.Install(n3);
    sinkCbr.Start(Seconds(1));
    sinkCbr.Stop(simulationEndTime);

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1000));
    uint16_t ftpPort = 50000;
    BulkSendHelper ftp("ns3::TcpSocketFactory", Address());
    AddressValue remoteAddress(InetSocketAddress(serverIp, ftpPort));
    ftp.SetAttribute("Remote", remoteAddress);
    ftp.SetAttribute("SendSize", UintegerValue(1000));
    if (enableFtp)
    {
        ApplicationContainer sourceFtp = ftp.Install(n0);
        sourceFtp.Start(Seconds(2));
        sourceFtp.Stop(simulationEndTime - Seconds(1));
    }

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), ftpPort));
    sinkHelper.SetAttribute("Protocol", TypeIdValue(TcpSocketFactory::GetTypeId()));
    ApplicationContainer sinkFtp = sinkHelper.Install(n3);
    sinkFtp.Start(Seconds(2));
    sinkFtp.Stop(simulationEndTime);

    // Fix the random variable streams to specific stream indices
    docsis.AssignStreams(linkDocsis, 0);

    // Schedule the dynamic link changes.  We deferred this step until
    // now so that we could fetch the relevant device pointer
    ScheduleCapacityChanges(docsis.GetUpstream(linkDocsis), capacityChanges);

    // Create error model and add it to CmtsNetDevice to model upstream loss
    if (packetErrorRate > 0)
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(packetErrorRate));
        em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
        // Note:  we add the error model on what we call the downstream
        // device, but its effects are applied in the upstream direction
        docsis.GetDownstream(linkDocsis)->SetReceiveErrorModel(em);
    }

    g_grantStream.open("simple-docsislink.grants.dat");
    g_grantStream << "# time mean var grantRate adjGrantRate cGrantRate cGrantBytes " << std::endl;

    Ptr<CmtsUpstreamScheduler> scheduler =
        docsis.GetUpstream(linkDocsis)->GetCmtsUpstreamScheduler();
    scheduler->TraceConnectWithoutContext("MapTrace", MakeCallback(&MapTraceCallback));
    g_sojournStream.open("simple-docsislink.sojourn.dat");
    docsis.GetUpstream(linkDocsis)
        ->TraceConnectWithoutContext("ClassicSojournTime", MakeCallback(&TraceClassicSojourn));

    // tracing of link from n1 to n2
    docsis.EnableAscii("simple-docsislink.tr", linkDocsis.Get(0), true);

    // Install flow monitor objects for per-flow statistics
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    // Output config store to txt format
    Config::SetDefault("ns3::ConfigStore::Filename",
                       StringValue("simple-docsislink.attributes.txt"));
    Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("RawText"));
    Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
    ConfigStore outputConfig2;
    outputConfig2.ConfigureDefaults();
    outputConfig2.ConfigureAttributes();

    Simulator::Stop(simulationEndTime);
    Simulator::Run();

    if (verbose)
    {
        // Output docsis configuration
        docsis.PrintConfiguration(std::cout, linkDocsis);
        std::cout << std::endl;
        // Print out flow throughput and sojourn summaries
        docsis.PrintFlowSummaries(std::cout, flowHelper, flowMonitor);
    }
    // Write to file
    std::ofstream configFile;
    configFile.open("simple-docsislink.config.txt");
    docsis.PrintConfiguration(configFile, linkDocsis);
    configFile.close();
    std::ofstream flowFile;
    flowFile.open("simple-docsislink.flows.txt");
    docsis.PrintFlowSummaries(flowFile, flowHelper, flowMonitor);
    flowFile.close();
    g_grantStream.close();
    g_sojournStream.close();

    Simulator::Destroy();
    return 0;
}
