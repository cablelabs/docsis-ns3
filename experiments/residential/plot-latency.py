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
cmFlowList,cmDelayList=[],[]
cmtsFlowList,cmtsDelayList=[],[]

columns = ('UDP-EF', 'UDP-Default', 'TCP', 'DCTCP')
columns_rtt = ('RTT (ms)','null')
rows = ['Maximum']
rows += ['99.9$^{th}$ Percentile']
rows += ['%d$^{th}$ Percentile'% x for x in (99,95,90,50,10,1)]
rows += ['Minimum']

rows_rtt = ['Maximum']
rows_rtt += ['99.9$^{th}$ Percentile']
rows_rtt += ['%d$^{th}$ Percentile'% x for x in (99,95,90,50,10,1)]
rows_rtt += ['Minimum']

# Positional arguments are 1) numTcpDownloads, 2) numTcpUploads, 3) numDashStreams, 4) numDctcpDownloads, 5) numDctcpUploads, 6) numDctcpDashStreams 7) numWebUsers, 9) heading, 10) simulationEndTime
parser = argparse.ArgumentParser()
parser.add_argument("numTcpDown", type=int, help="number of TCP downloads")
parser.add_argument("numTcpUp", type=int, help="number of TCP uploads")
parser.add_argument("numDash", type=int, help="number of DASH streams")
parser.add_argument("numDctcpDown", type=int, help="number of DCTCP downloads")
parser.add_argument("numDctcpUp", type=int, help="number of DCTCP uploads")
parser.add_argument("numDctcpDash", type=int, help="number of DCTCP DASH streams")
parser.add_argument("numWeb", type=int, help="number of Web users")
parser.add_argument("plotHeading", help="plot heading")
parser.add_argument("simulationTime", help="simulation end time")
# Optional arguments
parser.add_argument("--fileNameCm", help="CM latency file")
parser.add_argument("--fileNameCmts", help="CMTS latency file")
parser.add_argument("--plotNameCm", help="CM latency output pdf filename")
parser.add_argument("--plotNameCmts", help="CMTS latency output pdf filename")
parser.add_argument("--plotNameRtt", help="RTT estimate latency output pdf filename")
parser.add_argument("--imageNameRtt", help="RTT estimate latency output jpg filename")
parser.add_argument("--fileNameSummary", help="filename to append 99th% RTT")
parser.add_argument("--scenarioId", help="scenario id")
args = parser.parse_args()

# source IP address list
# 10.1.1.1 - upstream UDP-EF
# 10.1.1.2 - upstream UDP-Default
# 10.1.1.3 - upstream TCP/DASH
# 10.1.1.4 - upstream DCTCP/DASH
# 10.1.1.x where x > 4 - upstream Web Client
# 10.1.2.2 - downstream UDP-EF
# 10.1.3.2 - downstream UDP-Default
# 10.1.4.2 - unused address
# 10.1.x.2 where x > 4 - downstream TCP/DASH
# 10.2.x.2 where x > 4 - downstream DCTCP/DASH
# 10.5.1.2 - downstream Web Server

def parse_input(file):
	""" Read data and map source IP addresses to flow types 
	    (UDP-EF, UDP-Default, TCP, DCTCP)
	"""
	global udpEfFound
	delay_samples = []
	for line in file:
		delay_sample,ip_addr =line.split()[1:3]
		ipa,ipb,ipc,ipd=ip_addr.split('.')	
		if (int(ipc) == 1 and int(ipd) == 3):
			flow_type = "TCP"
		elif (int(ipc) == 1 and int(ipd) == 4):
			flow_type = "DCTCP"
		elif (int(ipc) == 1 and int(ipd) >= 5):
			flow_type = "TCP"
		elif (ip_addr == "10.5.1.2"): 
			flow_type = "TCP"
		elif (int(ipb) == 1 and int(ipc) >= 5):
			flow_type = "TCP"
		elif (int(ipb) == 2 and int(ipc) >= 5):
			flow_type = "DCTCP"
		elif (ip_addr == "10.1.1.1" or ip_addr == "10.1.2.2"): 
			udpEfFound = True
			flow_type = "UDP-EF"
		elif (ip_addr == "10.1.1.2" or ip_addr == "10.1.3.2"): 
			flow_type = "UDP-Default"
		else:
			print("ip_addr %s not mapped to flow" % ip_addr)
			sys.exit(1)
		delay_samples += [(flow_type, delay_sample)]
	return delay_samples

def split_tuple(t):
	""" split the tuples into two lists
	"""
	flowList, delayList = [],[]
	for item in range(len(t)):
		flow_type,delay_sample = t[item]
		flowList.append(flow_type)
		delayList.append(delay_sample)
	return (flowList, delayList)

def generate_uniq(flowList):
	""" generate list of unique ip addresses and their occurrence counts
	"""
	uniq, occur = [],[]
	for x in flowList:
		if x not in uniq:
			uniq.append(x)
			occur.append(flowList.count(x))
	return (uniq, occur)

def plot_a_line(xx,yy,flow_type,delayList):
	""" function to plot a single line in the CDF plot
	"""
	l=0
	global array_EF 
	global array_DF 
	global array_TCP 
	global array_DCTCP 

	s = []
	m = []

	for i in range(xx,yy,1):
		s.append( float (delayList[i]))
		l+=1

	s1 =sum(s)
	av=s1/float(l)

	s.sort()
	for j in range (1,l+1):
		m.append(j/float(l))

	if flow_type == "UDP-EF":
		plt.plot(s,m,color='green',label ='UDP-EF')
		array_EF = np.interp([1, 0.999, 0.99, 0.95, 0.90, 0.50, 0.10, 0.01, 0], m,s)
	elif flow_type == "UDP-Default":
		plt.plot(s,m,color='orange',label ='UDP-Default')
		array_DF= np.interp([1, 0.999, 0.99, 0.95, 0.90, 0.50, 0.10, 0.01, 0], m,s)
	elif flow_type == "DCTCP":
		plt.plot(s,m,color='red',label ='DCTCP')
		array_DCTCP= np.interp([1, 0.999, 0.99, 0.95, 0.90, 0.50, 0.10, 0.01, 0], m,s)
	elif flow_type == "TCP":
		plt.plot(s,m,color='blue',label ='TCP')
		array_TCP= np.interp([1, 0.999, 0.99, 0.95, 0.90, 0.50, 0.10, 0.01, 0], m,s)
	else:
		print("flow type %s not found" % flow_type)
		sys.exit(1)
	return 

def generate_titlestring():
	""" Generate title string from command line arguments
	"""
	title_string = "Web users=" + str(args.numWeb) + ", TCP (up/dn)="
	title_string += str(args.numTcpUp) + "/" + str(args.numTcpDown) + ", DASH flows="
	title_string += str(args.numDash) + ", DCTCP (up/dn)="
	title_string += str(args.numDctcpUp) + "/" + str(args.numDctcpDown) + ", DCTCP DASH flows="
	title_string += str(args.numDctcpDash) + ", Duration=" + str(args.simulationTime)
	return title_string

##############################################
# plot upstream CM latency

array_EF = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])
array_DF = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])
array_TCP = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])
array_DCTCP = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])

fd = open(args.fileNameCm, 'r')
udpEfFound= False
samples=parse_input(fd)
# sort the packets (by ip addr then by time)	
samples.sort()
cmFlowList, cmDelayList = split_tuple(samples)
sortedUpstreamSamples = samples
upstreamUdpEfFound = udpEfFound
del samples

cmUniq, cmOccur = generate_uniq(cmFlowList)

# Plot a line for each unique "flow" (ip address)
end_val=0
start_val=0
convertedTuple=[]
for ip_index in range(len(cmUniq)):	
	end_val=end_val + cmOccur[ip_index]
	plot_a_line(start_val,end_val,cmUniq[ip_index],cmDelayList)
	start_val=end_val

# plot annotations
resultant=zip(array_EF,array_DF,array_TCP, array_DCTCP)
for i in resultant:
	convertedTuple.append(tuple(map(lambda x: round(x, 2), i))) 
columnLabels = columns

plt.legend(loc="center right")
plt.xlabel('Latency in ms')
plt.ylabel('CDF of Packet Latency')
plt.suptitle(args.plotHeading + " - Upstream", size=11)
the_table=plt.table(cellText=convertedTuple, colWidths = [0.12]*4,rowLabels=rows,colLabels=columnLabels,loc='lower right')
the_table.set_fontsize(6)
plt.title(generate_titlestring(), size=10)
plt.savefig(args.plotNameCm, format='pdf')
plt.close()
fd.close()

us_ef_99=array_EF[1]

##############################################
# plot downstream CMTS latency

fd = open(args.fileNameCmts, 'r')
udpEfFound = False
samples=parse_input(fd)
# sort the packets (by ip addr then by time)	
samples.sort()
cmtsFlowList, cmtsDelayList = split_tuple(samples)
sortedDownstreamSamples = samples
downstreamUdpEfFound = udpEfFound
del samples

cmtsUniq, cmtsOccur = generate_uniq(cmtsFlowList)

# Plot a line for each unique "flow" (ip address)
end_val=0
start_val=0
convertedTuple=[]
try:
	del array_TCP
	del array_DF
	del array_EF
	del array_DCTCP
	array_EF = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])
	array_DF = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])
	array_TCP = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])
	array_DCTCP = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0])
except:
	pass
for ip_index in range(len(cmtsUniq)):	
	end_val=end_val + cmtsOccur[ip_index]
	plot_a_line(start_val,end_val,cmtsUniq[ip_index],cmtsDelayList)
	start_val=end_val

# plot annotations
resultant=zip(array_EF,array_DF,array_TCP,array_DCTCP)
for i in resultant:
	convertedTuple.append(tuple(map(lambda x: round(x, 2), i))) 

plt.legend(loc="center right")
plt.xlabel('Latency in ms')
plt.ylabel('CDF of Packet Latency')
plt.suptitle(args.plotHeading + " - Downstream", size=11)
the_table=plt.table(cellText=convertedTuple, colWidths = [0.12]*4,rowLabels=rows,colLabels=columnLabels,loc='lower right')
the_table.set_fontsize(6)
plt.title(generate_titlestring(), size=10)
plt.savefig(args.plotNameCmts, format='pdf')
plt.close()
fd.close()

ds_ef_99=array_EF[1]


##############################################
# Generate RTT CDF

sortedUpstreamUdpSamples = []
cmFlowList, cmDelayList = split_tuple(sortedUpstreamSamples)
for ftype, delay in sortedUpstreamSamples:
	if (upstreamUdpEfFound and ftype == "UDP-EF"):
		sortedUpstreamUdpSamples.append (delay)
	elif (not upstreamUdpEfFound and ftype == "UDP-Default"):
		sortedUpstreamUdpSamples.append (delay)
sortedUpstreamUdpSamples = [float(x) for x in sortedUpstreamUdpSamples]

sortedDownstreamUdpSamples = []
cmFlowList, cmDelayList = split_tuple(sortedDownstreamSamples)
for ftype, delay in sortedDownstreamSamples:
	if (downstreamUdpEfFound and ftype == "UDP-EF"):
		sortedDownstreamUdpSamples.append (delay)
	elif (not downstreamUdpEfFound and ftype == "UDP-Default"):
		sortedDownstreamUdpSamples.append (delay)
sortedDownstreamUdpSamples = [float(x) for x in sortedDownstreamUdpSamples]

maxUpstreamLatency = max(sortedUpstreamUdpSamples)
maxDownstreamLatency = max(sortedDownstreamUdpSamples)
# Histogram bin width (input data in units of ms: 0.001 corresponds to 1 us)
bin_width = 0.001
bin_width = max(bin_width, round(max(maxUpstreamLatency,maxDownstreamLatency)/10000,3))

# Calculate the range of each pmf vector, corresponding to the bin width 
us_pmf_size = int(round (maxUpstreamLatency/bin_width + 1))
ds_pmf_size = int(round (maxDownstreamLatency/bin_width + 1))
pmf_max_size = max(us_pmf_size, ds_pmf_size)

pmf_latency_vector=np.arange(0, (pmf_max_size + 1)) * bin_width
us_pmf, us_edges=np.histogram(sortedUpstreamUdpSamples, bins=pmf_latency_vector, density=True)
# Scale by bin width to obtain pmf (see numpy.histogram 'density' definition)
us_pmf = us_pmf * bin_width
ds_pmf, ds_edges= np.histogram(sortedDownstreamUdpSamples, bins=pmf_latency_vector, density=True)
ds_pmf = ds_pmf * bin_width

rtt_pmf=np.convolve(us_pmf, ds_pmf)
rtt_cdf=np.cumsum(rtt_pmf)
cdf_latency_vector=np.arange(0, len(rtt_cdf)) * bin_width
if upstreamUdpEfFound:
	plt.plot(cdf_latency_vector.tolist(), rtt_cdf.tolist(), color='green', label ='UDP-EF')
else:
	plt.plot(cdf_latency_vector.tolist(), rtt_cdf.tolist(), color='orange', label ='UDP-Default')
percentiles = np.interp([1, 0.999, 0.99, 0.95, 0.90, 0.50, 0.10, 0.01, 0], rtt_cdf.tolist(), cdf_latency_vector.tolist())
percentiles_list = [ '%.3f' % elem for elem in percentiles ]
resultant=zip(percentiles_list)
the_table=plt.table(cellText=resultant,colWidths = [0.24]*2,rowLabels=rows_rtt,colLabels=columns_rtt,loc='lower right')
the_table.set_fontsize(8)
the_table.scale(0.8, 1)
plt.legend(loc="center right")
plt.xlabel('Latency in ms')
plt.ylabel('CDF of RTT estimate')
plt.suptitle("CDF of RTT estimate", size=11)
plt.title(generate_titlestring(), size=10)
plt.savefig(args.plotNameRtt, format='pdf')
plt.savefig(args.imageNameRtt, format='jpg')
plt.close()

# calculate ccdf
rtt_ccdf = [ (1 - float(x)) for x in rtt_cdf.tolist()]
RTT_CCDF_YLIM = 0.001

# Find the necessary length to represent the CCDF down to RTT_CCDF_YLIM
for index, elem in enumerate(rtt_ccdf):
    if (elem < RTT_CCDF_YLIM):
        break
    length_for_ylim = index
del rtt_ccdf[length_for_ylim:]
cdf_latency_vector=np.arange(0, len(rtt_ccdf)) * bin_width

# save CCDF data
ccdf_output_file = open('ccdf_' + args.scenarioId + '.dat', 'w')
for index, elem in zip(cdf_latency_vector, rtt_ccdf):
    ccdf_output_file.write("%s %s\n" % (index, elem))
ccdf_output_file.close()




# Append 99% US, DS & RTT to summary file
fdSummary = open(args.fileNameSummary, 'a')
fdSummary.write(" %.3f %.3f %s\n" % (us_ef_99,ds_ef_99,percentiles_list[1]))
fdSummary.close()
