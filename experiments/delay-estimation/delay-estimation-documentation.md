# ns-3 DOCSIS "delay-estimation" experiment documentation

Last modified:  August 31, 2023

## 1. Introduction

This experiment uses a bash script (delay-estimation.sh) to launch the
delay-estimation.cc program, in order to experiment with the virtual queue
described in the latest LLD specifications.

**delay-estimation.sh** executes a number of scenarios in parallel, and then
gathers the scenario trace data in subdirectories indexed by scenario number.
Two plotting programs are included to produce time-series plots from traces
of the L-queue.

**delay-estimation.cc** is a program to run two constant bit rate UDP packet
streams (unresponsive to AQM marks or drops) through a cable modem model
to evaluate the effectiveness of the virtual queue proposal.  The packet
streams can be crafted to test different operating conditions.

## 2. DOCSIS model overview

The ns-3 DOCSIS model is described in separate documentation.

## 3. Experiment overview

###3.1 Simulation Topology

The following figure represents the topology.

~~~
//              ------  --------  
// node 'n0'----| CM |--| CMTS |---- node 'n3'
//              ------  --------     
//               'n1'     'n2'
~~~

**Figure 1: delay-estimation.cc topology**

At the center of Figure 1 are two nodes to represent a single cable
modem (CM) and cable modem termination system (CMTS). The CMTS upstream
scheduler imposes a rate control (token bucket filter) based on the AMSR.
Figure 1 depicts a client node 'n0' sourcing CBR traffic to a server node 'n3'.


### 3.2 Traffic Patterns

#### 3.2.1 Simulation scenarios defined in delay-estimation.sh

The default patterns are shown below (and can be edited to create
different scenarios).

~~~
#scenario arguments:  scenario_id AMSR PeakRate GGR GGI Weight Ltraf Ctraf size capM capV
# - AMSR refers to the upstream AMSR; downstream AMSR is fixed at 200 Mbps
# - Peak Rate refers to the upstream Peak Rate
# - GGR is expressed as a data rate value (with units); zero means disabled
# - GGI is expressed as a microsecond value; zero defaults to one frame interval
# - Weight is an integer 1<weight<255
# - Ltraf and Ctraf are data rate values for LL and Classic UDP traffic
# - size is integer bytes (Data PDU size)
# - capM is FreeCapacityMean (data rate value), zero means disabled
# - capV is FreeCapacityVariation (percent), 0<capV<100
#
# The following parameters are set globally in above bash variables
# - MAP interval (default to 1ms)
# - simulationEndTime (default to 31s
# - estimator (QDelayCoupledL (default) or VirtualQueue)
# - vqResetInterval (default to 0)
# 
# The expected result from a well-functioning VQ is that the queue delay
# estimate in the L queue remains at the minimum value (1 packet) for 
# scenarios 1-4, and that in scenarios 5 and 6 it grows at a slow rate.
# Similarly, for scenarios 7-12, only scenario 12 should experience queue
# growth.

declare -a scenario=(\
#       S# AMSR     PeakRate GGR      GGI   Weight Ltraf Ctraf  size  capM capV
# Total 49.9-- no real queue buildup
        "1   50Mbps 100Mbps 0         0   230   49.9Mbps 0      1000   0   0   "
# Total 49.9-- no real queue buildup
        "2   50Mbps 100Mbps 0         0   230   44.9Mbps 5Mbps  1000   0   0   "
# Total 49.9-- no real queue buildup
        "3   50Mbps 100Mbps 0         0   230   24.9Mbps 25Mbps 1000   0   0   "
# Total 52.2 (not 50.1 due to scheduler overage)-- only c-queue buildup
        "4   50Mbps 100Mbps 0         0   230   44.9Mbps 7.3Mbps 1000   0  0   "
# Total 52.2 (not 50.1 due to scheduler overage)-- only l-queue buildup
        "5   50Mbps 100Mbps 0         0   230   52.2Mbps 0      1000   0   0   "
# Slightly over allocation for the L queue (compare with 4)
        "6   50Mbps 100Mbps 0         0   230   47.085Mbps  7.3Mbps 1000   0  0"
# Add PGS scenarios with gradually increasing GGR
        "7   50Mbps 100Mbps 10Mbps  1000   230   49.9Mbps 0      1000   0   0 "
        "8   50Mbps 100Mbps 20Mbps  1000   230   49.9Mbps 0      1000   0   0 "
        "9   50Mbps 100Mbps 30Mbps  1000   230   49.9Mbps 0      1000   0   0 "
        "10  50Mbps 100Mbps 40Mbps  1000   230   49.9Mbps 0      1000   0   0 "
        "11  50Mbps 100Mbps 49.9Mbps 1000  230   49.9Mbps 0      1000   0   0 "
# Repeat scenario 6 but with PGS enabled
        "12  50Mbps 100Mbps 20Mbps  1000  230   47.085Mbps 7.3Mbps 1000   0   0 "
        )
~~~

There are also some additional parameters at the top of the Bash script:

~~~
# These variables are not set in the Bash array but apply across all
# scenarios defined in the Bash array
export mapInterval=1ms
export simulationEndTime=31s
# Alternatives for the below are "QDelayCoupledL" or "VirtualQueue"
# This variable controls which estimator is actually used in the code
export estimator="VirtualQueue"
# If non-zero, enables optional periodic virtual queue reconciliation
# If zero, the periodic queue reconciliation will occur upon each dequeue
export vqResetInterval=1ms
# Random variable run number
export RngRun=1
# Whether to echo commandlog to foreground or save to file
export foreground=0

numSims=4 # number of simultaneous simulations to run.
          # Set this less than or equal to the number of cores on your machine

# Attributes for alternative PHY configurations
# Below values are the ns-3 defaults
export numUsSc=1880
export symbolsPerFrame=6
export usSpectralEfficiency=10
export usCpLen=256
export numDsSc=3745
export dsCpLen=512
~~~

### 3.3 Script execution

In the experiments/delay-estimation directory, one can run the script
as follows:

    $ cd experiments/delay-estimation
    $ ./delay-estimation.sh

The script will then launch four simulation processes in parallel,
in the background, and will continue to run scenarios until all 12
scenarios are completed.

### 3.4 Data output

The script is instrumented to provide the following output data in 
a timestamped results directory.  The artifacts are collected to
enhance future reproducibility:

1\) **commandlog.out:** Log of the process completions.

2\) **delay-estimation.cc:** Copy of the C++ program source used

3\) **delay-estimation.sh:** Copy of the shell script used

4\) **plot-dequeue-trace.py:** Copy of the dequeue trace plot program

5\) **plot-vq-align-trace.py:** Copy of the VQ alignment trace plot program

6\) **version.txt:** Information about the version of ns-3 that was used,
    including the branch, the latest commit hash, the date, and any
    diff against the last commit (i.e., ns-3 library modifications).

7\) **pdfunite.sh:** PDF concatenation script (not run by default; optionally
    used to combine all scenario PDFs into one file)

Then, in each scenario directory, the following are produced:

1\) **logNN.out:** Output of delay-estimation.cc for scenario number NN.

For example:

~~~
num: 187106 marked: 0 (0%)
time(ms)       :     min    50th    90th    99th     max
sojourn        :    1.32    2.64    3.44    3.97    4.71
qDelayCoupledL :   0.162    2.75    3.56    4.04    4.85
VirtualQueue   :    0.16   0.207   0.308   0.379   0.471
qDelayCoupledC :       0       0       0       0       0
~~~

The above provides the number of packets in the scenario, the number
that were marked, and then various percentiles of packet delays or
delay estimates observed in the trace files.   For instance, the 90th
percentile delay estimate of the actual sojourn time was 3.44 ms in the
above.

2\) **dequeue-trace-NN.pdf:** PDF of the time series plot of different
    latencies traced at dequeue time.

3\) **vq-align-trace-NN.pdf:** PDF of the time series plot of queue statistics
    around the virtual queue alignment process.
    
4\) **delay-estimation.cl.soujourn.dat:** Trace of the classic sojourn
    time.

5\) **delay-estimation.dequeue.dat:** Trace of the low latency dequeue
    trace (these values are plotted in dequeue-trace-NN.pdf).

6\) **delay-estimation.vq.align.dat:** Trace of the low latency virtual queue
    VQ align trace (these values are plotted in vq-align-trace-NN.pdf).

7\) **delay-estimation.ll.estimate.dat:** Trace of the latency estimate
    at enqueue time of the low latency queue.

## 4. Summary data

The script will generate four summary PDFs with plots from the numbered
scenarios.

1\) **align-vq-trace.pdf:** Check how the VQ alignment matches the actual queue.

2\) **tcp-cwnd-trace.pdf:** Plots of TCP cwnd evolution for TCP scenarios.

3\) **tcp-rtt-trace.pdf:** Plots of TCP RTT evolution for TCP scenarios.

4\) **tcp-throughput-trace.pdf:** Plots of TCP throughput for TCP scenarios.

The scenarios are crafted to send specific test streams to check the virtual
queue (VQ) operation under different scenarios.  The most used plots during
development were the VQ alignment plots (18 scenarios). The first six
scenarios sent UDP EF and BE traffic combinations (but no TCP) to probe
how well the VQ tracks the actual queue, when all of the traffic is L-queue
(scenario 1), the balance between C-queue and L-queue matches the scheduler
weight (scenario 2), a different weight value (128-- scenario 3), and overload
of the AMSR (scenarios 4-6).

In the plots, the desired outcome is that the average AQ (magenta line) is
close to the allowed AQ line (black line), and that even though the actual
queue (AQ) may vary due to multiple access delays, the virtual queue
(green line) usually stays at a low value.  Scenario 3 shows that the
Allowed AQ doesn't match with the Average AQ because the scheduling
assumptions are different than configured, but the VQ still stays low.
Scenarios 5 and 6 show a high virtual queue (and actual queue) but the offered
load exceeds the AMSR, and sanctioning is disabled, so this is expected.

The scenarios 7-12 provide UDP tests but with different levels of PGS
granting (the earlier scenarios had PGS disabled).  In all cases except
scenario 12 (overload), the virtual queue is maintained at a low value,
and the actual queue can be observed to decrease as the PGS grant rate
is increased (as expected).

The final six scenarios (numbered between 101 and 111) use a DCTCP flow
(surrogate for TCP Prague) instead of UDP traffic, under best-effort
(scenarios 101-103) and PGS granting (107-111) configurations.  Scenarios
102 and 103 introduce some competing C-queue traffic (UDP).  The virtual
queue is kept low (around 1 ms or below) for all scenarios, and the
actual latency varies between 0-5 ms (less for when PGS is enabled).
The TCP throughput trace plots show that the throughput is
close to the AMSR unless some C-traffic exists (scenarios 2 and 3)
to take capacity from the L-queue (as governed by the CMTS granting
that allocates capacity to the C-queue).
