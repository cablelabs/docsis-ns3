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

import sys
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('agg')
import matplotlib.pyplot as plt
import argparse
import re

parser = argparse.ArgumentParser()
# Positional arguments
parser.add_argument("fileName", help="file name")
# Optional timestep argument
parser.add_argument("--timestep", help="timestep resolution in seconds (default 0.1)")
parser.add_argument("--title", help="title string")
parser.add_argument("--subtitle", help="subtitle string")
args = parser.parse_args()

timestep = 0.1
if args.timestep is not None:
    timestep = float(args.timestep)

regex=re.compile(r'\d+')
scenarioNumber=regex.search(args.fileName).group(0)

title_string = "TCP throughput, scenario " + scenarioNumber
if args.title is not None:
    title_string = args.title
if args.subtitle is not None:
    title_string = title_string + args.subtitle

# create histogram of bits per timestep from input file
times=[]
bits_per_timestep=[]
fd = open(args.fileName, 'r')
current_time = 0
current_bits = 0 
for line in fd:
    l = line.split()
    # Any TCP upstream flow will have source address 10.1.1.3, proto 6, and
    # the ECN field will be '0'
    if (l[2] == "10.1.1.3" and l[7] == "6" and l[8] == "0"):
        timestamp = float(l[0])
        if (timestamp < (current_time + timestep)):
            current_bits = current_bits + int(l[1])*8
        else:
            times.append(current_time + timestep)
            bits_per_timestep.append(current_bits)
            current_time = current_time + timestep
            while(current_time + timestep <= timestamp):
                times.append(current_time + timestep)
                bits_per_timestep.append(0)
                current_time = current_time + timestep
            current_bits = int(l[1])*8
# finish last sample
times.append(current_time + timestep)
bits_per_timestep.append(current_bits)
fd.close()

if len(times) == 0:
    print("No data points found, exiting...")
    sys.exit(1)

# Convert observed bits per timestep to Mb/s and plot
rate_in_mbps = [float(x/timestep)/1e6 for x in bits_per_timestep]
plt.plot(times, rate_in_mbps)
plt.xlabel('Time (s)')
plt.ylabel('Rate (Mb/s)')
plt.title(title_string)
#plt.show()
plotname = "tcp-throughput-" + scenarioNumber + ".pdf"
plt.savefig(plotname, format='pdf')
plt.close()

sys.exit (0)
