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
the patch file name indicating which version of ns-3 to use.
Once ns-3 is reconfigured, the build system will include the new module.

1. Download and unpack the most recent release of ns-3 (ns-3.35), either with a browser or from the command-line as follows. 

    `$ wget https://www.nsnam.org/releases/ns-allinone-3.35.tar.bz2`
    `$ tar xjf ns-allinone-3.35.tar.bz2`
    `$ cd ns-allinone-3.35/ns-3.35`
  
  **Note:**  The development version ``ns-3-dev`` might also work with the current code:

    `$ git clone https://gitlab.com/nsnam/ns-3-dev.git`

2. Change into the `contrib` directory:

    `$ cd contrib`

3. Within the `contrib` directory, clone this repository into a `docsis` directory:

    `$ git clone https://github.com/cablelabs/docsis-ns3.git docsis`

   **Note:**  Ensure that the directory's name is 'docsis' and not 'docsis-ns3'

4. Try to patch ns-3 with some additional test applications and changes used in the examples:

    `$ cd ../`

    `$ patch -p1 -i contrib/docsis/patches/ns-3.35.patch --dry-run`

5. If the above dry-run works, patch the code for real.

    `$ patch -p1 -i contrib/docsis/patches/ns-3.35.patch`

   This patch will eventually go away once the application changes are upstreamed.
   
   If the patch does not apply cleanly, please open an issue on the GitHub
   tracker for the docsis-ns3 project.

6. Now configure and build ns-3 as usual (optimized build is recommended):

    `$ ./waf configure --enable-examples --enable-tests -d optimized`

    `$ ./waf build`

  Follow instructions in the [ns-3 tutorial](https://www.nsnam.org/releases/ns-3-35/documentation/) if you are unfamiliar with how to build ns-3 and run programs.

   If the configuration complains that it `Could not find a task generator for the name 'ns3-docsis-ns3'`, this means that the directory was cloned without renaming it to `docsis`.

7.  Try to run an example program:

    `$ ./waf --run residential-example`

Look at `contrib/docsis/experiments/residential/residential-documentation.md` for more documentation about this example.

For users unfamiliar with ns-3, please consult the documentation available on
the [app store page](https://apps.nsnam.org/app/docsis-ns3) or listed 
in the tutorial link above.

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
The corresponding specification is version I21 of the
[DOCSIS 3.1 MAC and Upper Layer Protocols Interface Specification](https://specification-search.cablelabs.com/CM-SP-MULPIv3.1).

