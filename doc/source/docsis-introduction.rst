Introduction
------------
.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)

The DOCSIS® extension module for |ns3| (``docsis-ns3``) allows users to
experiment with models of low latency DOCSIS® operation in the |ns3|
simulation environment.


Who Should Use This Guide
*************************

This guide is intended for researchers who are interested in developing packet
level simulations of networks that contain DOCSIS 3.1 cable broadband links. 
The guide gives a brief overview of |ns3|, describes the necessary steps to
build the module and run some example experiments, and it discusses some of
the internal architecture and current limitations of the model.

Getting Started
***************

What is ns-3?
=============

|ns3| is an open-source packet-level network simulator. |ns3| is written in C++,
with optional Python bindings. |ns3| is a command-line tool that uses native
C++ as its modeling languange. Users must be comfortable with at least basic
C++ and compiling code using g++ or clang++ compilers. Linux and MacOS are
supported; Windows native Visual Studio C++ compiler is not supported, but
Windows 10 machines can run |ns3| either through the Windows Subsystem for Linux,
or on a virtual machine.

An |ns3| simulation program is a C++ main() executable, or a Python program,
that links the necessary libraries and constructs a simulation scenario to
generate output data. Users are often interested in conducting a study in
which scenarios are re-run with slightly different configurations. This is
usually accomplished by a script written in Bash or Python (or another
scripting language) calling the |ns3| program with slightly different
configurations, and taking care to label and save the output data for
post-processing. Data presentation is usually done by users constructing their
own custom scripts and generating plots through tools such as Matplotlib or
gnuplot.

Some animators, visualizers, and graphical configuration editors exist for
|ns3| but most are not actively maintained.  Some extensions to |ns3|
can be found in the `ns-3 App Store <https://apps.nsnam.org>`_.

ns-3 documentation 
==================

A large amount of documentation on |ns3| is available at
https://www.nsnam.org/documentation.
New readers are suggested to thoroughly read the |ns3| tutorial.

Please note that this documentation attempts to quickly summarize how
users can get started with the specific features related
to DOCSIS.  There are portions of |ns3| that are not relevant to DOCSIS
simulations (e.g. the Python bindings or NetAnim network animator) so we
will skip over them.

What version of ns-3 is this?
=============================

This extension module is designed to be run with *ns3.39* release (July
2023) or later versions of |ns3|.

Prerequisites
=============

This version of |ns3| requires, at minimum, a modern C++ compiler 
supporting C++17 (g++ or clang++), a Python 3 installation, CMake,
and Linux or macOS.  

For Linux, distributions such as Ubuntu 20.04, RedHat 8, or anything
newer, should suffice.  For macOS, users will either need to install
the Xcode command line tools or the full Xcode environment.  

We have added experimental control and plotting scripts that have additional
Python dependencies, including:

* ``matplotlib``, ``numpy``, and ``pandas``: Numerical and plotting support  
* ``reportlab``: PDF and graphics support`
* ``pillow``: The Python Imaging Library (now maintained as pillow).

The above five modules should be installed if they are not already, using
whichever Python package manager is in use for your environment.

Finally, the experiment scripts require a PDF concatenation program,
either "``PDFconcat``", "``pdftk``", or "``pdfunite``".  One script
also requires ``pdfjam``.

For Mac users: ``pdfunite`` can be installed by installing the ``poppler``
package (Homebrew or MacPorts).  ``pdfjam`` can also be installed by
either package system.

How do I build ns-3?
====================

There are two steps, ``./ns3 configure`` and ``./ns3 build``.  ``ns3`` is a
Python wrapper script for an underlying CMake build system.

There are three main build modes defined for ns-3: `default`, `debug`, and `optimized`.  When running a simulation campaign, use `optimized` for faster code.  If you are debugging and want to disable optimizations and use |ns3| logging and asserts,
use `debug` code.  By default, the `default` build profile provides some level
of optimization while also retaining the logging and assert capabilities.

Try this set of commands to get started from within the top level |ns3| directory:

.. sourcecode:: bash

    $ ./ns3 configure --enable-examples --enable-tests
    $ ./ns3 build

Unit tests related to the DOCSIS code can be run as follows:

.. sourcecode:: bash

    $ ./test.py -s docsis-link
    $ ./test.py -s docsis-lld
    $ ./test.py -s dual-queue-coupled-aqm
    $ ./test.py -s queue-protection

To build a debug version (note: this build will create libraries that execute more slowly):

.. sourcecode:: bash

    $ ./ns3 clean
    $ ./ns3 configure -d debug --enable-examples --enable-tests
    $ ./ns3 build

It is generally a good idea to clean the build when changing versions
(the ``./ns3 clean`` command above).  Similar steps can be used to change
to an optimized build.

``ns3 configure`` reports missing features?
===========================================

You will see a configuration report after typing ``./ns3 configure`` that looks
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


Where are the interesting programs located?
=========================================== 

The ``examples/`` directory contains example DOCSIS simulation
programs.  Presently, four examples are provided:

* ``residential-example.cc``
* ``simple-docsislink.cc``
* ``docsis-configuration-example.cc``
* ``delay-estimation.cc``

In addition, the ``experiments/`` directory
contains bash scripts to automate the running and plotting of
scenarios.  The ``experiments/residential/`` contains plotting and execution
scripting around ``residential-example.cc``.
The ``experiments/simple-docsislink/`` contains plotting and execution
scripting around ``simple-docsislink.cc``.  Finally, the
``experiments/delay-estimation/`` contains plotting and execution around
the ``delay-estimation.cc`` program.

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

When all simulations have finished, you can recurse into the
timestamped directory named: ``results/test-YYYYMMDD-HHMMSS`` to find the
outputs.

More thorough documentation about the residential example program
is found in the same experiments directory
(in Markdown format) in the file named ``residential-documentation.md``.

Users can also inspect the unit test programs in ``test/`` for simpler examples
of how to put together simulations (although the test code is constructed
for testing purposes).

Editing the code
================

In most cases, the act of running a program or experiment script will
trigger the rebuilding of the simulator if needed, but you can force a
rebuild by typing ``./ns3 build`` at the top-level |ns3| directory.

