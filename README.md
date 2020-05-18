# DOCSIS® module for ns-3 (docsis-ns3)

This repository contains an extension module for the 
[ns-3 network simulator](https://www.nsnam.org) to simulate
[DOCSIS-3.1](https://www.cablelabs.com/technologies#DOCSIS%C2%AE-3.1-Technology) 
cable networks.  More information can be found on the 
[ns-3 App Store](https://apps.nsnam.org/app/docsis-ns3).

# Getting started

For users familiar with ns-3, this module can be downloaded or cloned into
the ``contrib/`` directory of a patched version of ns-3 mainline code.
The ``patches/`` directory contains the patch to apply to ns-3-dev, with
the patch file name indicating which version of ns-3-dev to use.
Once ns-3 is rebuilt, the build system will include the new module.

For users unfamiliar with ns-3, please consult the documentation available on
the [app store page](https://apps.nsnam.org/app/docsis-ns3).

# About this module

This module extends ns-3 to simulate the MAC layer operation of a single
DOCSIS link (between a cable modem and a CMTS) for a single customer.
The module includes abstracted PHY models to simulate the presence of
OFDM(A) physical channels, models to simulate the request/grant exchange
process for requesting upstream transmission opportunities for a cable
modem, a simple scheduling model to handle grant requests and congestion 
due to other CMs in the service group, a downstream MAC model for the CMTS, 
an AQM model (both DOCSIS-PIE and Dual Queue Coupled AQM), and Queue 
Protection.  All of the OFDM(A) channel configuration options are supported, 
and subset of the Service Flow QoS configuration parameters are supported. 
Both single service flow (corresponding to DOCSIS 3.1) and two service flow
(corresponding to Low Latency DOCSIS extensions to DOCSIS 3.1) are supported.
The corresponding specification is version I19 of the
[DOCSIS 3.1 MAC and Upper Layer Protocols Interface Specification](https://specification-search.cablelabs.com/CM-SP-MULPIv3.1).

