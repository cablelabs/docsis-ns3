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
 */

//
// This program is used for experimenting with delay estimation
//
// Program Options:
// --simulationEndTime:        Time to end the simulation [+6.1e+10ns]
// --guaranteedGrantRate:      Guaranteed grant rate for low latency flow [0bps]
// --guaranteedGrantInterval:  Guaranteed grant interval for low latency flow [0]
// --estimator:                Low Latency upstream estimator to use [QDelayCoupledL]
// --enableAscii:              Enable ASCII trace [false]
// --llRate:                   Data rate of the LL CBR flow [5000000bps]
// --classicRate:              Data rate of the classic CBR flow [5000000bps]
// --enableDctcp:              Enable DCTCP flow [false]
// --upstreamAmsr:             Upstream AMSR (data rate) [10000000bps]
// --pduSize:                  Data PDU size (bytes) [1000]
//
// Output files:
//   delay-estimation.cl.sojourn.dat
//   delay-estimation.ll.estimate.dat
//   delay-estimation.ll.sojourn.dat
//   delay-estimation.ll.bytes.dat
//   delay-estimation.align.vq.dat
//
// Topology:
//              ------  --------
// node 'n0'----| CM |--| CMTS |---- node 'n3'
//              ------  --------
//               'n1'     'n2'
//
// >--- Direction of traffic flow --->
//

#include "ns3/applications-module.h"
#include "ns3/bridge-module.h"
#include "ns3/core-module.h"
#include "ns3/docsis-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <iomanip>
#include <iostream>

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE("DelayEstimation");

// Declaration of variables used below in trace sinks
std::ofstream g_clSojournStream;
std::ofstream g_llSojournStream;
std::ofstream g_llDelayStream;
std::ofstream g_llDequeueStream;
std::ofstream g_alignVqStream;
std::ofstream g_llBytesStream;
std::ofstream g_llRequestStream;
std::ofstream g_mapStream;
std::ofstream g_cwndStream;
std::ofstream g_rttStream;
std::ofstream g_throughputStream;
std::vector<double> g_sojournValues;
std::vector<double> g_estimateValues;
std::vector<double> g_classicDelayValues;
uint64_t g_markCount;
uint64_t g_macPduBytes;
uint64_t g_macPduBytesInterval;
Time g_startTraceTime;
Time g_startTcpThroughputTime;
Time g_lastTcpThroughputTime;
Time g_tcpThroughputInterval;
uint32_t g_lastAllowedLen;
uint32_t g_lastAvgLen;

// Forward declarations of some trace sinks and scheduled methods
void PrintValues(std::string prefix, std::vector<double> values);
void ConnectTcpTraces();
void EnableCongestion(Ptr<CmtsUpstreamScheduler> scheduler,
                      DataRate freeCapacityMean,
                      double freeCapacityVariation);
void DisableCongestion(Ptr<CmtsUpstreamScheduler> scheduler);
void TraceTcpTx(const Ptr<const Packet> packet,
                const TcpHeader& header,
                const Ptr<const TcpSocketBase> socket);
void TraceTcpCwnd(uint32_t oldValue, uint32_t newValue);
void TraceTcpRtt(Time oldValue, Time newValue);
void TraceAlignVq(uint32_t actualLen, Time vq, uint32_t avgLen, uint32_t allowedLen, bool aligned);
void TraceLowLatencyDequeue(Time sojourn,
                            Time estimate,
                            Time classicDelay,
                            bool isMarked,
                            uint32_t size);
void TraceClassicSojourn(Time sojourn);
void TraceLowLatencySojourn(Time sojourn);
void TraceLowLatencyQueueDelay(Time qDelay, uint32_t size);
void TraceLowLatencyBytes(uint32_t oldValue, uint32_t newValue);
void TraceMapMessage(CmtsUpstreamScheduler::MapReport report);
void TraceLowLatencyRequest(uint32_t request);

int
main(int argc, char* argv[])
{
    // Suggested non-LLD configuration has been provided by Greg White

    // Variables that may be changed through command-line arguments
    Time simulationEndTime = Seconds(61);
    DataRate guaranteedGrantRate(DataRate(0)); // may be overridden at cmd line
    uint16_t guaranteedGrantInterval(0);       // may be overridden at cmd line
    std::string estimator = "QDelayCoupledL";
    bool enableAscii = false;
    uint32_t schedulingWeight = 230;
    DataRate llRate("5Mbps");
    DataRate classicRate("5Mbps");
    DataRate burstRate;  // Default of zero
    Time burstStartTime; // Default of zero
    Time burstDuration;  // Default of zero
    bool enableDctcp = false;
    DataRate freeCapacityMean(0);
    double freeCapacityVariation = 0;
    DataRate upstreamAmsr("10Mbps"); // may be overridden at cmd line
    DataRate upstreamPeakRate;       // will default to AMSR unless overridden at cmdline
    Time upstreamMaxBurstTime = MilliSeconds(100); // Used to set Maximum Traffic Burst
    uint32_t pduSize = 1000;                       // data PDU size; includes Ethernet framing

    // Other variables (not configurable by command-line argument)
    Time appStartTime = Seconds(1);   // For CBR apps
    g_startTraceTime = Seconds(0.95); // Start tracing data after brief warmup period
    g_startTcpThroughputTime =
        Seconds(11); // Start tracking TCP throughput during steady state (after warmup)
    g_lastTcpThroughputTime = Seconds(0);        // Initialize to start of simulation
    g_tcpThroughputInterval = MilliSeconds(200); // For computing average send throughput
    g_markCount = 0;
    g_macPduBytes = 0;
    g_macPduBytesInterval = 0;
    // Make changes to FreeCapacityMean and FreeCapacityVariation at these times
    Time enableCongestionTime = Seconds(15);
    Time disableCongestionTime = Seconds(25);
    DataRate downstreamAmsr("200Mbps");
    DataRate downstreamPeakRate(2 * downstreamAmsr.GetBitRate());
    Time downstreamMaxBurstTime = MilliSeconds(100); // Used to set Maximum Traffic Burst
    Time queueDepthTime(MilliSeconds(250));          // target max. latency for C-queue

    // Defaults set here can be updated below by command-line argument processing
    Config::SetDefault("ns3::docsis::DocsisNetDevice::MapInterval", TimeValue(MilliSeconds(1)));
    Config::SetDefault("ns3::docsis::QueueProtection::QProtectOn", BooleanValue(false));
    // By default ns-3 DCTCP uses ECT(0) instead of ECT(1)
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpDctcp"));
    Config::SetDefault("ns3::TcpDctcp::UseEct0", BooleanValue(false));
    Config::SetDefault("ns3::TcpSocketState::EnablePacing", BooleanValue(true));
    // Disable PRR (not interacting well with DCTCP)
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TcpClassicRecovery::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(930));
    // TCP configuration -- increase default buffer sizes to improve throughput
    // over long delay paths
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(2048000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(2048000));

    // Parse command-line arguments
    CommandLine cmd;
    cmd.AddValue("simulationEndTime", "Time to end the simulation", simulationEndTime);
    cmd.AddValue("guaranteedGrantRate",
                 "Guaranteed grant rate for low latency flow",
                 guaranteedGrantRate);
    cmd.AddValue("guaranteedGrantInterval",
                 "Guaranteed grant interval for low latency flow",
                 guaranteedGrantInterval);
    cmd.AddValue("estimator", "Low Latency estimator to use", estimator);
    cmd.AddValue("enableAscii", "Enable ASCII trace", enableAscii);
    cmd.AddValue("schedulingWeight", "Scheduling weight", schedulingWeight);
    cmd.AddValue("llRate", "Data rate of the LL CBR flow", llRate);
    cmd.AddValue("classicRate", "Data rate of the classic CBR flow", classicRate);
    cmd.AddValue("burstRate", "Data rate of the transient burst LL CBR flow", burstRate);
    cmd.AddValue("burstStartTime", "Start time of the transient burst LL CBR flow", burstStartTime);
    cmd.AddValue("burstDuration", "Duration of the transient burst LL CBR flow", burstDuration);
    cmd.AddValue("enableDctcp", "Enable a DCTCP flow", enableDctcp);
    cmd.AddValue("freeCapacityMean",
                 "apply transient congestion (mean data rate)",
                 freeCapacityMean);
    cmd.AddValue("freeCapacityVariation",
                 "apply transient congestion (percentage variation)",
                 freeCapacityVariation);
    cmd.AddValue("upstreamAmsr", "Upstream AMSR (data rate)", upstreamAmsr);
    cmd.AddValue("upstreamPeakRate", "Upstream Peak (data rate)", upstreamPeakRate);
    cmd.AddValue("upstreamMaxBurst", "Duration of upstream max burst", upstreamMaxBurstTime);
    cmd.AddValue("pduSize", "Data PDU size (bytes)", pduSize);
    cmd.Parse(argc, argv);

    // update attributes that may be affected by command line arguments
    Config::SetDefault("ns3::docsis::DualQueueCoupledAqm::LlEstimator", StringValue(estimator));

    if (upstreamPeakRate.GetBitRate() == 0)
    {
        // value was not configured at command line, so set equal to AMSR
        upstreamPeakRate = upstreamAmsr;
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
    NetDeviceContainer n2n1Device;
    docsis.SetCmAttribute("DataRate", StringValue("100Mbps"));
    docsis.SetCmtsAttribute("DataRate", StringValue("200Mbps"));
    docsis.SetChannelAttribute("Delay", TimeValue(MicroSeconds(40)));

    NS_LOG_DEBUG("Adding upstream aggregate service flow");
    Ptr<AggregateServiceFlow> upstreamAsf = CreateObject<AggregateServiceFlow>();
    upstreamAsf->m_maxSustainedRate = upstreamAmsr;
    upstreamAsf->m_peakRate = upstreamPeakRate;
    upstreamAsf->m_maxTrafficBurst =
        static_cast<uint32_t>(upstreamAmsr.GetBitRate() * upstreamMaxBurstTime.GetSeconds() / 8);
    upstreamAsf->m_schedulingWeight = schedulingWeight;
    Ptr<ServiceFlow> sf1 = CreateObject<ServiceFlow>(CLASSIC_SFID);
    sf1->m_targetBuffer =
        static_cast<uint32_t>(upstreamAmsr.GetBitRate() * queueDepthTime.GetSeconds() / 8);
    upstreamAsf->SetClassicServiceFlow(sf1);
    Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
    if (guaranteedGrantRate.GetBitRate() != 0)
    {
        sf2->m_guaranteedGrantRate = guaranteedGrantRate;
        sf2->m_guaranteedGrantInterval = guaranteedGrantInterval;
    }
    upstreamAsf->SetLowLatencyServiceFlow(sf2);
    NS_LOG_DEBUG("Adding downstream aggregate service flow");
    Ptr<AggregateServiceFlow> downstreamAsf = CreateObject<AggregateServiceFlow>();
    downstreamAsf->m_maxSustainedRate = downstreamAmsr;
    downstreamAsf->m_peakRate = downstreamPeakRate;
    downstreamAsf->m_maxTrafficBurst = static_cast<uint32_t>(
        downstreamAmsr.GetBitRate() * downstreamMaxBurstTime.GetSeconds() / 8);
    downstreamAsf->m_schedulingWeight = schedulingWeight;
    sf1 = CreateObject<ServiceFlow>(CLASSIC_SFID);
    sf1->m_targetBuffer =
        static_cast<uint32_t>(downstreamAmsr.GetBitRate() * queueDepthTime.GetSeconds() / 8);
    downstreamAsf->SetClassicServiceFlow(sf1);
    sf2 = CreateObject<ServiceFlow>(LOW_LATENCY_SFID);
    downstreamAsf->SetLowLatencyServiceFlow(sf2);

    n2n1Device = docsis.Install(n2, n1, upstreamAsf, downstreamAsf);

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

    // Add static ARP entries on endpoints to avoid the initial delay of
    // address resolution
    Ptr<SimpleNetDevice> device0 = endpointDevices.Get(0)->GetObject<SimpleNetDevice>();
    Ptr<SimpleNetDevice> device3 = endpointDevices.Get(1)->GetObject<SimpleNetDevice>();
    Mac48Address address0 = Mac48Address::ConvertFrom(device0->GetAddress());
    Mac48Address address3 = Mac48Address::ConvertFrom(device3->GetAddress());
    Ptr<Ipv4L3Protocol> ip0 = n0->GetObject<Ipv4L3Protocol>();
    int32_t interface0 = ip0->GetInterfaceForDevice(device0);
    Ptr<Ipv4Interface> ipv4Interface0 = ip0->GetInterface(interface0);
    Ptr<ArpCache> cache0 = ipv4Interface0->GetArpCache();
    ArpCache::Entry* entry0 = cache0->Add(Ipv4Address("10.1.1.2"));
    entry0->SetMacAddress(address3);
    entry0->MarkPermanent();
    Ptr<Ipv4L3Protocol> ip3 = n3->GetObject<Ipv4L3Protocol>();
    int32_t interface3 = ip3->GetInterfaceForDevice(device3);
    Ptr<Ipv4Interface> ipv4Interface3 = ip3->GetInterface(interface3);
    Ptr<ArpCache> cache3 = ipv4Interface3->GetArpCache();
    ArpCache::Entry* entry3 = cache3->Add(Ipv4Address("10.1.1.1"));
    entry3->SetMacAddress(address0);
    entry3->MarkPermanent();

    // Bridge the devices together at the CMTS and CM
    BridgeHelper bridge;
    NetDeviceContainer cmBridgeDevices;
    cmBridgeDevices.Add(n0n1Device.Get(1));
    cmBridgeDevices.Add(n2n1Device.Get(1));
    bridge.Install(n1, cmBridgeDevices);
    NetDeviceContainer cmtsBridgeDevices;
    cmtsBridgeDevices.Add(n2n3Device.Get(0));
    cmtsBridgeDevices.Add(n2n1Device.Get(0));
    bridge.Install(n2, cmtsBridgeDevices);

    // Simple global static routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Stop the application one second before simulation end time
    Time appDuration = simulationEndTime - appStartTime + TimeStep(1);

    uint16_t sourceLlPort = 5000;
    uint16_t sourceClPort = 6000;
    uint16_t destLlPort = 10;
    uint16_t destClPort = 11;

    if (llRate.GetBitRate() > 0)
    {
        docsis.AddPacketBurstDuration(n0,
                                      n3,
                                      sourceLlPort,
                                      destLlPort,
                                      llRate,
                                      pduSize,
                                      appStartTime,
                                      appDuration,
                                      Ipv4Header::DscpType::DscpDefault,
                                      Ipv4Header::EcnType::ECN_ECT1);
    }

    if (burstRate.GetBitRate() > 0)
    {
        uint16_t sourceBurstPort = 7000;
        uint16_t destBurstPort = 12;
        docsis.AddPacketBurstDuration(n0,
                                      n3,
                                      sourceBurstPort,
                                      destBurstPort,
                                      burstRate,
                                      pduSize,
                                      burstStartTime,
                                      burstDuration,
                                      Ipv4Header::DscpType::DscpDefault,
                                      Ipv4Header::EcnType::ECN_ECT1);
    }

    if (classicRate.GetBitRate() > 0)
    {
        docsis.AddPacketBurstDuration(n0,
                                      n3,
                                      sourceClPort,
                                      destClPort,
                                      classicRate,
                                      pduSize,
                                      appStartTime,
                                      appDuration,
                                      Ipv4Header::DscpType::DscpDefault,
                                      Ipv4Header::EcnType::ECN_NotECT);
    }

    if (enableDctcp)
    {
        docsis.AddFtpSession(n0,
                             n3,
                             8000,
                             Seconds(5),
                             simulationEndTime,
                             "ns3::TcpSocketFactory",
                             "unlimited");
    }

    Ptr<DualQueueCoupledAqm> upstreamQueue = docsis.GetUpstream(n2n1Device)->GetQueue();

    g_clSojournStream.open("delay-estimation.cl.sojourn.dat");
    upstreamQueue->TraceConnectWithoutContext("ClassicSojournTime",
                                              MakeCallback(&TraceClassicSojourn));

    g_llSojournStream.open("delay-estimation.ll.sojourn.dat");
    upstreamQueue->TraceConnectWithoutContext("LowLatencySojournTime",
                                              MakeCallback(&TraceLowLatencySojourn));

    g_llDelayStream.open("delay-estimation.ll.estimate.dat");
    upstreamQueue->TraceConnectWithoutContext("LowLatencyQueueDelay",
                                              MakeCallback(&TraceLowLatencyQueueDelay));

    g_llDequeueStream.open("delay-estimation.dequeue.dat");
    upstreamQueue->TraceConnectWithoutContext("LowLatencyDequeue",
                                              MakeCallback(&TraceLowLatencyDequeue));

    g_alignVqStream.open("delay-estimation.align.vq.dat");
    upstreamQueue->TraceConnectWithoutContext("AlignVq", MakeCallback(&TraceAlignVq));

    g_llBytesStream.open("delay-estimation.ll.bytes.dat");
    upstreamQueue->TraceConnectWithoutContext("LowLatencyBytes",
                                              MakeCallback(&TraceLowLatencyBytes));

    Ptr<CmNetDevice> cmNetDevice = docsis.GetUpstream(n2n1Device)->GetObject<CmNetDevice>();

    g_llRequestStream.open("delay-estimation.ll.request.dat");
    cmNetDevice->TraceConnectWithoutContext("LowLatencyRequest",
                                            MakeCallback(&TraceLowLatencyRequest));

    Ptr<CmtsUpstreamScheduler> scheduler =
        docsis.GetUpstream(n2n1Device)->GetCmtsUpstreamScheduler();
    g_mapStream.open("delay-estimation.map.dat");
    scheduler->TraceConnectWithoutContext("MapTrace", MakeCallback(&TraceMapMessage));

    // tracing of link from n1 to n2
    if (enableAscii)
    {
        docsis.EnableAscii("delay-estimation.tr", n2n1Device.Get(0), true);
    }

    if (enableDctcp)
    {
        g_cwndStream.open("delay-estimation.tcp.cwnd.dat");
        g_rttStream.open("delay-estimation.tcp.rtt.dat");
        g_throughputStream.open("delay-estimation.tcp.throughput.dat");
        Simulator::Schedule(Seconds(5.1), &ConnectTcpTraces);
    }
    // Schedule congestion to start and stop
    Simulator::Schedule(enableCongestionTime,
                        &EnableCongestion,
                        scheduler,
                        freeCapacityMean,
                        freeCapacityVariation);
    Simulator::Schedule(disableCongestionTime, &DisableCongestion, scheduler);

    Simulator::Stop(simulationEndTime);
    Simulator::Run();

    g_clSojournStream.close();
    g_llSojournStream.close();
    g_llDelayStream.close();
    g_llDequeueStream.close();
    g_alignVqStream.close();
    g_llBytesStream.close();
    g_llRequestStream.close();
    g_mapStream.close();
    if (enableDctcp)
    {
        g_cwndStream.close();
        g_rttStream.close();
        g_throughputStream.close();
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "packets: " << g_sojournValues.size() << " ";
    std::cout << "marked: " << g_markCount << " ";
    std::cout << "(" << g_markCount * 100.0 / g_sojournValues.size() << "%)" << std::endl;
    std::cout << "time(ms)       :     min    50th    90th    99th     max" << std::endl;
    PrintValues("sojourn        :", g_sojournValues);
    PrintValues("qDelayCoupledL/V :", g_estimateValues);
    PrintValues("qDelayCoupledC :", g_classicDelayValues);
    if (enableDctcp)
    {
        std::cout << "DCTCP throughput "
                  << g_macPduBytes * 8 /
                         (1000 * 1000 * (Simulator::Now() - g_startTcpThroughputTime).GetSeconds())
                  << " Mbps" << std::endl;
    }

    // Export some configuration details
    std::cout << "# Summary: actualMap(us) framesPerMap minislotCapacity minislotsPerMap "
                 "minReqGntDelay(frames) avgAq-allowedAq(ms) tput(Mbps) P99pctLat(ms)"
              << std::endl;
    std::cout << "# throughput and latency only for DCTCP scenarios" << std::endl;
    std::cout << " " << cmNetDevice->GetActualMapInterval().GetSeconds() * 1000 * 1000 << " ";
    std::cout << cmNetDevice->GetFramesPerMap() << " ";
    std::cout << cmNetDevice->GetMinislotCapacity() << " ";
    std::cout << cmNetDevice->GetMinislotsPerMap() << " ";
    std::cout << cmNetDevice->GetMinReqGntDelay() << " ";
    int32_t difference =
        static_cast<int32_t>(g_lastAvgLen) - static_cast<int32_t>(g_lastAllowedLen);
    std::cout << std::setw(7)
              << static_cast<double>(difference * 8 * 1000) / upstreamAmsr.GetBitRate();
    if (enableDctcp)
    {
        std::cout << " " << std::fixed << std::setprecision(3) << std::setw(7)
                  << g_macPduBytes * 8 /
                         (1000 * 1000 * (Simulator::Now() - g_startTcpThroughputTime).GetSeconds())
                  << " ";
        std::sort(g_sojournValues.begin(), g_sojournValues.end());
        auto n = g_sojournValues.size();
        std::cout << std::setw(7) << g_sojournValues[std::ceil((n - 1) * 0.99)];
    }
    std::cout << std::endl;

    Simulator::Destroy();
    return 0;
}

void
PrintValues(std::string prefix, std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    auto it = values.begin();
    auto n = values.size();
    if (n)
    {
        std::cout << std::setprecision(3);
        std::cout << prefix;
        std::cout << std::setw(8) << *it;
        std::cout << std::setw(8) << values[std::ceil((n - 1) * 0.5)];
        std::cout << std::setw(8) << values[std::ceil((n - 1) * 0.9)];
        std::cout << std::setw(8) << values[std::ceil((n - 1) * 0.99)];
        auto rit = values.rbegin();
        std::cout << std::setw(8) << *rit;
        std::cout << std::endl;
    }
}

// TCP connections do not exist at simulation start time, so they must be
// hooked during runtime
void
ConnectTcpTraces()
{
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::Node/$ns3::TcpL4Protocol/SocketList/0/$ns3::TcpSocketBase/Tx",
        MakeCallback(&TraceTcpTx));
    Config::ConnectWithoutContext("/NodeList/0/$ns3::Node/$ns3::TcpL4Protocol/SocketList/0/"
                                  "$ns3::TcpSocketBase/CongestionWindow",
                                  MakeCallback(&TraceTcpCwnd));
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::Node/$ns3::TcpL4Protocol/SocketList/0/$ns3::TcpSocketBase/RTT",
        MakeCallback(&TraceTcpRtt));
}

void
TraceTcpTx(const Ptr<const Packet> packet,
           const TcpHeader& header,
           const Ptr<const TcpSocketBase> socket)
{
    if (Simulator::Now() >= g_startTcpThroughputTime)
    {
        // Add header bytes so that throughput is measured at MAC PDU level
        // TCP payload + 32 bytes header + 20 bytes IP header + 18 bytes Ethernet
        g_macPduBytes += (packet->GetSize() + 32 + 20 + 18);
    }
    g_macPduBytesInterval += (packet->GetSize() + 32 + 20 + 18);
    if (Simulator::Now() > g_lastTcpThroughputTime + g_tcpThroughputInterval)
    {
        g_throughputStream << Simulator::Now().GetSeconds() << " "
                           << g_macPduBytesInterval * 8 /
                                  (1000 * 1000 *
                                   (Simulator::Now() - g_lastTcpThroughputTime).GetSeconds())
                           << std::endl;
        g_macPduBytesInterval = 0;
        g_lastTcpThroughputTime = Simulator::Now();
    }
}

void
TraceTcpCwnd(uint32_t oldValue, uint32_t newValue)
{
    g_cwndStream << Simulator::Now().GetSeconds() << " " << newValue << std::endl;
}

void
TraceTcpRtt(Time oldValue, Time newValue)
{
    g_rttStream << Simulator::Now().GetSeconds() << " " << newValue.GetSeconds() * 1000
                << std::endl;
}

void
TraceAlignVq(uint32_t actualLen, Time vq, uint32_t avgLen, uint32_t allowedLen, bool aligned)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_alignVqStream << Simulator::Now().GetSeconds() << " " << actualLen << " "
                    << vq.GetNanoSeconds() << " " << avgLen << " " << allowedLen << " " << aligned
                    << std::endl;
    g_lastAvgLen = avgLen;
    g_lastAllowedLen = allowedLen;
}

void
TraceLowLatencyDequeue(Time sojourn, Time estimate, Time classicDelay, bool isMarked, uint32_t size)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_sojournValues.push_back(sojourn.GetSeconds() * 1000);
    g_estimateValues.push_back(estimate.GetSeconds() * 1000);
    g_classicDelayValues.push_back(classicDelay.GetSeconds() * 1000);
    if (isMarked)
    {
        g_markCount++;
    }
    g_llDequeueStream << Simulator::Now().GetSeconds() << " " << sojourn.GetSeconds() * 1000 << " "
                      << estimate.GetSeconds() * 1000 << " " << classicDelay.GetSeconds() * 1000
                      << " " << isMarked << " " << size << std::endl;
}

void
TraceClassicSojourn(Time sojourn)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_clSojournStream << Simulator::Now().GetSeconds() << " " << sojourn.GetSeconds() * 1000
                      << std::endl;
}

void
TraceLowLatencySojourn(Time sojourn)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_llSojournStream << Simulator::Now().GetSeconds() << " " << sojourn.GetSeconds() * 1000
                      << std::endl;
}

void
TraceLowLatencyQueueDelay(Time qDelay, uint32_t size)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_llDelayStream << Simulator::Now().GetSeconds() << " " << qDelay.GetSeconds() * 1000 << " "
                    << size << std::endl;
}

void
TraceLowLatencyBytes(uint32_t oldValue, uint32_t newValue)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_llBytesStream << Now().GetSeconds() << " " << oldValue << " " << newValue << std::endl;
}

void
TraceMapMessage(CmtsUpstreamScheduler::MapReport report)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_mapStream << Now().GetSeconds() << " " << report.m_availableTokens << " "
                << report.m_cGrantedBytes << " " << report.m_pgsGrantedBytes << " "
                << report.m_lGrantedBytes << " "
                << report.m_lGrantedMinislots * report.m_minislotCapacity << std::endl;
}

void
TraceLowLatencyRequest(uint32_t request)
{
    if (Now() < g_startTraceTime)
    {
        return;
    }
    g_llRequestStream << Simulator::Now().GetSeconds() << " " << request << std::endl;
}

void
EnableCongestion(Ptr<CmtsUpstreamScheduler> scheduler,
                 DataRate freeCapacityMean,
                 double freeCapacityVariation)
{
    scheduler->SetAttribute("FreeCapacityMean", DataRateValue(freeCapacityMean));
    scheduler->SetAttribute("FreeCapacityVariation", DoubleValue(freeCapacityVariation));
}

void
DisableCongestion(Ptr<CmtsUpstreamScheduler> scheduler)
{
    scheduler->SetAttribute("FreeCapacityMean", DataRateValue(DataRate(0)));
    scheduler->SetAttribute("FreeCapacityVariation", DoubleValue(0));
}
