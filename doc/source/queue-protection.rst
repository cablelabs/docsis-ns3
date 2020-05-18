Queue Protection
----------------

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

The Queue Protection model is supported by objects of class 
``ns3::docsis::QueueProtection``.   The model is an implementation of the
pseudocode description of Annex P, CM-SP-MULPIv3.1-I19-191016 [DOCSIS3.1.I19]_.
This object is designed to be used in conjunction with a
DualQueueCoupledAqm object.  The two objects have references to one
another, so that DualQueueCoupledAqm can pass a packet to this object
for evaluation at enqueue time, and this object can access latency
estimates of the dual queue.  This object also depends on a hash algorithm,
which is passed in via a callback.  The use of a callback allows for
test configurations in which the hash output is more deterministic.

Usage
*****

Most DOCSIS module users who create the network via the DocsisHelper
methods will not need to do anything special to enable the default
Queue Protection model; it is enabled by default.

To disable the use of Queue Protection, place the following statement
in your program:

::

    Config::SetDefault ("ns3::docsis::QueueProtection::QProtectOn", BooleanValue (false));

Typical code (used in the ``DocsisHelper``) for creating and connecting these objects is as follows:

::

    Ptr<DualQueueCoupledAqm> dual = CreateObject<DualQueueCoupledAqm> ();
    // ... other dual queue configuration
    Ptr<QueueProtection> queueProtection = CreateObject<QueueProtection> ();
    queueProtection->SetHashCallback (MakeCallback(&DocsisLowLatencyPacketFilter::GenerateHash32));
    queueProtection->SetQueue (dual);
    dual->SetQueueProtectionCallback (MakeCallback(&QueueProtection::QProtect, queueProtection));

