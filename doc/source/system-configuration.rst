DOCSIS System Configuration
---------------------------

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)


Upstream System & Model Parameters
**********************************



DOCSIS Upstream OFMDA Channel Parameters
========================================

* UsScSpacing: Upstream Subcarrier Spacing. Valid values 25e3 & 50e3 (25kHz & 50kHz); default = 50e3
* NumUsSc: Number of active upstream subcarriers. Valid values: 1-1900 for 50kHz SCs, 1-3800 for 25kHz SCs; default = 1880
* SymbolsPerFrame: Number of OFDMA symbols per OFDMA frame. Valid values 6-36; default = 6
* UsSpectralEfficiency: Upstream spectral efficiency in bps/Hz. Modulation order can vary on a per-subcarrier basis, this model implements this by applying the average value to all subcarriers. Valid values 1.0 - 12.0; default = 10.0
* UsCpLen: Upstream cyclic prefix length. Valid values: 96, 128, 160, 192, 224, 256, 288, 320, 384, 512, 640; default = 256


DOCSIS Upstream MAC Layer Parameters
====================================

* UsMacHdrSize: Upstream MAC Header Size (bytes). Valid values: 6-246; default = 10
* UsSegHdrSize: Upstream Segment Header Size (bytes). Always 8 bytes; default = 8
* MapInterval: Target MAP Interval (seconds).  The system will round this to the nearest integer multiple of the OFDMA frame duration to set the actual MAP interval used for simulation.  Valid values: any ; default = 2e-3

Upstream Model Parameters
=========================

* CmtsMapProcTime:  CMTS MAP Processing Time. The amount of time the model includes for performing scheduling operations.  This includes any “guard time” the CMTS wants to factor in. Valid values: 400us is the maximum allowed per DOCSIS spec; default = 200us
* CmUsPipelineFactor: = 1; CM burst preparation time, expressed as an integer of OFDMA frame times in advance of burst transmission that CM begins encoding.  Valid values: integer; default = 1
* CmtsUsPipelineFactor: = 1; CMTS burst processing time, expresssed as an integer of OFDMA frame times after burst reception completes that CMTS ends decoding.  Valid values: integer; default = 1

Downstream System & Model Parameters
************************************

DOCSIS Downstream OFDM Channel Parameters
=========================================

* DsScSpacing: Downstream Subcarrier Spacing. Valid values 25e3 & 50e3 (25kHz & 50kHz) default = 50e3
* NumDsSc: Number of active downstream subcarriers. Valid values: 1-3745 for 50e3 SC\_spacing, 1-7537 for 25e3 SC\_spacing. default = 3745
* DsSpectralEfficiency: Downsteam Spectral Efficiency.  The downstream modulation profile which the CM is using (bps/Hz). Modulation order can vary on a per-subcarrier basis, this model implements this by applying the average value to all subcarriers.   Valid range (float) 4.0–14.0; default = 12.0
* DsIntlvM:  Downstream Interleaver "M". Valid values 1-32 for 50e3 SC spacing, 1-16 for 25e3 SC spacing, M=1 means “off”; default = 3
* DsCpLen: DS cyclic prefix length (Ncp). Valid values: 192,256,512,768,1024; default = 512
* NcpModulation: Next Codeword Pointer modulation order. Valid values: 2,4,6; default = 4


DOCSIS Downstream MAC Layer Parameters
======================================
* DsMacHdrSize: Downstream MAC Header Size (bytes). Typically 10(no channel bonding) or 16(channel bonding). Valid values: 6-246. ; default = 10


Downstream Model Parameters
===========================

* CmtsDsPipelineFactor: CMTS transmission processing budget. Expressed in symbol times in advance of tx that encoding begins.  Valid values: integers; default = 1
* CmDsPipelineFactor: CM reception processing budget.  Expressed in symbol times after rx completes that decoding completes. Valid values: integers; default = 1
* AverageCodewordFill: Factor to account for 0xFF padding bytes between frames, shortened codewords due to profile changes, etc. Valid values: 0.0-1.0; default = 0.99


System Configuration Parameters
*******************************

* NumUsChannels: Number of US channels managed by this DS channel. This is used to calculate the UCD and MAP messaging overhead on the downstream channel. Valid values: integers; default = 1 
* AverageUsBurst: Average size of an upstream burst (bytes), used to calculate MAP messaging overhead on the downstream channel. Valid values: integers; default = 150
* AverageUsUtilization: Average utilization of the upstream channel(s). Used to calculate MAP overhead on the downstream channel. Valid values: 0.0-1.0; default = 0.1
* MaximumDistance: Plant kilometers from furthest CM to CMTS. Max per DOCSIS spec is 80km.  Valid values: 1.0-2000.0; default = 8

Implementation
**************

These attributes are defined in ``docsis-net-device.cc``.

Usage
*****

An example usage is provided in the program ``residential-example.cc`` around line 1210:


.. sourcecode:: C++

 // Configure DOCSIS Channel & System parameters
 
  // Upstream channel parameters
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsScSpacing", DoubleValue (50e3));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("NumUsSc", UintegerValue (1880));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("SymbolsPerFrame", UintegerValue (6));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsSpectralEfficiency", DoubleValue (10.0));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsCpLen", UintegerValue (256));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsMacHdrSize", UintegerValue (10));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("UsSegHdrSize", UintegerValue (8));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("MapInterval", TimeValue (MilliSeconds (1)));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("CmtsMapProcTime", TimeValue (MicroSeconds (200)));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("CmtsUsPipelineFactor", UintegerValue (1));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("CmUsPipelineFactor", UintegerValue (1));
 
  // Upstream parameters that affect downstream UCD and MAP message overhead
  docsis.GetUpstream (linkDocsis)->SetAttribute ("NumUsChannels", UintegerValue (1));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("AverageUsBurst", UintegerValue (150));
  docsis.GetUpstream (linkDocsis)->SetAttribute ("AverageUsUtilization", DoubleValue (0.1));
 
  // Downstream channel parameters
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsScSpacing", DoubleValue (50e3));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("NumDsSc", UintegerValue (3745));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsSpectralEfficiency", DoubleValue (12.0));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsIntlvM", UintegerValue (3));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsCpLen", UintegerValue (512));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("CmtsDsPipelineFactor", UintegerValue (1));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("CmDsPipelineFactor", UintegerValue (1));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("DsMacHdrSize", UintegerValue (10));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("AverageCodewordFill", DoubleValue (0.99));
  docsis.GetDownstream (linkDocsis)->SetAttribute ("NcpModulation", UintegerValue (4));
 
  // Plant distance (km)
  docsis.GetUpstream (linkDocsis)->SetAttribute ("MaximumDistance", DoubleValue (8.0));




