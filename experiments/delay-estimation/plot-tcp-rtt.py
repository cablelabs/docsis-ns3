#!/usr/bin/env python3
import matplotlib
matplotlib.use("agg")
import matplotlib.pyplot as plt
import sys
import numpy as np
import re
import os

# This script should be run from within a 'scenario*' directory
regex=re.compile(r'scenario(\d+)')
try:
    scenarioStr=regex.search(os.path.split(os.getcwd())[1]).group(1)
except AttributeError:
    print("Not executed from an expected directory: %s" % os.path.split(os.getcwd ())[1])
    exit(1)
plotname = 'tcp-rtt-trace-' + scenarioStr + '.pdf'
title = "TCP RTT, scenario " + scenarioStr

# ns3
f = open('delay-estimation.tcp.rtt.dat', 'r')
t = []
rtt = []
for line in f:
    columns = line.split()
    t.append(float(columns[0]))
    rtt.append (float(columns[1]))
f.close()

plt.xlabel('Time (s)')
plt.ylabel('RTT (ms)')
plt.title(title)
plt.plot(t, rtt, marker='', color='black', label='DCTCP')
plt.legend(loc='lower right')
plt.ticklabel_format(useOffset=False)
#plt.show()
plt.savefig(plotname, format='pdf')
plt.close()
