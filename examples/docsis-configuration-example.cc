/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 Cable Television Laboratories, Inc.
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
 */

/*
 * This program is used to experiment with DOCSIS 3.1 LLD configuration choices.
 * It accepts as input different values, and outputs several derived
 * values based on the inputs and default values.
 *
 * The helper method DocsisHelper::PrintConfiguration() can be used in
 * programs to output this information onto a standard output stream.
 *
 */
#include <iomanip>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/docsis-module.h"

using namespace ns3;
using namespace docsis;

NS_LOG_COMPONENT_DEFINE ("DocsisConfigurationExample");

int 
main (int argc, char *argv[])
{

  CommandLine cmd;
  cmd.Parse (argc,argv);

  Ptr<CmNetDevice> upstream = CreateObject<CmNetDevice> ();
  Ptr<CmtsNetDevice> downstream = CreateObject<CmtsNetDevice> ();
  Ptr<CmtsUpstreamScheduler> scheduler = CreateObject<CmtsUpstreamScheduler> ();
  Ptr<DocsisChannel> channel = CreateObject<DocsisChannel> ();
  upstream->SetCmtsUpstreamScheduler (scheduler);
  upstream->Attach (channel);
  downstream->Attach (channel);

  DoubleValue dVal;
  UintegerValue uVal;
  TimeValue tVal;
  DataRateValue rVal;
 
  std::streamsize oldPrecision = std::cout.precision ();

  std::cout << "Input parameters upstream" << std::endl;
  std::cout << "-----------------------" << std::endl;
  scheduler->GetAttribute ("FreeCapacityMean", rVal);
  std::cout << "FreeCapacityMean: Average upstream free capacity (bits/sec) = " << rVal.Get ().GetBitRate () << std::endl;
  scheduler->GetAttribute ("FreeCapacityVariation", dVal);
  std::cout << "FreeCapacityVariation: Bound (percent) on the variation of upstream free capacity = " << dVal.Get () << std::endl;
  upstream->GetAttribute ("BurstPreparation", tVal);
  std::cout << "The burst preparation time = " << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("UsScSpacing", dVal);
  std::cout << "UsScSpacing: Upstream subcarrier spacing = " << dVal.Get () << std::endl;
  upstream->GetAttribute ("NumUsSc", uVal);
  std::cout << "NumUsSc: Number of upstream subcarriers = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("SymbolsPerFrame", uVal);
  std::cout << "SymbolsPerFrame: Number of symbols per frame = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("UsSpectralEfficiency", dVal);
  std::cout << "UsSpectralEfficiency: Upstream spectral efficiency (bps/Hz) = " << dVal.Get () << std::endl; 
  upstream->GetAttribute ("UsCpLen", uVal);
  std::cout << "UsCpLen: Upstream cyclic prefix length = " << uVal.Get () << std::endl; 
  upstream->GetAttribute ("UsMacHdrSize", uVal);
  std::cout << "UsMacHdrSize: Upstream Mac header size = " << uVal.Get () << std::endl; 
  upstream->GetAttribute ("UsSegHdrSize", uVal);
  std::cout << "UsSegHdrSize: Upstream segment header size = " << uVal.Get () << std::endl; 
  upstream->GetAttribute ("MapInterval", tVal);
  std::cout << "MapInterval: MAP interval = " << tVal.Get ().As (Time::MS) << std::endl; 
  upstream->GetAttribute ("CmtsMapProcTime", tVal);
  std::cout << "CmtsMapProcTime: CMTS MAP processing time = " << tVal.Get ().As (Time::US) << std::endl; 
  upstream->GetAttribute ("CmUsPipelineFactor", uVal);
  std::cout << "CmUsPipelineFactor: upstream pipeline factor at CM = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("CmtsUsPipelineFactor", uVal);
  std::cout << "CmtsUsPipelineFactor: upstream pipeline factor at CMTS = " << uVal.Get () << std::endl;
  std::cout << std::endl;

  std::cout << "Input parameters downstream" << std::endl;
  std::cout << "-------------------------" << std::endl;
  // Downstream
  downstream->GetAttribute ("FreeCapacityMean", rVal);
  std::cout << "FreeCapacityMean: Average upstream free capacity (bits/sec) = " << rVal.Get ().GetBitRate () << std::endl;
  downstream->GetAttribute ("FreeCapacityVariation", uVal);
  std::cout << "FreeCapacityVariation: Bound (percent) on the variation of upstream free capacity = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("MaxPdu", uVal);
  std::cout << "MaxPdu: Peak rate token bucket maximum size (bytes) = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("DsScSpacing", dVal);
  std::cout << "DsScSpacing: Downstream subcarrier spacing = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("NumDsSc", uVal);
  std::cout << "NumDsSc: Number of downstream subcarriers = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("DsSpectralEfficiency", dVal);
  std::cout << "DsSpectralEfficiency: Downstream spectral efficiency (bps/Hz) = " << dVal.Get () << std::endl; 
  downstream->GetAttribute ("DsCpLen", uVal);
  std::cout << "DsCpLen: Downstream cyclic prefix length = " << uVal.Get () << std::endl; 
  downstream->GetAttribute ("DsIntlvM", uVal);
  std::cout << "DsIntlvM: Downstream interleaver M = " << uVal.Get () << std::endl; 
  downstream->GetAttribute ("CmtsDsPipelineFactor", uVal);
  std::cout << "CmtsDsPipelineFactor: downstream pipeline factor at CMTS = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("CmDsPipelineFactor", uVal);
  std::cout << "CmDsPipelineFactor: downstream pipeline factor at CM = " << uVal.Get () << std::endl;
  downstream->GetAttribute ("DsMacHdrSize", uVal);
  std::cout << "DsMacHdrSize: Downstream Mac header size = " << uVal.Get () << std::endl; 
  downstream->GetAttribute ("AverageCodewordFill", dVal);
  std::cout << "AverageCodewordFill: Average codeword fill = " << dVal.Get () << std::endl; 
  downstream->GetAttribute ("NcpModulation", uVal);
  std::cout << "NcpModulation: Downstream NCP modulation = " << uVal.Get () << std::endl; 
  std::cout << std::endl;
  std::cout << "System configuration and other assumptions" << std::endl;
  std::cout << "------------------------------------------" << std::endl;
  upstream->GetAttribute ("NumUsChannels", uVal);
  std::cout << "NumUsChannels: Number upstream channels = " << uVal.Get () << std::endl; 
  upstream->GetAttribute ("AverageUsBurst", uVal);
  std::cout << "AverageUsBurst: Average size of upstream burst = " << uVal.Get () << std::endl; 
  upstream->GetAttribute ("AverageUsUtilization", dVal);
  std::cout << "AverageUsUtilization: Average upstream utilization = " << dVal.Get () << std::endl; 
  std::cout << std::endl;
  std::cout << "HFC plant" << std::endl;
  std::cout << "---------" << std::endl;
  downstream->GetAttribute ("MaximumDistance", dVal);
  std::cout << "MaximumDistance: (in km) = " << dVal.Get () << std::endl;
  std::cout << std::endl;

  std::cout << "Calculated parameters:  DOCSIS MAC" << std::endl;
  std::cout << "----------------------------------" << std::endl;
  upstream->GetAttribute ("ScPerMinislot", uVal);
  std::cout << "ScPerMinislot: Subcarriers per minislot = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("FrameDuration", tVal);
  std::cout << "FrameDuration: Frame duration = " << std::fixed << std::setprecision (3) << tVal.Get ().As (Time::MS) << std::endl;
  std::cout << std::setprecision (oldPrecision);
  std::cout.unsetf (std::ios_base::fixed);
  upstream->GetAttribute ("MinislotsPerFrame", uVal);
  std::cout << "MinislotsPerFrame: Minislots per frame = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("MinislotCapacity", uVal);
  std::cout << "MinislotCapacity: Minislot capacity (bytes) = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("UsCapacity", dVal);
  std::cout << "UsCapacity: Upstream capacity (bps) = " << dVal.Get () << std::endl;
  upstream->GetAttribute ("CmMapProcTime", tVal);
  std::cout << "CmMapProcTime: CM MAP processing time = " << std::fixed << std::setprecision (3) << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("DsSymbolTime", tVal);
  std::cout << "DsSymbolTime: Downstream symbol time = " << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("DsIntlvDelay", tVal);
  std::cout << "DsIntlvDelay: Downstream interleaver delay = " << tVal.Get ().As (Time::MS) << std::endl;
  upstream->GetAttribute ("Rtt", tVal);
  std::cout << "Rtt: Round trip time = " << tVal.Get ().As (Time::MS) << std::endl;
  std::cout << std::setprecision (oldPrecision);
  std::cout.unsetf (std::ios_base::fixed);
  upstream->GetAttribute ("MinReqGntDelay", uVal);
  std::cout << "MinReqGntDelay: (frames) = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("FramesPerMap", uVal);
  std::cout << "FramesPerMap: (frames) = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("MinislotsPerMap", uVal);
  std::cout << "MinislotsPerMap: minislots per MAP = " << uVal.Get () << std::endl;
  upstream->GetAttribute ("ActualMapInterval", tVal);
  std::cout << "ActualMapInterval: actual MAP interval = " << std::fixed << std::setprecision (3) << tVal.Get ().As (Time::MS) << std::endl;
  std::cout << std::endl;

  std::cout << "Calculated parameters:  MAP message downstream overhead" << std::endl;
  std::cout << "-------------------------------------------------------" << std::endl;
  downstream->GetAttribute ("UsGrantsPerSecond", dVal);
  std::cout << "UsGrantsPerSecond: average # of grants scheduled on each US channel = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgIesPerMap", dVal);
  std::cout << "AvgIesPerMap: average # of grants scheduled in each MAP = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgMapSize", dVal);
  std::cout << "AvgMapSize: average size (bytes) of a MAP message = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgMapDatarate", dVal);
  std::cout << "AvgMapDatarate: average MAP datarate (bits/sec) = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("AvgMapOhPerSymbol", dVal);
  std::cout << "AvgMapOhPerSymbol: average MAP overhead per symbol (bytes) = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("ScPerNcp", dVal);
  std::cout << "ScPerNcp: subcarriers per NCP message block = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("ScPerCw", dVal);
  std::cout << "ScPerCw: subcarriers per codeword = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("DsCodewordsPerSymbol", dVal);
  std::cout << "DsCodewordsPerSymbol: Downstream codewords per symbol = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("DsSymbolCapacity", dVal);
  std::cout << "DsSymbolCapacity: Downstream symbol capacity (bytes) = " << dVal.Get () << std::endl;
  downstream->GetAttribute ("DsCapacity", dVal);
  std::cout << "DsCapacity: Downstream capacity (bits/sec) = " << dVal.Get () << std::endl;

  // Dispose of allocated memory
  upstream->Dispose ();
  downstream->Dispose ();
  scheduler->Dispose ();
  channel->Dispose ();
  Simulator::Destroy ();
  return 0;
}
