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
# from the ns-3 trace of virtual queue alignment events.  Specifically,
# each time the virtual queue is checked for a possible reconciliation
# with the smoothed actual queue (typically, on a 1ms interval), the following
# quantities are traced (and plotted herein as a time series):
# 1. Actual queue (AQ) length
# 2. Smoothed actual queue (Smoothed-AQ)
# 3. Virtual queue (VQ) length after possibly being aligned
# 4. Allowed actual queue (Allowed-AQ) based on estimate of loop delay and
#    configuration (service flow rates and MAP interval)
# 
# The output of this is a file named 'align-vq-trace-NN.pdf' where the NN
# is the scenario number.

import sys
import os
import matplotlib
matplotlib.use('agg')
import matplotlib.pyplot as plt
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument("amsr", help="AMSR in value units notation", type=str)
args = parser.parse_args()

# Convert from a string like '50Mbps' to a float
#amsr_numeric = re.sub("[^0-9]","", args.amsr)
amsr_split = re.findall(r'[A-Za-z]+|\d+', args.amsr)

amsr = float(amsr_split[0])
if amsr_split[1] == "Mbps":
    amsr = amsr * 1000000
elif amsr_split[1] == "Kbps":
    amsr = amsr * 1000
elif amsr_split[1] == "bps":
    amsr = amsr;
else:
    print("Error: %s units not understood" % amsr_split[1])
    sys.exit(1)

# This script should be run from within a 'scenario*' directory
regex=re.compile(r'scenario(\d+)')
try:
    scenarioStr=regex.search(os.path.split(os.getcwd())[1]).group(1)
except AttributeError:
    print("Not executed from an expected directory: %s" % os.path.split(os.getcwd ())[1])
    exit(1)
plotname = 'align-vq-trace-' + scenarioStr + '.pdf'

colors = ['b', 'g', 'm', 'k', 'c', 'y', 'r']

# create histogram of bits per timestep from input file
times=[]
lengths=[]
vq_lengths=[]
avg=[]
allowed=[]
try:
    fd = open('delay-estimation.align.vq.dat', 'r')
except FileNotFoundError:
    print("Data file delay-estimation.align.vq.dat not found")
    exit(1)
for line in fd:
    l = line.split()
    times.append(float(l[0]))
    lengths.append(float(l[1])) 
    # VQ reported in nanoseconds; convert to bytes
    vq_lengths.append(float(l[2])*amsr/(8 * 1e9)) 
    avg.append(float(l[3])) 
    allowed.append(float(l[4])) 
fd.close()
if len(times) == 0:
    print("No data points found, exiting...")
    sys.exit(1)
title_string = "Queue length samples at VQ interval, scenario " + scenarioStr
fig, ax1 = plt.subplots()
ax2 = ax1.twinx()

# Use a callback to set a second y axis in units of ms
def convert_ax2(ax1):
  y1, y2 = ax1.get_ylim()
  ax2.set_ylim(y1*(8/amsr)*1000,y2*(8/amsr)*1000)
  ax2.figure.canvas.draw ()
  ax2.set_ylabel('ms')

ax1.callbacks.connect("ylim_changed", convert_ax2)

ax1.plot(times, lengths, linestyle='-', linewidth=2,alpha=.8, color=colors[0], label='AQ')
ax1.plot(times, vq_lengths, linestyle='-', linewidth=2,alpha=.8, color=colors[1], label='VQ')
ax1.plot(times, avg, linestyle='-', linewidth=2,alpha=.8, color=colors[2], label='Avg-AQ')
ax1.plot(times, allowed, linestyle='-', linewidth=2,alpha=.8, color=colors[3], label='Allowed-AQ')
ax1.set_xlabel('Time (s)')
ax1.set_ylabel('bytes')
ax1.set_ylim(bottom=0)
ax1.set_title(title_string)
ax1.legend(loc='upper left')
#plt.show()
fig.savefig(plotname, format='pdf')
plt.close(fig)
