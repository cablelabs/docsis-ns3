Model Overview
--------------
.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

The Data Over Cable Service Interface Specification (DOCSIS) specifications
[DOCSIS3.1]_ are used by vendors to build interoperable equipment comprising two
device types:  the cable modem (CM) and the cable modem termination system (CMTS).
DOCSIS links are multiple-access links in which access to the uplink
and downlink channels on a hybrid fiber/coax (HFC) plant is controlled by a scheduler at the CMTS.  

This module contains |ns3| models for sending Internet traffic between CM and 
CMTS over an abstracted physical layer channel model representing the HFC plant.
These |ns3| models are focused on the DOCSIS MAC layer specification
for low-latency DOCSIS version 3.1, version I19, Oct. 2019 [DOCSIS3.1.I19]_.

The |ns3| models contain high-fidelity models of the MAC layer packet forwarding 
operation of these links, including detailed models of the active queue management (AQM)
and MAC-layer scheduling and framing.  Other aspects of MAC layer operation are 
highly abstracted. For example, no MAC Management Messages are exchanged between 
the CM and CMTS model.  

In brief, these models focus on the management of packet forwarding in a downstream
(CMTS to a single cable modem) or upstream (single cable modem to CMTS) 
direction, by modeling the channel access mechanism (requests and grants),
scheduling, and queueing (via Active Queue Management (AQM)) present
in the cable modem and CMTS. 

The physical channel is a highly abstracted model ofthe Orthogonal Frequency 
Division Multiplexing (OFDM)-based downstream and OFDM with Multiple Access 
(OFDMA)-based upstream PHY layer.  The channel model supports all of the basic 
DOCSIS OFDM/OFDMA PHY configuration options (#subcarriers, bit loading, frame 
sizes, interleavers, etc.), and can model physical plant length.  There are 
no physical-layer impairments implemented. 

Channel bonding is not currently supported.

Each instance of model supports a single CM / CMTS pair, and supports either a single 
US/DS Service Flow pair (with or without PIE AQM), or an LLD single US/DS 
Low Latency Aggregate Service Flow pair (each with a Classic SF and Low 
Latency SF). Upstream shared-channel congestion (i.e. multiple CMs contending 
for access to the channel) can be supported via an abstracted congestion 
model, which supports a time-varying load on the channel. There is currently 
no downstream shared-channel congestion model.

The model supports both contention requests and piggyback requests, but the 
contention request model is simplified - there are no request collisions, 
the CM always succeeds in sending a request sometime (selected via uniform 
random variable) in the next MAP interval. The scheduling types supported are 
Best Effort and PGS.  Supported QoS Params are: Max Sustained Rate, Peak Rate, 
Max Burst, Guaranteed Grant Rate. The PGS scheduler supports only two values 
for Guaranteed Grant Interval:  GGI = MAP interval  &  GGI = OFDMA Frame 
interval.  It also implements the LLD queuing functions, including 
dual-queue-coupled-AQM and queue-protection.

This model started as a port of an earlier |ns2| model of DOCSIS 3.0, to which
extensions to model OFDMA framing were added.  Some original authors
of the DOCSIS 3.0 |ns2| model are credited in these models.

Model Description
*****************

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

DocsisNetDevice
===============

As mentioned above, the ``DocsisNetDevice`` class is the base class for
``CmNetDevice``  and ``CmtsNetDevice``.  Its attributes and methods include
things that are in common  between the CM and CMTS, such as upstream and
downstream channel parameters, MAC-layer  framing, etc. The salient attributes
are described in a later section of this document.


CmtsNetDevice
=============

The CMTS functionality is encapsulated in an ns-3 class ``CmtsNetDevice``
modeling a DOCSIS
downstream link (from CMTS to the cable modem).  More specifically, 
it models a single OFDM downstream channel upon which either a single 
downstream service flow or a single downstream aggregate service flow 
(with underlying "classic" and "low-latency" service flows) is instantiated 
to provide service to a single cable modem. 

As per the DOCSIS 3.1 specification, each service flow uses two token buckets 
(peak and sustained rate) for rate shaping that will accumulate tokens 
according to their parameters.


CmNetDevice and CmtsUpstreamScheduler
=====================================

``CmNetDevice`` device type models a DOCSIS upstream link (from
cable modem to the CMTS).  More specifically, it models a single upstream
OFDMA channel upon which either a single 
upstream service flow or a single upstream aggregate service flow 
(with underlying "classic" and "low-latency" service flows) is instantiated 
to provide service to a single cable modem. 

DOCSIS's upstream transmission is scheduled in regular intervals called
"MAP intervals" (typically 1 - 2ms).  Before the beginning of each MAP interval, the cable modem
receives a MAP message potentially providing grants for its service flows during 
the MAP interval.  The size of the grant will depend on the backlog of bytes 
requested by the CM, the state of the rate shaper, and congestion from other 
users on the shared upstream link (see below).

The model is based upon generating events corresponding to the MAP interval
and a notional request-grant interaction between the ``CmNetDevice`` and an 
instance of the ``CmtsUpstreamScheduler`` object. While the ``CmtsUpstreamScheduler`` 
is notionally implemented at the CMTS, in the model there is no direct linkage between
the ``CmtsUpstreamScheduler`` object and the CmtsNetDevice.  

The following events periodically occur every MAP interval:

#. At a particular time in advance of the start of the MAP interval, the  
   ``CmtsUpstreamScheduler``  
   generates the MAP schedule by calculating how many "minislots" that it will grant to each 
   service flow and when within the MAP interval each grant is scheduled. The CMTS then 
   notionally transmits this information in a MAP message to the CM in advance 
   of the MAP interval by scheduling an event corresponding to its arrival at the CM.
#. At MAP message arrival, the CM parses the MAP message and creates an event shortly 
   before each grant time.  If there is no grant for a service flow within the MAP interval, 
   a contention request opportunity is identified at MAP arrival time, and an event is scheduled. 
#. At each grant event, the CM dequeues packets, calculates any new requests (for 
   piggybacking), notionally builds the L2 transmission frame, and schedules events 
   corresponding to packet arrivals at the CMTS.
#. At each contention request event, the CM calculates any new requests for the service flow, and
   schedules an event corresponding to the arrival of the request frame at the CMTS.

A key aspect of the model is that there is no actual exchange of |ns3| 
Packet objects between the CM and CMTS for the control plane; instead,
a MAP message reception is *simulated* at the CM as if the CMTS had
sent it, and Request message reception is *simulated* at the CMTS as if the CM had sent it.
The class ``CmtsUpstreamScheduler`` is responsible for the scheduling.

The ``CmtsUpstreamScheduler`` also has an abstract congestion model that can be used to induce
scheduling behavior corresponding to shared channel congestion (in which not all requested bytes
can be granted in the next MAP interval).  This is described in the next subsection.

DOCSIS 3.1 upstream frame transmission utilizes "continuous concatenation and fragmentation" 
in which L2 DOCSIS MAC frames (an Ethernet frame prepended with a DOCSIS MAC Frame Header) are
enqueued as a contiguous string of bytes, and then to fill a grant, an appropriate number of 
bytes are dequeued (without regard to DOCSIS MAC frame boundaries).  The resulting set of bytes 
has a Segment Header prepended before transmission.  As a result, each upstream transmission 
(i.e. each grant) could contain the tail end of one MAC frame, some number of whole MAC 
frames, and the head of another MAC frame. In the |ns3| model this is abstracted by simply 
calculating when the tail of each MAC frame would be transmitted, and then scheduling the 
arrival of the corresponding Packet object at the CMTS at the notional time when the tail 
would have arrived. 

The upstream channel in DOCSIS is composed of OFDMA frames, with a configured duration.  
Each frame consists of a set of minislots, each with a certain byte 
capacity.   All frames have the same number of minislots.  The minislots 
are numbered serially as shown below.

.. _fig-docsis-ofdma:

.. figure:: figures/docsis-ofdma.*

   DocsisNetDevice OFDMA model

The CMTS schedules grants for the CM(s) across a MAP Interval.  
The duration of MAP Intervals is required to 
be an integer number of minislots, but in this simulation model it is additionally constrained to 
be an integer number of OFDMA frames.    Within the MAP interval, grants to CMs 
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
This pipeline factor is intended to model the burst generation processing at
the transmitter and the demodulation and FEC decoding at the receiver.
In the figure, the grant is shown as shaded minislots and spans two 
frames.
Note that, if transmission latency is calculated from the start of grant, 
then it only includes transmission time + propagation delay + 
CMTS_US_pipeline_factor.

.. _fig-docsis-timeline:

.. figure:: figures/docsis-timeline.*

   CmNetDevice timeline

CmtsUpstreamScheduler Attributes
================================

The key attributes governing performance are the Rate, PeakRate, and MaxTrafficBurst
(controlling rate shaping) for the corresponding service flow and FreeCapacityMean 
and FreeCapacityVariation (controlling the amount of notional congestion).  Two 
classes, a ``ServiceFlow`` and an ``AggregateServiceFlow``, are used in conjunction
with a dual token bucket rate shaper to regulate the granting of transmission opportunities.
A cable modem
may have one stand-alone service flow that has a deep queue and DOCSIS-PIE AQM, or 
it may have two service flows under an aggregate service flow, corresponding to a 
low-latency and classic service flow combination as defined by low latency DOCSIS.  

The dual token bucket parameters:

* Rate (bits/sec) is the maximum sustained rate of the rate shaper
* MaxTrafficBurst (bytes) is the token bucket depth for the max sustained rate shaper
* PeakRate (bits/sec) is the peak rate of the rate shaper

These are associated with either the ``ServiceFlow`` (in the case of a stand-alone service 
flow) or with the ``AggregateServiceFlow`` (in the case of the low latency DOCSIS
configuration).  This is discussed in more detail in a later Chapter of this document.

The scheduler also supports an abstract congestion model that may constrain
the delivery of grants to the cable modem.

* FreeCapacityMean (bits/sec) is the notional amount of capacity available 
  for the CM.  When multiplied by the MAP interval and divided by 8 (bits/byte)
  it yields roughly the amount of bytes in a MAP interval available for grant
  (on average).
* FreeCapacityVariation is the percentage variation on a MAP-by-MAP basis
  around the FreeCapacityMean to account for notional congestion. 

For each MAP interval, the scheduler does a uniform random draw to calculate the 
amount of free capacity (minislots) in the current interval. The grant is then 
limited to this number of minislots.

Low Latency DOCSIS Features
***************************

When an ``AggregateServiceFlow`` object is instantiated (along with its two 
constituent ``ServiceFlow`` objects), this forms a "Low Latency ASF" which 
supports dual queue coupled AQM and queue protection.

Dual Queue Coupled AQM
======================

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
================

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
lighter users of the queue.  Queue Protection is optional and can be
disabled from a simulation. 

The implementation of Queue Protection closely follows the pseudocode
found in Annex P of [DOCSIS3.1.I19]_.



