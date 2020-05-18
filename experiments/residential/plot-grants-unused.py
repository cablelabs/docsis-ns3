#! /usr/bin/python
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
import gc
import sys
import time
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import math
import os.path
import numpy as np
import argparse

# Positional arguments are 1) heading, 2) simulationEndTime
parser = argparse.ArgumentParser()
parser.add_argument("plotHeading", help="plot heading")
parser.add_argument("simulationTime", help="simulation end time")
# Optional arguments
parser.add_argument("--fileName", help="general input filename")
parser.add_argument("--plotName", help="general output pdf filename")
args = parser.parse_args()

data=[]
fd = open(args.fileName, 'r')
for line in fd:
	data.append(line.split())

times=[float(x[0]) for x in data]
values=[float(x[1]) for x in data]
plt.scatter(times,values,1)
plt.xlabel('time (seconds)')
plt.ylabel('unused grant bytes')
plt.suptitle(args.plotHeading + ' - Grants Unused (bytes)',size=11)
# Add small table element to print mean value
v=np.array(values).astype(float)
cell_text = []
cell_text.append ('%.1f' % np.mean(v))
cell_rows=['Average']
the_table=plt.table(cellText=zip(cell_text),colWidths=[0.1],rowLabels=cell_rows,loc='lower right')
the_table.set_fontsize(8)
plt.title("Duration=" + args.simulationTime,size=10)
plt.savefig(args.plotName, format='pdf')
plt.close()	
fd.close()
sys.exit()
