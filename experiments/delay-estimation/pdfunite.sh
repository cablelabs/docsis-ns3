#!/bin/bash

# This will bind the individual scenario PDFs into two files in increasing 
# order of the scenario number.
# This must be run from the directory containing scenario* subdirectories
# and the system must have 'pdfunite' installed

if ! [ -x "$(command -v pdfunite)" ]; then
  echo 'Error: pdfunite is not installed or found on the path.' >&2
  exit 1
fi

LIST=($(find . -name "scenario*" | sed -e "s/\.\/scenario//" | sort -n))

if [ -z "$LIST" ]; then
  echo 'Error: scenario directory not found; run from a results directory.' >&2
  exit 1
fi

scenarios=""
cwndFiles=""
throughputFiles=""
rttFiles=""
for i in "${LIST[@]}"; do
   scenarios="${scenarios} scenario$i/align-vq-trace-$i.pdf" 
   if [ -e "scenario$i/tcp-cwnd-trace-$i.pdf" ]; then 
        cwndFiles="${cwndFiles} scenario$i/tcp-cwnd-trace-$i.pdf"
   fi
   if [ -e "scenario$i/tcp-throughput-trace-$i.pdf" ]; then 
        throughputFiles="${throughputFiles} scenario$i/tcp-throughput-trace-$i.pdf"
   fi
   if [ -e "scenario$i/tcp-rtt-trace-$i.pdf" ]; then 
        rttFiles="${rttFiles} scenario$i/tcp-rtt-trace-$i.pdf"
   fi
done
pdfunite $scenarios align-vq-trace.pdf
if [ ! -z "$cwndFiles" ]; then
    pdfunite $cwndFiles tcp-cwnd-trace.pdf
fi
if [ ! -z "$throughputFiles" ]; then
    pdfunite $throughputFiles tcp-throughput-trace.pdf
fi
if [ ! -z "$rttFiles" ]; then
    pdfunite $rttFiles tcp-rtt-trace.pdf
fi
