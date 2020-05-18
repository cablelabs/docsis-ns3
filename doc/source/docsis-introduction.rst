Introduction
------------
.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

The DOCSIS® extension module for ns-3 (``docsis-ns3``) allows users to
experiment with models of low latency DOCSIS® operation in the ns-3
simulation environment.

Getting Started
***************

What is ns-3?
=============

ns-3 is an open-source packet-level network simulator. ns-3 is written in C++, with optional Python bindings. ns-3 is a
command-line tool that uses native C++ as its modeling languange. Users
must be comfortable with at least basic C++ and compiling code using g++
or clang++ compilers. Linux and MacOS are supported; Windows native
Visual Studio C++ compiler is not supported, but Windows 10 machines can
run ns-3 either through the Bash Subsystem for Linux, or on a virtual
machine.

An ns-3 simulation program is a C++ main() executable, or a Python
program, that links the necessary libraries and constructs a simulation
scenario to generate output data. Users are often interested in
conducting a study in which scenarios are re-run with slightly different
configurations. This is usually accomplished by a script written in Bash
or Python (or another scripting language) calling the ns-3 program with
slightly different configurations, and taking care to label and save the
output data for post-processing. Data presentation is usually done by
users constructing their own custom scripts and generating plots through
tools such as Matplotlib or gnuplot.

Some animators, visualizers, and graphical configuration editors exist
for ns-3 but are not actively maintained.

ns-3 documentation 
==================

A large amount of documentation on |ns3| is available at
https://www.nsnam.org/documentation.
New readers are suggested to thoroughly read the |ns3| tutorial.

Please note that this documentation attempts to quickly summarize how
sers can get started with the specific features related
to DOCSIS.  There are portions of ns-3 that are not relevant to DOCSIS
simulations (e.g. the Python bindings or NetAnim network animator) so we
will skip over them.

What version of ns-3 is this?
=============================

This extension module is designed to be run with ns-3.31 release (expected
for May 2020) or later versions of ns-3.

Prerequisites
=============

This version of ns-3 requires, at minimum, a modern C++ compiler 
supporting C++11 (g++ or clang++), a Python 3 installation,
and Linux or macOS.  

For Linux, distributions such as Ubuntu 16.04, RedHat 7, or anything
newer, should suffice.  For macOS, users will either need to install
the Xcode command line tools or the full Xcode environment.  

We have added experimental control and plotting scripts that have additional
Python dependencies, including:

* ``matplotlib``:  Consult the Matplotlib installation guide: https://matplotlib.org/faq/installing_faq.html.
* ``reportlab``:  Typically, either ``pip install reportlab`` or ``easy_install reportlab``
* `pillow`: The Python Imaging Library (now maintained as pillow).  Typically, ``pip install pillow`` or ``easy_install pillow``
* A PDF concatenation program, either "``PDFconcat``", "``pdftk``", or "``pdfunite``"

For Mac users: ``PDFconcat`` is simply an alias to ``/System/Library/Automator/Combine PDF Pages.action/Contents/Resources/join.py``

How do I build ns-3?
====================

There are two steps, ``waf configure`` and ``waf build``.

There are two main library types built by waf: `debug` and `optimized`.  When running a simulation campaign, use `optimized` for faster code.  If you are debugging and want ns-3 logging, use `debug` code.

Try this set of commands to get started from within the top level ns-3 directory:

.. sourcecode:: bash

    $ ./waf configure -d optimized --enable-examples --enable-tests
    $ ./waf build
    $ ./test.py

The last line above will run all of the ns-3 unit tests.  To build a debug version:

.. sourcecode:: bash

    $ ./waf configure -d debug --enable-examples --enable-tests
    $ ./waf build

``waf configure`` reports missing features?
===========================================

You will see a configuration report after typing ``./waf configure`` that looks
something like this:

::

    ---- Summary of optional NS-3 features:
    Build profile                 : optimized
    Build directory               :
    BRITE Integration             : not enabled (BRITE not enabled (see option --with-brite))
    DES Metrics event collection  : not enabled (defaults to disabled)
    Emulation FdNetDevice         : enabled
    ...

Do not worry about the items labeled as `not enabled`; you will not need them
for DOCSIS simulations.

What is `waf`? 
==============

This is a Python-based build system, similar to ``make``.  See the
ns-3 documentation (https://www.nsnam.org/documentation) for more information.

Where are the interesting programs located?
=========================================== 

The ``examples/`` directory contains the main DOCSIS C++
programs.  Only one example, ``residential-example.cc``, is provided
at this time.

the ``experiments/residential/`` contains plotting and execution
scripting around this program, to automate the running of some
interesting experiments.

Try these commands:

.. sourcecode:: bash

    $ cd experiments/residential
    $ ./residential.sh test

After the build information is displayed (showing what modules are enabled
and disabled), you should see something like this, indicating a number of
processes have been spawned in parallel in the background:

.. sourcecode:: bash

    ***************************************************************
    * Launched:  results/test-20200220-190624/residential.sh
    * Output in:  results/test-20200220-190624/commandlog.out
    * Kill this run with:  kill -SIGTERM -30307
    ***************************************************************

When all seven have finished, you can recurse into the
timestamped directory named: ``results/test-YYYYMMDD-HHMMSS`` to find the
outputs.

More thorough documentation about this is found in the same directory
(in Markdown format) in the file named ``residential-documentation.md``.

Editing the code
================

In most cases, the act of running a program or experiment script will
trigger the rebuilding of the simulator if needed, but you can force a
rebuild by typing ``./waf build`` at the top-level ns-3 directory.

Disclaimer
==========

This document is furnished on an "AS IS" basis and CableLabs does not provide
any representation or warranty, express or implied, regarding the accuracy,
completeness, noninfringement, or fitness for a particular purpose of this
document, or any document referenced herein. Any use or reliance on the
information or opinion in this document is at the risk of the user, and
CableLabs shall not be liable for any damage or injury incurred by any person
arising out of the completeness, accuracy, infringement, or utility of any
information or opinion contained in the document.  CableLabs reserves the right
to revise this document for any reason including, but not limited to, changes
in laws, regulations, or standards promulgated by various entities, technology
advances, or changes in equipment design, manufacturing techniques, or
operating procedures.  This document may contain references to other documents
not owned or controlled by CableLabs. Use and understanding of this document
may require access to such other documents. Designing, manufacturing,
distributing, using, selling, or servicing products, or providing services,
based on this document may require intellectual property licenses from third
parties for technology referenced in this document. To the extent this document
contains or refers to documents of third parties, you agree to abide by the
terms of any licenses associated with such third-party documents, including
open source licenses, if any. This document is not to be construed to suggest
that any company modify or change any of its products or procedures. This
document is not to be construed as an endorsement of any product or company
or as the adoption or promulgation of any guidelines, standards, or
recommendations.

Model Overview
**************

|ns3| contains models for sending Internet traffic between cable modems
(CM) and cable modem termination systems (CMTS) over an abstracted physical
layer channel model representing the hybrid fiber coax (HFC) link.
The Data Over Cable Service Interface Specification (DOCSIS) specifications
[DOCSIS3.1]_ are used by vendors to build interoperable equipment. 

DOCSIS links are multiple-access links in which access to the uplink
and downlink channels is controlled by a scheduler at the CMTS.  The |ns3|
models contain high-fidelity models of the MAC layer operation of these
links, including detailed models of the active queue management (AQM)
and MAC-layer scheduling and framing, over highly abstracted models of
the Orthogonal Frequency Division Multiplexing with Multiple Access
(OFDMA)-based PHY layer.

These |ns3| models are focused on the latest DOCSIS MAC layer specification
for low-latency DOCSIS version 3.1, version I19, Oct. 2019 [DOCSIS3.1.I19]_.
In brief, these models focus on the management of latency in a downstream
(CMTS to a single cable modem) or upstream (single cable modem to CMTS) 
direction, by modeling the channel access mechanism (dynamic grants),
scheduling, and queueing (via Active Queue Management (AQM)) present
in the cable modem and CMTS.  Other aspects of the system such as the
physical layer are heavily abstracted in this model.  Channel bonding
for DOCSIS 3.x links is not explicitly modeled but can be approximated by
setting the link rate equal to the aggregate link rate for the bonding group.

This model started as a port of the |ns2| model of DOCSIS 3.0, but
later extensions to model OFDMA framing were added.  Some original authors
of the DOCSIS 3.0 model are credited in these models due to this previous
|ns2| model.

Model Description
=================

This model is organized into a single ``docsis-ns3`` |ns3| extension module,
which can be placed either in the |ns3| ``src`` or ``contrib`` directory.
Typically, extension modules are placed in the ``contrib`` directory.

There are two specialized ``NetDevice`` classes to model the DOCSIS
downstream and upstream devices, and a special Channel class to 
interconnect them.  The base class ``DocsisNetDevice`` supports both the
``CmNetDevice`` and the ``CmtsNetDevice`` classes.  

The main modeling emphasis of these DOCSIS NetDevice models is to model the 
latency due to scheduling, queueing, fragmentation, and framing at the MAC
layer.  Consequently, there is a lot of abstraction at the physical layer.  

CmtsNetDevice
#############

The CMTS functionality is encapsulated in an ns-3 class ``CmtsNetDevice``
modeling a DOCSIS
downstream link (from CMTS to the cable modem).  More specifically, 
it models a single downstream aggregate service flow providing service 
to a single cable modem. It takes the following parameters:

* **Rate**: "Maximum Sustained Traffic Rate": i.e. Token bucket rate (bits/s) 
* **MaxTrafficBurst**:  "Maximum Traffic Burst": i.e. Token bucket maximum size (bytes) 
* **PeakRate**:  "Peak Traffic Rate": i.e. Peak rate token generation rate (bits/s)
* **MaxPdu**: Peak rate token bucket maximum rate (bytes): leave at 1532 to model DOCSIS 

The CMTS also has an abstract congestion model that can be used to induce
scheduling behavior corresponding to congestion (in which not all grants
can be satisfied in the next MAP interval).  This is implemented in the
``CmtsScheduler`` object described below.

As per the DOCSIS 3.1 specification, this device uses two token buckets
for rate shaping that will accumulate tokens according to their parameters.
A departing packet gets the peak or normal transmission rate depending on
the available tokens.

CmNetDevice
###########

``CmNetDevice`` device type models a DOCSIS upstream link (from
cable modem to the CMTS).  More specifically, it models a single upstream
aggregate service flow with best effort scheduling service and also
proactive granting according to the latest specification.  

The model is based upon generating events corresponding to the MAP interval
and a notional interaction with a CMTS scheduler object.  The following events
periodically occur every MAP interval:

#. The MAP interval itself starts
#. A grant (from the CMTS) may start at some point within the MAP interval,
   if data is available and permitted to be transmitted
#. The schedule and grant from the CMTS scheduler is received

A key aspect of the model is that there is no actual exchange of |ns3| 
Packet objects between the CM and CMTS for the control plane; instead,
a MAP message reception is *simulated* at the CM as if the CMTS had
scheduled it.  The class ``CmtsUpstreamScheduler`` is responsible for the scheduling.

DOCSIS's upstream transmission is scheduled at a regular interval called
a "MAP interval".  Before the beginning of each MAP interval, the cable modem
receives a grant
for how many bytes it can send.  This byte count varies as a result of
congestion from other users on the shared upstream link (see above).

The upstream channel is composed of OFDMA frames, with a configured duration.  
Each frame consists of a set of minislots, each with a certain byte 
capacity.   All frames have the same number of minislots.  The minislots 
are numbered serially as shown below.

.. _fig-docsis-ofdma:

.. figure:: figures/docsis-ofdma.*

   DocsisNetDevice OFDMA model

The CMTS schedules grants for the CM(s) across a MAP Interval.  
The duration of MAP Intervals is required to 
be an integer number of minislots, but can be additionally constrained to 
be an integer number of frames.    Within the MAP interval, grants to CMs 
are composed of an integer number of minislots.  Grants can span frame 
boundaries, but cannot span MAP Interval boundaries. 

:ref:`fig-docsis-pipeline` illustrates that the model maintains an internal
pipeline to track which data frames are eligible for transmission in which
MAP interval.  There is a delay between requests being calculated by the 
CM and the requests being considered in the formation of a MAP.  When it 
builds a MAP, the CMTS will take into consideration all requests calculated 
up until the deadline.  Since requests are piggybacked on grants, the timing
of the grant within the MAP interval will affect whether the request 
beats the deadline or not.  In the figure, the grant for MAP (z-2) occurs
before the deadline for processing for MAP interval z, so any requests
notionally sent here can be handled in MAP interval z.  If the grant had
happened later than the deadline, however, the request would have been
scheduled in MAP interval (z+1).

.. _fig-docsis-pipeline:

.. figure:: figures/docsis-pipeline.*

   CmNetDevice pipeline

The upstream transmission latency has four components, the 
CM_US_pipeline_factor, transmission time, propagation delay, and 
CMTS_US_pipeline_factor.  :ref:`fig-docsis-timeline` shows the expected 
case that both pipeline_factors equal 1 (i.e. one frame time at each end).  
In the figure, the grant is shown as shaded minislots and spans two 
frames.
Note that, if transmission latency is calculated from the start of grant, 
then it only includes transmission time + propagation delay + 
CMTS_US_pipeline_factor.

.. _fig-docsis-timeline:

.. figure:: figures/docsis-timeline.*

   CmNetDevice timeline

CmtsUpstreamScheduler
#####################

The key attributes governing performance are the Rate, PeakRate, and MaxTrafficBurst
(controlling rate shaping) and FreeCapacityMean and FreeCapacityVariation
(controlling the amount of notional congestion).  Two classes, a 
``ServiceFlow`` and an ``AggregateServiceFlow``, are used in conjunction
with token buckets to regulate the granting of transmission opportunities.
A cable modem
may have one or two service flows in an aggregate service flow; if only
one, it corresponds to the classic DOCSIS PIE service, and if two, it
corresponds to a low-latency and classic service flow combination.  

* Rate (bits/sec) is the maximum sustained rate of the rate shaper
* PeakRate (bits/sec) is the peak rate of the rate shaper
* MaxTrafficBurst (bytes) is usually configured by default to 
  the value of 0.1 * Rate (maximum sustained rate).

The scheduler also supports an abstract congestion model that may constrain
the delivery of grants to the cable modem.

* FreeCapacityMean (bits/sec) is the notional amount of capacity available 
  for the CM.  When multiplied by the MAP interval and divided by 8 (bits/byte)
  it yields roughly the amount of bytes in a MAP interval available for grant
* FreeCapacityVariation is the percentage variation around the FreeCapacityMean
  to account for notional congestion. 

Dual Queue Coupled AQM
######################

The dual queue coupled AQM model is embedded into the CmNetDevice and
CmtsNetDevice objects.  Based on the IP ECN codepoint or other classifiers,
packets entering the DocsisNetDevice are classified as either Low Latency
or Classic, and enqueued in the respective queue.  The Dual Queue
implements a relationship between the ECN marking probability for Low
Latency traffic and the drop probability for Classic traffic.
Packets are dequeued from the dual queue either using its internal
weighted deficit round robin (WDRR) scheduler that balances between the
two internal queues, or based on grants that explicitly provide 
transmission opportunities for each of the two service classes.
The implementation of Dual Queue Coupled AQM closely follows the pseudocode
found in Annexes M, N, and O of [DOCSIS3.1.I19]_.

Queue Protection
################

QueueProtection is an additional object that inspects packets arriving
to the Low Latency queue and, if the queue becomes overloaded and the
latency is impacted beyond a threshold, will sanction (redirect) packets
from selected flows into the Classic queue.  Flows are hashed into one
of 32 buckets (plus an overload bucket), and in times of congestion,
flows will maintain state in the bucket and generate congestion scores that,
when crossing a threshold score, will result in the packet being redirected.
In this manner, the queue can be protected from certain overload
situations (as might arise from misclassification of traffic), and the
system tends to sanction the most heavy users of the queue before
ligher users of the queue.  Queue Protection is optional and can be
disabled from a simulation. 

The implementation of Queue Protection closely follows the pseudocode
found in Annex P of [DOCSIS3.1.I19]_.

Using this module
*****************

At present, much of the Sphinx structured documentation for this module
(i.e., this document) is not yet written, and simple tutorial programs
are not provided.  However, a comprehensive example program
called ``residential-example.cc`` is located in the
``examples/`` directory, and a corresponding scripted experiment
is located in the ``experiments/residential`` directory.
Documentation on this experiment and how to run it can be found in
the Markdown document ``experiments/residential/residential-documentation.md.``
Users can also inspect the unit test programs for simpler examples
of how to put together simulations (although the test code is constructed
for testing purposes).

