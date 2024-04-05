#!/usr/bin/env python3
"""
# Copyright (c) 2017-2020 Cable Television Laboratories, Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions, and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The names of the authors may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# Alternatively, provided that this notice is retained in full, this
# software may be distributed under the terms of the GNU General
# Public License ("GPL") version 2, in which case the provisions of the
# GPL apply INSTEAD OF those given above.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""

# This plotting script is designed to run with the delay-estimation.sh
# script.  For each scenario, this script will plot a few time-series
# from the ns-3 trace of low latency dequeue events.  Specifically,
# each time a packet is dequeued from the low latency queue, the following
# quantities are traced (and plotted herein as a time series):
# 1. Actual sojourn time experienced
# 2. qDelayCoupledL/V estimate that was obtained at the time of enqueuing
#    the packet (i.e., the queue delay value that Iaqm and queue protection
#    would have used upon enqueue)
# 
# The output of this is a file named 'dequeue-trace-NN.pdf' where the NN
# is the scenario number.

import sys
import os
import matplotlib
matplotlib.use('agg')
import matplotlib.pyplot as plt
import argparse
import re

parser = argparse.ArgumentParser()
# Positional arguments
args = parser.parse_args()

colors = ['b', 'g', 'm', 'k', 'c', 'y', 'r']

# This script should be run from within a 'scenario*' directory
regex=re.compile(r'scenario(\d+)')
try:
    scenarioStr=regex.search(os.path.split(os.getcwd())[1]).group(1)
except AttributeError:
    print("Not executed from an expected directory: %s" % os.path.split(os.getcwd ())[1])
    exit(1)
scenarioNum = int(scenarioStr)
plotname = 'dequeue-trace-' + scenarioStr + '.pdf'

# Parse dequeue trace file
times=[]
sojourns=[]
delays=[]
try:
    fd = open('delay-estimation.dequeue.dat', 'r')
except FileNotFoundError:
    print("Data file delay-estimation.dequeue.dat not found")
    exit(1)
for line in fd:
    l = line.split()
    times.append(float(l[0]))
    sojourns.append(float(l[1]))
    delays.append(float(l[2])) 
fd.close()
if len(times) != 0:
    title_string = "Low latency queue dequeue trace"
    plt.plot(times, sojourns, linestyle='-', linewidth=2,alpha=.8, color=colors[0], label='sojourn times (actual)')
    plt.plot(times, delays, linestyle='-', linewidth=2,alpha=.8, color=colors[1], label='qDelayCoupledL/V at enqueue')
    plt.xlabel('Time (s)')
    plt.ylabel('Delay (ms)')
    plt.ylim([0,10])
    plt.legend(loc='upper left')
    plt.title(title_string)
    #plt.show()
    plt.savefig(plotname, format='pdf')
    plt.close()
else:
    print("No times found (data file empty?), exiting")

