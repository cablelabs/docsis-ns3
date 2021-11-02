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

"""Produce summary PDF report after simulations have been run by latency.sh.

Program inputs:
  $ python3 summary-report.py <title>

Program operation:
   The program relies on summary data files and latency data files indexed
   by scenario number.  Summary data files and latency files are expected
   to be in a temp/ directory below the directory where the program is invoked.

   The program works as follows:
   1. The list of summary files is built by searching for all filenames in the
      temp/ directory pattern-matching 'summary*.dat'
   2. A python.reportlab Canvas object is started, and the program starts
      to build the table based on the summary files.
   3. Each summary file is processed, and almost the entire data table is
      built by copying data from each summary*.dat file.  The last row (totals)
      is not yet built.  The process of building all but the last row yields
      a list of scenario IDs, matching the field found in each
      'summary(\w+).dat' file, for the next step.
   4. The list of scenario IDs is passed to prepare_latency_data_for_rtt_cdf().
      These are used as indices to open each 'latency-CM<index>.dat' and
      'latency-CMTS<index>.dat' file containing the latency samples gathered
      during each scenario run. The structure of the latency.cc topology
      is leveraged to map packet IP address to flow type; one of 'UDP-EF',
      'UDP-Default', or 'TCP'.  The method builds two aggregate lists of
      latency samples, with (flowType, delaySample) tuples, sorted in
      ascending order of delaySample across all flows.  These lists are
      returned by the method, and also copied into two new temp files,
      'cm-total.dat' and 'cmts-total.dat', which can be inspected for
      debugging or other processing.
   5. The program next builds a list of 'selected_flow_types' by inspecting
      the latency samples.  The list is ordered 'UDP-EF' (if present),
      'UDP-Default', and 'TCP'.  In addition, a flow color for the RTT plot
      is added, so as to match with the color convention of the other
      per-scenario plots.  It may be possible for UDP-EF to be absent, in
      which case only the two flow types 'UDP-Default' and 'TCP' are processed.
      Note: If there is no EF data present, the summary*.dat files will
      already contain per-scenario 99th percentile estimates for UDP-Default.
   6. The last row of the summary table is built based on averaging subtotals
      saved from an earlier step, except the last cell (aggregate 99th
      percentile value for the first subflow type; typically UDP-EF but
      sometimes UDP-Default).  This value is obtained by processing the
      aggregate data in the next step.
   7. The aggregate latency samples, list of selected flow types, and list
      of colors for flows is then passed to the process_files_for_rtt_cdf()
      function.  This function returns the value of the 99th percentile
      RTT for the first flow type, but in the course of doing so, also
      generates an aggregate RTT CDF for all selected flow types, and a
      complementary CDF for the RTT of the first subflow type.  The same
      technique used in plot-latency.py is followed; namely, to build discrete
      pmfs, on a per-flowtype basis, of the upstream and downstream one-way
      delays, and then to convolve the two pmfs to obtain an estimate of the
      rtt pmf (then converted back to a cdf).  Selected percentile values
      are extracted and put into a table embedded in the image.  The values
      used to plot the CCDF are left in a 'ccdf.dat' file in the temp/
      directory.
   8. With all table data now available, the table is written out to the
      Canvas, after the title (passed in as a command-line argument) is
      prepended.
   9. The 'rtt-cdf.jpg' and 'rtt-ccdf.jpg' images are appended.  Note that
      the xlim axis maximum for the CDF is configured by default to 200ms
      but can be changed in the below program (the RTT_CDF_XLIM constant).
      Also, the ylim minimum for the CCDF is configured by default to
      the value 0.001, and can be changed below (RTT_CCDF_YLIM constant).

Program outputs:
   A file 'summary.pdf' in the same directory as the program is invoked.
   File 'temp/cm-total.dat' and 'temp/cmts-total.dat' in the temp/ directory.
   Files 'temp/rtt-cdf.jpg' and 'temp/rtt-cdf.pdf' in the temp/ directory.
   Files 'temp/rtt-ccdf.jpg' and 'temp/rtt-ccdf.pdf' in the temp/ directory.
   File 'temp/ccdf.dat' in the temp/ directory.
"""
import sys, os, fnmatch, re
import numpy as np
import matplotlib
from operator import itemgetter
matplotlib.use('Agg')
import matplotlib.pyplot as plt
try:
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import letter, inch
    from reportlab.lib.styles import ParagraphStyle
    from reportlab.platypus import SimpleDocTemplate, Paragraph, Table, TableStyle, Image, Spacer
    from reportlab.pdfgen import canvas
except ImportError:
    print("Python module reportlab not installed; exiting...")
    sys.exit(1)

# Xlim value (milliseconds) for use in CDF
RTT_CDF_XLIM = 200
# Ylim value for use in CCDF
RTT_CCDF_YLIM = 0.001
# Histogram bin width value (units of milliseconds)
BIN_WIDTH = 0.001

def prepare_latency_data_for_rtt_cdf(scenario_ids):
    """Prepare latency sample files from input latency trace files.

    Recurse into 'temp/' directory and process all latency-CM and
    latency-CMTS data files.  Use the structure of latency.cc topology
    to figure out the packet type by IP address.  Return two lists of
    2-D tuples (flow_type, delay_sample); one for CM and one for
    CMTS.  The lists are sorted by delay across all flow types.

    For debugging support, this function also writes two files,
    temp/cm-total.dat and temp/cmts-total.dat, as line-delimited files
    of all of the values returned.

    Args:
        scenario_ids: A list of scenario IDs of the form
            'latency-CM' + id + '.dat'; each such file should be located
            in the 'temp' directory

    Returns:
        Two lists of paired values (tuples); one for CM and one
        for CMTS.  Each pair is a flowtype and a latency sample.  Latency
        values are sorted in ascending order across all flowtypes.
    """
    cm_delay_samples = []
    cmts_delay_samples = []
    for id in scenario_ids:
        filename = 'latency-CM' + id + '.dat'
        with open(filename) as f:
            for line in f:
                delay_sample,ip_addr =line.split()[1:3]
                ipa,ipb,ipc,ipd=ip_addr.split('.')
                if (int(ipc) == 1 and int(ipd) >= 3):
                    flow_type = "TCP"
                elif (ip_addr == "10.5.1.2"):
                    flow_type = "TCP"
                elif (int(ipc) >= 5):
                    flow_type = "TCP"
                elif (ip_addr == "10.1.1.1" or ip_addr == "10.1.2.2"):
                    flow_type = "UDP-EF"
                elif (ip_addr == "10.1.1.2" or ip_addr == "10.1.3.2"):
                    flow_type = "UDP-Default"
                else:
                    print("ip_addr %s not mapped to flow" % ip_addr)
                    sys.exit(1)
                cm_delay_samples += [(flow_type, float(delay_sample))]
            f.close()
        filename = 'latency-CMTS' + id + '.dat'
        with open(filename) as f:
            for line in f:
                delay_sample,ip_addr =line.split()[1:3]
                ipa,ipb,ipc,ipd=ip_addr.split('.')
                if (int(ipc) == 1 and int(ipd) >= 3):
                    flow_type = "TCP"
                elif (ip_addr == "10.5.1.2"):
                    flow_type = "TCP"
                elif (int(ipc) >= 5):
                    flow_type = "TCP"
                elif (ip_addr == "10.1.1.1" or ip_addr == "10.1.2.2"):
                    flow_type = "UDP-EF"
                elif (ip_addr == "10.1.1.2" or ip_addr == "10.1.3.2"):
                    flow_type = "UDP-Default"
                else:
                    print("ip_addr %s not mapped to flow" % ip_addr)
                    sys.exit(1)
                cmts_delay_samples += [(flow_type, float(delay_sample))]
            f.close()
    cm_delay_samples.sort(key=itemgetter(1))
    with open('cm-total.dat', 'w') as of:
        for sample in cm_delay_samples:
            of.write("%s %s\n" % sample)
    of.close()
    cmts_delay_samples.sort(key=itemgetter(1))
    with open('cmts-total.dat', 'w') as of:
        for sample in cmts_delay_samples:
            of.write("%s %s\n" % sample)
    of.close()
    return cm_delay_samples, cmts_delay_samples

def process_files_for_rtt_cdf(cm_delay_samples, cmts_delay_samples, selected_flow_types, colors):
    """Generate RTT CDF plot based on provided latency samples.

    Generates a 'rtt-cdf.jpg' and 'rtt-cdf.pdf' figure for report inclusion
    based on RTT estimation for each flow type (using convolution of
    CM and CMTS delay value samples for each flow type).

    Also returns the ninety-ninth percentile estimate of the RTT CDF of the
    first of the selected flowtypes, for use in the summary table.

    Args:
        cm_delay_samples:  A list of tuples of CM flowtype,delaysample pairs
        cmts_delay_samples:  A list of tuples of CMTS flowtype,delaysample pairs
        selected_flow_types:  A list of flow types to process
        colors:  A list of plot line colors to use (pairwise with flow types)

    Returns:
        Ninety-ninth percentile estimate of the RTT CDF of the first of the
        selected flowtypes (for use in the summary table).

    """
    global BIN_WIDTH
    ninety_ninth_percentiles = []
    percentiles_lists = []
    cm_delay_sample_lists = []
    cmts_delay_sample_lists = []
    rtt_cdf_lists = []
    maxUpstreamLatency = 0
    maxDownstreamLatency = 0
    for selected_flow_type in selected_flow_types:
        selected_cm_samples = []
        selected_cmts_samples = []
        columns_rtt = selected_flow_types
        rows_rtt = ['99.9$^{th}$ Percentile']
        rows_rtt += ['%d$^{th}$ Percentile'% x for x in (99,95,90,50,10,1)]
        rows_rtt += ['Minimum']
        for (ftype, sample) in cm_delay_samples:
            if ftype == selected_flow_type:
                selected_cm_samples.append (float(sample))
        for (ftype, sample) in cmts_delay_samples:
            if ftype == selected_flow_type:
                selected_cmts_samples.append (float(sample))
        cm_delay_sample_lists.append (selected_cm_samples)
        cmts_delay_sample_lists.append (selected_cmts_samples)
        maxUpstreamLatency = max(maxUpstreamLatency, max(selected_cm_samples))
        maxDownstreamLatency = max(maxDownstreamLatency, max(selected_cmts_samples))
    # Histogram bin width (input data in units of ms: 0.001 corresponds to 1 us)
    # Calculate the range of each pmf vector, corresponding to the bin width
    BIN_WIDTH = max(BIN_WIDTH, round(max(maxUpstreamLatency,maxDownstreamLatency)/10000,3))
    us_pmf_size = int(round (maxUpstreamLatency/BIN_WIDTH + 1))
    ds_pmf_size = int(round (maxDownstreamLatency/BIN_WIDTH + 1))
    pmf_max_size = max(us_pmf_size, ds_pmf_size)

    plt.figure(0)
    for index, elem in enumerate(cm_delay_sample_lists):
        cm_delay_sample_list = cm_delay_sample_lists[index]
        cmts_delay_sample_list = cmts_delay_sample_lists[index]
        flow_color = colors[index]
        pmf_latency_vector=np.arange(0, (pmf_max_size + 1)) * BIN_WIDTH
        us_pmf, us_edges=np.histogram(cm_delay_sample_list, bins=pmf_latency_vector, density=True)
        # Scale by bin width to obtain pmf
        us_pmf = us_pmf * BIN_WIDTH
        ds_pmf, ds_edges= np.histogram(cmts_delay_sample_list, bins=pmf_latency_vector, density=True)
        ds_pmf = ds_pmf * BIN_WIDTH

        rtt_pmf=np.convolve(us_pmf, ds_pmf)
        rtt_cdf=np.cumsum(rtt_pmf)
        rtt_cdf_lists.append(rtt_cdf)
        rtt_cdf = rtt_cdf[:int(RTT_CDF_XLIM/BIN_WIDTH)]
        cdf_latency_vector=np.arange(0, len(rtt_cdf)) * BIN_WIDTH
        plt.plot(cdf_latency_vector.tolist(), rtt_cdf.tolist(), color=flow_color, label = selected_flow_types[index])
        percentiles = np.interp([0.999, 0.99, 0.95, 0.90, 0.50, 0.10, 0.01, 0], rtt_cdf.tolist(), cdf_latency_vector.tolist())
        percentiles_lists.append ([ '%.3f' % elem for elem in percentiles ])
        ninety_ninth_percentiles.append (percentiles_lists[0][1])
    if (len(selected_flow_types) == 1):
        resultant=list(zip(percentiles_lists[0]))
        the_table=plt.table(cellText=resultant,colWidths = [0.24]*2,rowLabels=rows_rtt,colLabels=columns_rtt,loc='lower right')
    elif (len(selected_flow_types) == 2):
        resultant=list(zip(percentiles_lists[0], percentiles_lists[1]))
        the_table=plt.table(cellText=resultant,colWidths = [0.24]*3,rowLabels=rows_rtt,colLabels=columns_rtt,loc='lower right')
    elif (len(selected_flow_types) == 3):
        resultant=list(zip(percentiles_lists[0], percentiles_lists[1], percentiles_lists[2]))
        the_table=plt.table(cellText=resultant,colWidths = [0.24]*3,rowLabels=rows_rtt,colLabels=columns_rtt,loc='lower right')
    the_table.set_fontsize(8)
    the_table.scale(0.8, 1)
    plt.legend(loc="center right")
    plt.xlabel('Latency in ms')
    plt.ylabel('CDF of RTT')
    plt.ylim([0, 1])
    plt.xlim([0, RTT_CDF_XLIM])
    plt.title("CDF of RTT, all scenarios combined", size=10)
    plt.savefig('rtt-cdf.pdf', format='pdf')
    plt.savefig('rtt-cdf.jpg', format='jpg')
    plt.close()
    # Generate CCDF of first selected flow
    plt.figure(1)
    plt.xlabel('Latency in ms')
    plt.ylabel('Complementary CDF of RTT for %s' % selected_flow_types[0])
    plt.yscale('log')
    selected_rtt_cdf = rtt_cdf_lists[0].tolist()
    selected_rtt_ccdf = [ (1 - float(x)) for x in selected_rtt_cdf]
    # Find the necessary length to represent the CCDF down to RTT_CCDF_YLIM
    for index, elem in enumerate(selected_rtt_ccdf):
        if (elem < RTT_CCDF_YLIM):
            break
        length_for_ylim = index
    del selected_rtt_ccdf[length_for_ylim:]
    cdf_latency_vector=np.arange(0, len(selected_rtt_ccdf)) * BIN_WIDTH
    # Save CCDF data
    ccdf_output_file = open('ccdf.dat', 'w')
    for cbin, elem in list(zip(cdf_latency_vector, selected_rtt_ccdf)):
        ccdf_output_file.write("%s %s\n" % (cbin, elem))
    ccdf_output_file.close()
    # plot CCDF
    try:
        plt.plot(cdf_latency_vector.tolist(), selected_rtt_ccdf, color='green', label = 'UDP-EF')
        plt.grid(True)
        plt.title("CCDF of RTT, flow type: %s" % selected_flow_types[0], size=10)
        plt.savefig('rtt-ccdf.pdf', format='pdf')
        plt.savefig('rtt-ccdf.jpg', format='jpg')
        plt.close()
    except ValueError:
        print("ValueError on generating ccdf plot; plot will be omitted")
        pass
    return ninety_ninth_percentiles[0]


## MAIN ##

summary_files = []
if os.path.isdir('temp'):
    for file in os.listdir('temp'):
        if fnmatch.fnmatch(file, 'summary*.dat'):
            summary_files.append(file)
if len(summary_files) == 0:
    print("No summary files found; exiting...")
    sys.exit(1)
# alphanumeric sort from 'summary1.dat' ... 'summaryN.dat'
summary_files.sort(key=lambda x: int(os.path.splitext(x)[0][7:]))

c = canvas.Canvas("summary.pdf", pagesize=letter)
c.setTitle(sys.argv[1])
# canvas locations are a pair of numbers, starting at lower left corner
# and moving rightwards and upwards.  For instance, (4*inch,1*inch) is the
# point 4 inches rightward of the left margin, and 1 inch above bottom margin
stringVpos=10*inch
c.drawString(0.5*inch, stringVpos, sys.argv[1])

# Build summary table
os.chdir('temp')
scenario_ids = []
rows=1
total_duration = 0
total_grant = 0
total_transmit = 0
total_wasted = 0
total_eff = 0
data= [['', 'Duration', 'Granted\n Mbps', 'Transmitted\n Mbps', 'Wasted\n Mbps', 'Efficiency', 'P99 US\n Latency (ms)',  'P99 DS\n Latency (ms)','P99 DOCSIS\n RTT (ms)']]
for filename in summary_files:
    with open(filename) as f:
        p = re.compile('summary(\w+).dat')
        m = p.match(filename)
        scenario_ids.append(m.group(1))
        l = f.read().split()
        total_duration += float(l[0])
        total_grant += float(l[1]) * float(l[0])
        total_transmit += float(l[2]) * float(l[0])
        total_wasted += float(l[3]) * float(l[0])
        total_eff += float(l[4]) * float(l[0])
        l.insert(0, 'Scenario'+m.group(1))
        data.append(l)
        rows += 1

cm_samples, cmts_samples = prepare_latency_data_for_rtt_cdf(scenario_ids)

# Determine if there is UDP EF data present; if not, only use UDP BE data
udp_ef_found = False
selected_flow_types = []
flow_colors = []
# Insert UDP-EF into the list first, if present
for (ftype, sample) in cm_samples:
    if ftype == "UDP-EF":
        udp_ef_found = True
        selected_flow_types.append("UDP-EF")
        flow_colors.append('green')
        break
# Always insert UDP-Default and TCP.  If UDP-EF is not present, then the
# table will show 99th percentile total for the UDP-Default instead
selected_flow_types.append("UDP-Default")
flow_colors.append('orange')
selected_flow_types.append("TCP")
flow_colors.append('blue')

# calculate US P99 & DS P99
if udp_ef_found:
    us_ef_latency=sorted([elem[1] for elem in cm_samples if elem[0]=="UDP-EF"]) # extract & sort UDP-EF latency values
    ds_ef_latency=sorted([elem[1] for elem in cmts_samples if elem[0]=="UDP-EF"]) # extract & sort UDP-EF latency values
    us_ef_p99=np.interp([.99], np.linspace(0.0,1.0,len(us_ef_latency)),us_ef_latency) # find P99 value
    ds_ef_p99=np.interp([.99], np.linspace(0.0,1.0,len(ds_ef_latency)),ds_ef_latency) # find P99 value


total = ['Total']
total.append(total_duration)
total.append("%.2f" % float (total_grant / total_duration))
total.append("%.2f" % float (total_transmit / total_duration))
total.append("%.2f" % float (total_wasted / total_duration))
total.append("%.2f" % float (total_transmit / total_grant))
total.append("%.2f" % float (us_ef_p99)) # placeholder for total P99 US latency
total.append("%.2f" % float (ds_ef_p99)) # placeholder for total P99 DS latency
# The next method generates rtt-cdf figures and also returns 99th percentile
# of the first flow type in the list (typically, 'EF'), for the summary table
rtt_99th_percentile = process_files_for_rtt_cdf(cm_samples, cmts_samples, selected_flow_types, flow_colors)
total.append("%.2f" % float (rtt_99th_percentile))
data.append(total)
rows += 1

rowHeights = rows * [0.3*inch]
rowHeights[0] = 0.4*inch
t=Table(data, None, rowHeights=rowHeights)
t.setStyle(TableStyle([('ALIGN',(0,0),(-1,0),'LEFT'),
                       ('ALIGN',(1,0),(-1,-1),'RIGHT'),
                       ('INNERGRID', (0,0), (-1,-1), 0.25, colors.black),
                       ('BOX', (0,0), (-1,-1), 0.25, colors.black),
                      ]))
# Locate the table on the canvas
tableVpos=stringVpos - 0.25*inch - sum(rowHeights)
t.wrapOn(c, 0.5*inch, tableVpos)
t.drawOn(c, 0.5*inch, tableVpos)

# Add images if present
imgAspect=float(288)/float(243)
imgHeight=3.375*inch
imgWidth=imgHeight*imgAspect
imgVpos=tableVpos -0.25*inch - imgHeight
try:
    # To display only a single image, uncomment the below and comment the others
    # c.drawImage("rtt-cdf.jpg", 0.5*inch, 0.5*inch, width=480, height=360)
    c.drawImage("rtt-cdf.jpg", 0.5*inch, imgVpos, width=imgWidth, height=imgHeight)
    c.drawImage("rtt-ccdf.jpg", 4.25*inch, imgVpos, width=imgWidth, height=imgHeight)
except IOError:
    pass
# write the pdf
c.showPage()
os.chdir('..')
c.save()
