DOCSIS Tests
------------

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

Several unit and regression tests are located in the ``test/``
directory.  A test runner program called ``test.py`` is provided in
the top-level ns-3 directory.  Running ``test.py`` without any
arguments will run all of the tests for ns-3.  Running ``test.py`` with
the ``-s`` argument allows the user to limit the test to one test suite.

* ``docsis-link``:  Checks transmission of single packet, point-to-point
  mode or DOCSIS mode, and checks point-to-point mode for multiple packets.
  Configures the MSR to 50 Mbps and configures bursts of varying length
  of line-rate packets, allocated 50% to low latency and 50% to classic
  service flow.  Checks that the classic queue is not starved when GGR
  is at the MSR and peak rate > MSR (i.e. tests unused grant accounting).
* ``docsis-lld``:  Conducts a number of LLD-specific tests on a small test
  network, including the callbacks reporting on the number of grant bytes
  used and unused, and the exact time that packets should be received
  based on notional grant requests, MAP arrival times, and transmission
  times.  Also checks the arithmetic on Time to Minislot conversions.
* ``dual-queue-coupled-aqm``:  This test suite performs some basic unit
  testing on IAQM ramp thresholds.
* ``queue-protection``:  This test performs some basic unit testing on the
  traces, on the bucket selection, and on sanctioning.

