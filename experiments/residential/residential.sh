#!/bin/bash
#
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
#
# Authors:
#   Greg White <g.white@cablelabs.com>
#   Tom Henderson <tomh@tomh.org>

# Assumes that this file is four levels down from the top-level ns-3
# directory; e.g. contrib/docsis/experiments/residential
pathToTopLevelDir="../../../.."
# detect if we are under the contrib/ or src/ directory
srcdir=$(cd ../../../ && echo ${PWD##*/})
# Set library path to find ns-3 libraries
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:`pwd`/${pathToTopLevelDir}/build/lib
# Set various configuration options
export heading="Residential"
export downstreamMsr=200Mbps
export upstreamMsr=50Mbps
export guaranteedGrantRate=5Mbps
export linkDelayWebServer=4ms
export ftpStartTime=100ms
export numUdpEfUp=1
export numUdpBeUp=1
export numUdpEfDown=1
export numUdpBeDown=1
export enableTrace=false
export enablePcap=false
export saveDatFiles=false
export RngRun=1
export queueDepthTime=250ms
numSims=8 # number of simultaneous simulations to run.
	  # Set this less than or equal to the number of cores on your machine
# `nproc` is not a command on macOS; use `sysctl -n hw.ncpu`
#if ((numSims > `nproc`)); then
#	echo "Error exit:  numSims greater than number of CPU cores"
#	exit 1	
#fi

# If a directory name (label) is provided, create the appropriate results directory, copy all necessary scripts (including this one)
# into that directory, cd there, and launch the copy of this script that lives there (using the -L flag), then exit.
if [ "$1" != "-L" ]
then
	dirname=$1
	if [ -z ${dirname} ]
	then
		echo "Error exit:  'dirname' argument to this script is unset"
		echo "Usage: $0 dirname"
		exit 1	
	fi

	./waf build
	resultsDir=results/$dirname-`date +%Y%m%d-%H%M%S`
	mkdir -p ${resultsDir}
	repositoryVersion=`git rev-parse --abbrev-ref HEAD`
	repositoryVersion+=' commit '
	repositoryVersion+=`git rev-parse --short HEAD`
	repositoryVersion+=' '
	repositoryVersion+=`git log -1 --format=%cd`
	echo $repositoryVersion > ${resultsDir}/version.txt
	gitDiff=`git diff`
	if [[ $gitDiff ]]
	then
		echo "$gitDiff" >> ${resultsDir}/version.txt
	fi
	PROFILE=$(./waf --check-profile | tail -1 | awk '{print $NF}')
	VERSION=$(cat ${pathToTopLevelDir}/VERSION | tr -d '\n')
	EXECUTABLE_NAME=ns${VERSION}-residential-example-${PROFILE}
	EXECUTABLE=${pathToTopLevelDir}/build/${srcdir}/docsis/examples/${EXECUTABLE_NAME}
	if [ -f "$EXECUTABLE" ]; then
		cp ${EXECUTABLE} ${resultsDir}/residential-example
	else
		echo "$EXECUTABLE not found, exiting"
		exit 1
	fi
	cp ${pathToTopLevelDir}/${srcdir}/docsis/examples/residential-example.cc ${resultsDir}/.
	cp plot-latency.py ${resultsDir}/.
	cp plot-http.py ${resultsDir}/.
	cp plot-grants-unused.py ${resultsDir}/.
	cp plot-grants-unused-bandwidth.py ${resultsDir}/.
	cp plot-queue-latency.py ${resultsDir}/.
	cp summary-report.py ${resultsDir}/.
	cp $0 ${resultsDir}/.
	cd ${resultsDir}
	mkdir temp

	./${0##*/} -L $1 $2 >commandlog.out &  # launch the copy of this script in the background
 
 	echo "***************************************************************"
	echo "* Launched:  ${resultsDir}/${0##*/}"
	echo "* Output in:  $resultsDir/commandlog.out"
	echo "* Kill this run with:  kill -SIGTERM -`ps h -o pgid -q $!`"  
 	echo "***************************************************************"
	echo
	exit 0
fi
shift
export summaryHeader="results directory: $1, ggr: $guaranteedGrantRate"

function run-scenario () { 

	# process function arguments
	scenario_id=${1}
	numTcpDownloads=${2}
	numTcpUploads=${3}
	numTcpDashStreams=${4}
	numDctcpDownloads=${5}
	numDctcpUploads=${6}
	numDctcpDashStreams=${7}
	numWebUsers=${8}
	linkDelayTcpStart=${9}
	linkDelayTcpStep=${10}
	fileModel=${11}
	simulationEndTime=${12}


	echo starting scenario $scenario_id
	fileNameCm=temp/latency-CM${scenario_id}.dat
	fileNameCmts=temp/latency-CMTS${scenario_id}.dat
	fileNamePageLoad=temp/pageLoadTime${scenario_id}.dat
	fileNameGrantsUnused=temp/grantsUnused${scenario_id}.dat
	fileNameUnusedBandwidth=temp/unusedBandwidth${scenario_id}.dat
	fileNameTcpFileCompletion=temp/tcpFileCompletion${scenario_id}.dat
	fileNameDctcpFileCompletion=temp/dctcpFileCompletion${scenario_id}.dat
	fileNameCQueueDownstreamBytes=temp/cQueueDownstreamBytes${scenario_id}.dat
	fileNameLQueueDownstreamBytes=temp/lQueueDownstreamBytes${scenario_id}.dat
	fileNameCQueueUpstreamBytes=temp/cQueueUpstreamBytes${scenario_id}.dat
	fileNameLQueueUpstreamBytes=temp/lQueueUpstreamBytes${scenario_id}.dat
	fileNameCQueueDropProbability=temp/cQueueDropProbability${scenario_id}.dat
	fileNameLQueueMarkProbability=temp/lQueueMarkProbability${scenario_id}.dat
	fileNameCGrantState=temp/cGrantState${scenario_id}.dat
	fileNameLGrantState=temp/lGrantState${scenario_id}.dat
	fileNameTcpPacketTrace=temp/tcpPacketTrace${scenario_id}.dat
	fileNameCmDrop=temp/cmDrop${scenario_id}.dat
	fileNameCmtsDrop=temp/cmtsDrop${scenario_id}.dat
	fileNameSummary=temp/summary${scenario_id}.dat
	FilePcap=CM_CPE_${scenario_id}

	pdfNameCm=temp/cm${scenario_id}.pdf
	pdfNameCmts=temp/cmts${scenario_id}.pdf
	pdfNameRtt=temp/rtt${scenario_id}.pdf
	imageNameRtt=temp/rtt${scenario_id}.jpg
	pdfNamePageLoad=temp/pageLoadTime${scenario_id}.pdf
	pdfNameGrantsUnused=temp/grantsUnused${scenario_id}.pdf
	pdfNameUnusedBandwidth=temp/unusedBandwidth${scenario_id}.pdf
	pdfNameThroughput=temp/fileThroughput${scenario_id}.pdf
	resultsfile=results${scenario_id}.pdf
	logfile=log${scenario_id}.out
	summaryFiles="$summaryFiles ${fileNameSummary}"

	./residential-example \
		--upstreamMsr=$upstreamMsr \
		--downstreamMsr=$downstreamMsr \
		--guaranteedGrantRate=$guaranteedGrantRate \
		--numTcpDownloads=$numTcpDownloads \
		--numTcpUploads=$numTcpUploads \
		--numTcpDashStreams=$numTcpDashStreams \
		--numDctcpDownloads=$numDctcpDownloads \
		--numDctcpUploads=$numDctcpUploads \
		--numDctcpDashStreams=$numDctcpDashStreams \
		--numWebUsers=$numWebUsers \
		--numUdpEfUp=$numUdpEfUp \
		--numUdpEfDown=$numUdpEfDown \
		--numUdpBeUp=$numUdpBeUp \
		--numUdpBeDown=$numUdpBeDown \
		--tcpDelayStart=$linkDelayTcpStart \
		--tcpDelayStep=$linkDelayTcpStep \
		--linkDelayWebServer=$linkDelayWebServer \
		--simulationEndTime=$simulationEndTime \
		--fileNameCm=${fileNameCm} \
		--fileNameCmts=${fileNameCmts} \
		--fileNamePageLoad=${fileNamePageLoad} \
		--fileNameGrantsUnused=${fileNameGrantsUnused} \
		--fileNameUnusedBandwidth=${fileNameUnusedBandwidth} \
		--fileNameTcpFileCompletion=${fileNameTcpFileCompletion} \
		--fileNameDctcpFileCompletion=${fileNameDctcpFileCompletion} \
		--fileNameTcpPacketTrace=${fileNameTcpPacketTrace} \
		--fileNameCQueueUpstreamBytes=${fileNameCQueueUpstreamBytes} \
		--fileNameLQueueUpstreamBytes=${fileNameLQueueUpstreamBytes} \
		--fileNameCQueueDownstreamBytes=${fileNameCQueueDownstreamBytes} \
		--fileNameLQueueDownstreamBytes=${fileNameLQueueDownstreamBytes} \
		--fileNameCQueueDropProbability=${fileNameCQueueDropProbability} \
		--fileNameLQueueMarkProbability=${fileNameLQueueMarkProbability} \
		--fileNameCGrantState=${fileNameCGrantState} \
		--fileNameLGrantState=${fileNameLGrantState} \
		--fileNameCmDrop=${fileNameCmDrop} \
		--fileNameCmtsDrop=${fileNameCmtsDrop} \
		--fileNameSummary=${fileNameSummary} \
		--ftpStartTime=$ftpStartTime \
		--fileModel=${fileModel} \
		--queueDepthTime=${queueDepthTime} \
		--enableTrace=${enableTrace} \
		--enablePcap=${enablePcap} \
		--fileNamePcap=${FilePcap} \
		--RngRun=${RngRun} \
		> $logfile  2>&1

	python3 plot-latency.py ${numTcpDownloads} ${numTcpUploads} ${numTcpDashStreams} ${numDctcpDownloads} ${numDctcpUploads} ${numDctcpDashStreams} ${numWebUsers} ${heading} ${simulationEndTime} --fileNameCm=${fileNameCm} --fileNameCmts=${fileNameCmts} --plotNameCm=${pdfNameCm} --plotNameCmts=${pdfNameCmts} --plotNameRtt=${pdfNameRtt} --imageNameRtt=${imageNameRtt} --fileNameSummary=${fileNameSummary} --scenarioId=${scenario_id} >/dev/null  &
	
	if [ -s ${fileNamePageLoad} ]
	then
		python3 plot-http.py ${heading} ${simulationEndTime} --fileName=${fileNamePageLoad} --plotName=${pdfNamePageLoad} >/dev/null &
	fi
	if [ $guaranteedGrantRate != 0 ]
	then
		python3 plot-grants-unused.py ${heading} ${simulationEndTime} --fileName=${fileNameGrantsUnused} --plotName=${pdfNameGrantsUnused} >/dev/null &
		python3 plot-grants-unused-bandwidth.py ${heading} ${simulationEndTime} --fileName=${fileNameUnusedBandwidth} --plotName=${pdfNameUnusedBandwidth} >/dev/null &
	fi

	titleCm=${fileNameCm%*.dat}
	titleCm=${titleCm#temp/}
	./plot-queue-latency.py $fileNameCm ${titleCm} ${fileNameCm%*.dat}.pdf &
	titleCmts=${fileNameCmts%*.dat}
	titleCmts=${titleCmts#temp/}
	./plot-queue-latency.py $fileNameCmts ${titleCmts} ${fileNameCmts%*.dat}.pdf &
	wait

	if [ $guaranteedGrantRate == 0 ]
	then
		pdfNameGrantsUnused=""
		pdfNameUnusedBandwidth=""
	fi
	if ! [ -s ${fileNamePageLoad} ]
	then
		pdfNamePageLoad=""
	fi
	if hash PDFconcat 2>/dev/null; then
		PDFconcat -o ${resultsfile} ${pdfNameRtt} ${pdfNameCm} ${pdfNameCmts} ${pdfNamePageLoad} ${pdfNameGrantsUnused} ${pdfNameUnusedBandwidth}
	elif hash pdftk 2>/dev/null; then
		pdftk ${pdfNameRtt} ${pdfNameCm} ${pdfNameCmts} ${pdfNamePageLoad} ${pdfNameGrantsUnused} ${pdfNameUnusedBandwidth} cat output ${resultsfile}
	elif hash pdfunite 2>/dev/null; then
		pdfunite ${pdfNameRtt} ${pdfNameCm} ${pdfNameCmts} ${pdfNamePageLoad} ${pdfNameGrantsUnused} ${pdfNameUnusedBandwidth} ${resultsfile}
	else
		mv $pdfNameRtt .
		mv $pdfNameCm .
		mv $pdfNameCmts .
		mv $pdfNamePageLoad .
		mv $pdfNameThroughput .
		if $usePgs
		then
			mv $pdfNameGrantsUnused .
			mv $pdfNameUnusedBandwidth .
		fi
		echo "No pdf concatenation tool found."
	fi

	udrops=`cat ${fileNameCmDrop} |awk ' $6 == "b8" { print }' |wc -l` 
	ufwds=`cat ${fileNameCm} |awk ' $7 == "b8" { print }' |wc -l` 
	ddrops=`cat ${fileNameCmtsDrop} |awk ' $6 == "b8" { print }' |wc -l` 
	dfwds=`cat ${fileNameCmts} |awk ' $7 == "b8" { print }' |wc -l` 
	upercent=`echo "scale=5;100 * $udrops / ($udrops + $ufwds)" | bc`
	dpercent=`echo "scale=5;100 * $ddrops / ($ddrops + $dfwds)" | bc`
	tpercent=`echo "scale=5;100 * ($udrops + $ddrops)/ ($udrops + $ufwds + $ddrops + $dfwds)" | bc`
	echo "EF Traffic Up:" $udrops "drops," $ufwds "forwards. Pkt Loss Rate =" $upercent "%" >> $logfile
	echo "EF Traffic Down:" $ddrops "drops," $dfwds "forwards. Pkt Loss Rate =" $dpercent "%" >> $logfile
	echo "EF Traffic Total:" $(( $udrops + $ddrops )) "drops," $(( $ufwds + $dfwds )) "forwards. Pkt Loss Rate =" $tpercent "%" >> $logfile
	
	echo finished scenario $scenario_id
	
}
export -f run-scenario

# Scenarios: 
# 1.       Single gaming flow (UDP up/down) with default DSCP; Single gaming flow with DSCP=EF; single web user
# 2.       Same as 1, but add 1 Dash user
# 3.       Same as 1, but add 1 TCP upstream
# 4.       Same as 1, but add 1 TCP downstream
# 5.       Same as 1, but add 1 TCP upstream and 1 TCP downstream
# 6.       Same as 1, but add 5 TCP upstream and 5 TCP downstream
# 7.       Same as 1, but add 5 TCP upstream and 5 TCP downstream and 2 Dash users (start times staggered by 1 second)
# 8.       Same as 1, but 5 web users
# 9.       Gaming flow w/ DSCP-0, gaming flow w/ DSCP-EF; 16 downstream TCPs (speedtest)
# 10.      Gaming flow w/ DSCP-0, gaming flow w/ DSCP-EF; 8 upstream TCPs (speedtest)



#scenario arguments:  scenario_id numTcpDownloads numTcpUploads numTcpDashStreams numDctcpDownloads numDctcpUploads numDctcpDashStreams numWebUsers linkDelayTcpStart linkDelayTcpStep fileModel simulationEndTime
declare -a scenario=(\
#	S# TDn TUp DASH DDn DUp DDASH Web dStart dStep  fileModel simTime
	"1  0   0   0    0   0    0    1   10ms   10ms  empirical 1000s"
	"2  0   0   1    0   0    0    1   10ms   10ms  empirical 1000s"
	"3  0   1   0    0   0    0    1   10ms   10ms  empirical 100s"
	"4  1   0   0    0   0    0    1   10ms   10ms  empirical 100s"
	"5  1   1   0    0   0    0    1   10ms   10ms  empirical 100s"
	"6  5   5   0    0   0    0    1   10ms   10ms  empirical 100s"
	"7  5   5   2    0   0    0    1   10ms   10ms  empirical 100s"
	"8  0   0   0    0   0    0    5   10ms   10ms  empirical 1000s"
	"9  16  0   0    0   0    0    0   2ms    0ms   speedtest 100s"
	"10 0   8   0    0   0    0    0   2ms    0ms   speedtest 100s"
	)

# launch simulation scenarios using GNU Parallel if it is installed, otherwise use basic job control 
if hash parallel 2>/dev/null; then
	printf '%s\n' "${scenario[@]}" | parallel --no-notice --colsep '\s+' -u run-scenario {} 	
else
	for x in ${!scenario[@]}
	do
		run-scenario ${scenario[$x]} &
		[[ $(( (x+1) % numSims)) -eq 0 ]] && wait # pause every numSims until those jobs complete
	done
fi

wait

echo "producing summary report"
python3 summary-report.py "$summaryHeader" &

flist=""
for i in {1..100} # if there are ever more than 100 scenarios defined, this will need to change
do
	fname1="temp/latency-CM${i}.pdf"
	fname2="temp/latency-CMTS${i}.pdf"
	if [ -f ${fname1} -a -f ${fname2} ]; then
		flist="${flist} ${fname1} ${fname2}"
	fi
done

pdfjam $flist --no-landscape --nup 1x2 --outfile queue-delay.pdf --quiet
rm $flist

wait

drops=`cat temp/cm*Drop*.dat |awk ' $6 == "b8" { print }' |wc -l`
fwds=`cat temp/latency-CM*.dat  |awk ' $7 == "b8" { print }' |wc -l`
percent=`echo "scale=5;100 * $drops / ($drops + $fwds)" | bc`
echo "EF Traffic:" $drops "drops," $fwds "forwards. Pkt Loss Rate =" $percent "%" >> loss.txt

mv temp/ccdf.dat .

if ! $saveDatFiles
then
	rm -rf temp
fi

rm -f residential-example
exit
