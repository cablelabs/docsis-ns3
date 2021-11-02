Service Flow Configuration
--------------------------

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

Service Flow Model Overview
***************************
Service Flows are defined in DOCSIS as a unidirectional flow of packets
to which certain Quality-of-Service properties are attributed or enforced.
Each Service Flow (SF) has a unique identifier called a Service Flow Identifier
(SFID) assigned by the CMTS.  In practice, upstream service flows also
have a unique Service Identifier (SID), but the ns-3 model does not use
SIDs, and instead uses the SFIDs in place of SIDs.

In DOCSIS 3.1, each cable modem would have at least one upstream and 
one downstream service flow defined, with rate shaping enabled, and all
user traffic would typically use these two service flow definitions.
Low Latency DOCSIS uses an extension of this concept to define Aggregate
Service Flows which consist of paired (unidirectional) Service Flows,
one for classic traffic, and one for low latency traffic.  An Aggregate
Service Flow (ASF) may have a single Service Flow defined or (more
commonly) would have two Service Flows.  The ASF has aggregate rate
shaping parameters specified, and the individual SF may also have
rate shaping parameters on a per-SF basis.

In the ns-3 model, only one CM is possible on a link, so the user
will need to configure either an ASF or a single SF for the upstream
direction, and one for the downstream direction, and will need to
add these objects to the appropriate device (the CmNetDevice for the
upstream ASF or SF, and the CmtsNetDevice for the downstream ASF or SF).

In a real DOCSIS system, SFIDs are unique across all of the upstream and
downstream SFs within a "MAC Domain". Currently the ns-3 model uses a fixed
SFID of 1 for the classic SF or for a single standalone SF, and uses a fixed
SFID of 2 for the low latency SF.  These same SFIDs are used in both
directions.

Configuration of SFs or ASFs is essential for conducting a DOCSIS
simulation and should be performed after the DOCSIS topology has
been constructed.

This ns-3 model currently supports the following service flow configuration
options:

1) a single Service Flow configured for a DOCSIS PIE AQM.  This service flow
   may include rate shaping parameters (Maximum Sustained Traffic Rate,
   Peak Traffic Rate, and Maximum Traffic Burst) which, if explicitly
   configured, will cause the CM upstream requests to be rate shaped,
   and will cause the CMTS scheduler to rate shape the granted bytes.
   If rate shaping parameters are not configured, the default values will
   disable rate shaping.

2) a Low Latency Aggregate Service Flow with a Classic Service Flow and
   a Low Latency Service Flow.  The Aggregate Service flow may include
   rate shaping QoS parameters that are enforced at the CMTS scheduler.
   Additionally, the Classic Service Flow may configure rate shaping
   parameters, in which case the CM upstream requests will be rate
   shaped based on Service Flow-level paramters.  Although, in practice,
   it is possible to also configure QoS parameters on the Low Latency
   Service Flow, causing rate shaping of upstream grant requests, the
   ns-3 model does not support this; only the Classic Service Flow
   of a Low Latency Aggregate Service Flow may express QoS parameters.

Implementation
**************

The implementation can be found in ``docsis-configuration.h``.  Two
classes are defined:  ``ServiceFlow`` and ``AggregateServiceFlow``.
The members of these objects are organized according to Annex C
of [DOCSIS3.1.I19]_; in practice, the elements of SF and ASF
configuration are encoded in TLV structures, and the ns-3
representation of them closely mirrors the TLV structure and naming
convention found in the specification.

The classes ``ServiceFlow`` and ``AggregateServiceFlow`` derive from
the ns-3 base class ``Object``, which means that they are heap-allocated
objects managed by the ns-3 smart pointer class ``Ptr``.

The class definition is more like a C-style struct with public data
members than a C++ class with private data member accessed by methods.
In addition, an inheritance hierarchy is not defined.  Instead, a
number of members are defined for each class, and users selectively
choose to enable the parameters of interest in their simulation.  By
default, the default values of each of these members will not cause
any configuration actions; it is only when the user changes a value
that such configuration will have an effect in the program.  Parameters
that do not apply to a particular service flow configuration are ignored
by the model, and some are marked as 'for future use' and are not
functional at this time.

Usage
*****
Typical usage can be found in the program ``residential-example.cc`` around
line 1170:

Single SF configuration
=======================

.. sourcecode:: C++

  DocsisHelper docsis;
  ...
  // Add service flow definitions
  if (numServiceFlows == 1)
    {
      NS_LOG_DEBUG ("Adding single upstream (classic) service flow");
      Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
      sf->m_maxSustainedRate = upstreamMsr;
      sf->m_peakRate = upstreamPeakRate;
      sf->m_maxTrafficBurst = static_cast<uint32_t> (upstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);
      docsis.GetUpstream (linkDocsis)->SetUpstreamSf (sf);
      NS_LOG_DEBUG ("Adding single downstream (classic) service flow");
      sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
      sf->m_maxSustainedRate = downstreamMsr;
      sf->m_peakRate = downstreamPeakRate;
      sf->m_maxTrafficBurst = static_cast<uint32_t> (downstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);
      docsis.GetDownstream (linkDocsis)->SetDownstreamSf (sf);
    }

The first block of code shows the simpler case of a single service flow
in each direction.  This corresponds to a DOCSIS 3.1 configuration without
Low Latency DOCSIS support.  The upstream SF object is first created and
assigned to a smart pointer ``sf``:

.. sourcecode:: C++

      Ptr<ServiceFlow> sf = CreateObject<ServiceFlow> (CLASSIC_SFID);


Note here that an argument to the constructor, using a defined constant,
is required.  In the ns-3 model, a classic SF is assigned with a SFID
of value 1 (CLASSIC_SFID), and a low latency SF is assigned with a SFID
value of 2 (LOW_LATENCY_SFID).  The ns-3 code uses these special values
to distinguish between the two types.

Next, the rate shaping parameters are assigned.  In this program, 
the Maximum Traffic Burst
was configured above in units of time (``maximumBurstTime``) at the MSR,
so the program converts this into units of bytes.

.. sourcecode:: C++

      sf->m_maxSustainedRate = upstreamMsr;
      sf->m_peakRate = upstreamPeakRate;
      sf->m_maxTrafficBurst = static_cast<uint32_t> (upstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);

No other parameters are configured, so the SF object is added to the cable
modem (CmNetDevice) object, as the next statement performs by using the
helper object to get the upstream device:

.. sourcecode:: C++

      docsis.GetUpstream (linkDocsis)->SetUpstreamSf (sf);

The process is repeated for the downstream direction, and the SF is added to
the CmtsNetDevice.

ASF configuration
=================

ASF configuration takes more statements, since both an 
``AggregateServiceFlow`` object and two constituent ``ServiceFlow``
objects must be create for each direction.  We will look at the
upstream direction only.

.. sourcecode:: C++

  else
    {
      NS_LOG_DEBUG ("Adding upstream aggregate service flow");
      Ptr<AggregateServiceFlow> asf = CreateObject<AggregateServiceFlow> ();
      asf->m_maxSustainedRate = upstreamMsr;
      asf->m_peakRate = upstreamPeakRate;
      asf->m_maxTrafficBurst = static_cast<uint32_t> (upstreamMsr.GetBitRate () * maximumBurstTime.GetSeconds () / 8);
      Ptr<ServiceFlow> sf1 = CreateObject<ServiceFlow> (CLASSIC_SFID);
      asf->SetClassicServiceFlow (sf1);
      Ptr<ServiceFlow> sf2 = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
      sf2->m_guaranteedGrantRate = guaranteedGrantRate;
      asf->SetLowLatencyServiceFlow (sf2);
      docsis.GetUpstream (linkDocsis)->SetUpstreamAsf (asf);

First, the ``AggregateServiceFlow`` object is created; note that the
ASF ID is not required in the constructor.  This value defaults to zero
and is not presently used in the ns-3 model.  Next, the QoS parameters
around rate shaping are configured at an ASF level, similar to the
code for the single SF.  When an ASF is present in the model, the
CMTS upstream scheduler object will enforce rate shaping based on
ASF-level rate shaping parameters as part of the grant allocation process.

Following the rate shaping configuration, two individual SF objects
are created and added to the ASF object.  In the Low Latency SF case,
an optional parameter is configured:  a Guaranteed Grant Rate to
enable PGS operation.  Similar configuration is performed in the
downstream direction (without the GGR configuration because it does
not apply for downstream).  In the above example, no rate shaping parameters
are configured on the Classic Service Flow, and as a result, the upstream
requests will not be shaped for this service flow.

Options
=======

Besides the setting of rate shaping parameters, optional overrides
on some configuration parameters in the DualQueueCoupledAqm or
QueueProtection models is possible.  While its possible to use the
typical ns-3 configuration techniques to configure non-default values
on ns-3 Attribute values in the DualQueue and QueueProtection models,
which are applied when a new object is instantiated, it is recommended instead
to use the ServiceFlow or AggregateServiceFlow configuration mechanism.  Following
instantiation, the configuration of some ASF and SF parameters
offers the opportunitity to override the initially instantiated
configuration.  This is done by selectively changing the value of
the following options away from their default value.

For example, the following parameter exists in class ``ServiceFlow``: 

.. sourcecode:: C++

  uint8_t m_classicAqmTarget {0}; //!< C.2.2.7.15.2 Classic AQM Latency Target (ms); set to non-zero value to override DualQueue default

If the value is left at the default of zero (i.e., if the user does not
set this), the act of loading an ASF or SF into the DocsisNetDevice
will not change any configuration.  If, however, the user elects to
change this such as follows:

.. sourcecode:: C++

      sf = CreateObject<ServiceFlow> (CLASSIC_SFID);
      sf->m_classicAqmTarget = 15;

then this value will be used to change the value of the parameter found
in the ``DualQueueCoupledAqm::ClassicAqmLatencyTarget`` attribute for
the object that the SF is added to (either CM or CMTS).  This type of
configuration is somewhat redundant with other means in ns-3 of changing
attribute values, but is enabled here because it is in line operationally
with how some of these parameters might be configured in practice during
Service Flow definition.  Note also that if the code had been:

.. sourcecode:: C++

      sf = CreateObject<ServiceFlow> (LOW_LATENCY_SFID);
      sf->m_classicAqmTarget = 15;

the attempted configuration of ClassicAqmLatencyTarget would have been
ignored because classic AQM configuration is out of scope for a Low
Latency Service Flow.

The following TLV parameters (outside of the rate shaping parameters of
MSR, PeakRate, and MaximumBurst that are available for all service flow
types) are available for classic Service Flows:

* ``m_tosOverwrite``: C.2.2.7.9: IP Type Of Service (DSCP) Overwrite 

* ``m_targetBuffer``: C.2.2.7.11.4: Target Buffer (bytes)

* ``m_aqmDisable``: C.2.2.7.15.1: SF AQM Disable

*  ``m_classicAqmTarget``: C.2.2.7.15.2: Classic AQM Latency Target

The following TLV parameters are available for low latency Service Flows:

* ``m_tosOverwrite``: C.2.2.7.9: IP Type Of Service (DSCP) Overwrite 

* ``m_targetBuffer``: C.2.2.7.11.4: Target Buffer (bytes)

* ``m_aqmDisable``: C.2.2.7.15.1: SF AQM Disable

* ``m_iaqmMaxThresh``: C.2.2.7.15.4: Immediate AQM Max Threshold

* ``m_iaqmRangeExponent``: C.2.2.7.15.5: Immediate AQM Range Exponent of Ramp Function

* ``m_guaranteedGrantRate``: C.2.2.8.13: Guaranteed Grant Rate

* ``m_guaranteedGrantInterval``: C.2.2.8.14: Guaranteed Grant Interval

The following TLV parameters are available for Aggregate Service Flows:

* ``m_coupled``:  COUPLED behavior of Annex M

* ``m_aqmCouplingFactor``: C.2.2.7.17.5: AQM Coupling Factor

* ``m_schedulingWeight``: C.2.2.7.17.6: Scheduling Weight

* ``m_queueProtectionEnable``: C.2.2.7.17.7: Queue Protection Enable

* ``m_qpLatencyThreshold``: C.2.2.7.17.8: QPLatencyThreshold

* ``m_qpQueuingScoreThreshold``: C.2.2.7.17.9: QPQueuingScoreThreshold

* ``m_qpDrainRateExponent``: C.2.2.7.17.10: QPDrainRateExponent

See the file ``docsis-configuration.h`` for more details about the
units and behavior associated with each of these parameters.

