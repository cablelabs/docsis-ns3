#!/bin/bash
#
# Copyright (c) 2017-2021 Cable Television Laboratories, Inc.
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

pathToTopLevelDir="../../../.."
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:`pwd`/${pathToTopLevelDir}/build/lib

# These variables are not set in the Bash array but apply across all
# scenarios defined in the Bash array
export simulationEndTime=31s
# Alternatives for the below are "QDelayCoupledL" or "QDelayCoupledV"
# This variable controls which estimator is actually used in the code
export estimator="QDelayCoupledV"
export mapInterval=1000us
# Periodic virtual queue interval; typically 1/2 of MAP interval
export vqInterval=500us
# Use maxDistance to control minimum request grant delay
# 1 corresponds to 11 frames of delay, 8=12 frames, 22=13, 35=14, 49=15, 62=16
export maxDistance=8
# PDU size for CBR traffic types
export pduSize=1000
# Congestion, if enabled, will occur during time 15-25s of simulation
# freeCapacityMean is FreeCapacityMean (data rate value), zero means disabled
export freeCapacityMean=0
# capVar is FreeCapacityVariation (percent), 0<capVar<100
export freeCapacityVariation=0

# Attributes for PHY configurations
# Below values are the ns-3 defaults; when MAP is configured to 1ms:
# MAP=945us; frames/MAP=7; slot size=48; slots/MAP=1645; minReqGntDelay=12
export numUsSc=1880
export symbolsPerFrame=6
export usSpectralEfficiency=10
export usCpLen=256
export numDsSc=3745
export dsCpLen=512

# Alternative PHY configuration 1
# When MAP is configured to 1ms:
# MAP=1050us; frames/MAP=3; slot size=76; slots/MAP=300; minReqGntDelay=7
#export numUsSc=800
#export symbolsPerFrame=16
#export usSpectralEfficiency=6
#export usCpLen=192
#export numDsSc=3680
#export dsCpLen=1024

# Alternative PHY configuration 2
# When MAP is configured to 1ms:
# MAP=720; frames/MAP=1; slot size=230; slots/MAP=62; minReqGntDelay=6
#export numUsSc=496
#export symbolsPerFrame=32
#export usSpectralEfficiency=9
#export usCpLen=256
#export numDsSc=3680
#export dsCpLen=1024

# Random variable run number
export RngRun=1
# Whether to echo commandlog to foreground (true) or save to file (false)
export foreground=0
numSims=4 # number of simultaneous simulations to run.
	  # Set this less than or equal to the number of cores on your machine

# Create a results directory, copy all necessary scripts (including this one)
# into that directory, cd there, and launch the copy of this script that lives there (using the -L flag), then exit.
if [ "$1" != "-L" ]
then
	dirname=$1
	if [ -z ${dirname} ]
	then
		dirname='results'
	fi

	${pathToTopLevelDir}/ns3 build
	resultsDir=results/$dirname-`date +%Y%m%d-%H%M%S`
	mkdir -p ${resultsDir}
	gitDiff=`git diff`
	repositoryVersion=`git rev-parse --abbrev-ref HEAD`
	repositoryVersion+=' commit: '
	repositoryVersion+=`git rev-parse --short HEAD`
	repositoryVersion+=' '
	if [[ $gitDiff ]]
	then
		repositoryVersion+='(dirty) '
	fi
	repositoryVersion+=`git log -1 --format=%cd`
	echo $repositoryVersion > ${resultsDir}/version.txt
	if [[ $gitDiff ]]
	then
		echo "$gitDiff" >> ${resultsDir}/version.txt
	fi
	PROFILE=$(${pathToTopLevelDir}/ns3 show profile | awk '{print $NF}')
	VERSION=$(cat ${pathToTopLevelDir}/VERSION | tr -d '\n')
	# Executable name varies whether it is in scratch or contrib directory
	EXECUTABLE_NAME=ns${VERSION}-delay-estimation-${PROFILE}
	EXECUTABLE=${pathToTopLevelDir}/build/contrib/docsis/examples/${EXECUTABLE_NAME}
	if [ -f "$EXECUTABLE" ]; then
		cp ${EXECUTABLE} ${resultsDir}/delay-estimation
	else
		# Try the src directory
		EXECUTABLE=${pathToTopLevelDir}/build/src/docsis/examples/${EXECUTABLE_NAME}
		if [ -f "$EXECUTABLE" ]; then
			cp ${EXECUTABLE} ${resultsDir}/delay-estimation
		else
			echo "$EXECUTABLE not found, exiting"
			exit 1
		fi
	fi
	PROGRAM=${pathToTopLevelDir}/contrib/docsis/examples/delay-estimation.cc 
	if [ -f "$PROGRAM" ]; then
		cp ${PROGRAM} ${resultsDir}/
	else
		# Try the src directory
		PROGRAM=${pathToTopLevelDir}/src/docsis/examples/delay-estimation.cc 
		if [ -f "$PROGRAM" ]; then
			cp ${PROGRAM} ${resultsDir}/
		else
			echo "$PROGRAM not found, but continuing"
		fi
	fi
	cp $0 ${resultsDir}/.
	cp plot-align-vq-trace.py ${resultsDir}/.
	cp plot-dequeue-trace.py ${resultsDir}/.
	cp plot-tcp-cwnd.py ${resultsDir}/.
	cp plot-tcp-throughput.py ${resultsDir}/.
	cp plot-tcp-rtt.py ${resultsDir}/.
	cp pdfunite.sh ${resultsDir}/.
	cd ${resultsDir}

	#./${0##*/} -L $1 $2 >commandlog.out &  # launch the copy of this script in the background
	if [[ "$foreground" -eq 1 ]]
	then	
		./${0##*/} -L $1 $2 &  # launch the copy of this script in the background
	else
		./${0##*/} -L $1 $2 >commandlog.out &  # launch the copy of this script in the background
	fi
 
 	echo "***************************************************************"
	echo "* Launched:  ${resultsDir}/${0##*/}"
	if [[ "$foreground" -eq 0 ]]
	then	
		echo "* Output in:  $resultsDir/commandlog.out"
	fi
	if [ "$(uname)" == "Linux" ]; then
		echo "* Kill this run with:  kill -SIGTERM -`ps h -o pgid -q $!`"  
	fi
 	echo "***************************************************************"
	echo
	exit 0
fi
shift

function run-scenario () { 

	# process function arguments
	scenario_id=${1}
	amsr=${2}
	peakRate=${3}
	ggr=${4}
	ggi=${5}
	weight=${6}
	lTraf=${7}
	cTraf=${8}
	dctcp=${9}

	echo starting scenario $scenario_id-${2}-${3}-${4}-${5}-${6}-${7}-${8}-${9}
	resultsfile=results${scenario_id}.pdf
	logfile=log${scenario_id}.out
	summaryFiles="$summaryFiles ${fileNameSummary}"

	mkdir scenario${scenario_id}
	cd scenario${scenario_id}
	/usr/bin/head -1 ../version.txt >> $logfile

	../delay-estimation \
		--ns3::docsis::DocsisNetDevice::MapInterval=$mapInterval \
		--estimator=${estimator} \
		--simulationEndTime=${simulationEndTime} \
		--upstreamAmsr=${amsr} \
		--upstreamPeakRate=${peakRate} \
		--guaranteedGrantRate=$ggr \
		--guaranteedGrantInterval=$ggi \
		--schedulingWeight=$weight \
		--ns3::docsis::DualQueueCoupledAqm::VqInterval=$vqInterval \
		--llRate=$lTraf \
		--classicRate=$cTraf \
		--pduSize=$pduSize \
		--enableDctcp=$dctcp \
		--freeCapacityMean=$freeCapacityMean \
		--freeCapacityVariation=$freeCapacityVariation \
		--ns3::docsis::DocsisNetDevice::NumUsSc=$numUsSc \
		--ns3::docsis::DocsisNetDevice::SymbolsPerFrame=$symbolsPerFrame \
		--ns3::docsis::DocsisNetDevice::UsSpectralEfficiency=$usSpectralEfficiency \
		--ns3::docsis::DocsisNetDevice::UsCpLen=$usCpLen \
		--ns3::docsis::DocsisNetDevice::NumDsSc=$numDsSc \
		--ns3::docsis::DocsisNetDevice::DsCpLen=$dsCpLen \
		--RngRun=$RngRun \
		>> $logfile  2>&1

	wait

	python3 ../plot-align-vq-trace.py $amsr
	python3 ../plot-dequeue-trace.py
	if [ $dctcp -ne "0" ]; then
		python3 ../plot-tcp-cwnd.py
		python3 ../plot-tcp-throughput.py
		python3 ../plot-tcp-rtt.py
        fi

	echo finished scenario $scenario_id
	
}
export -f run-scenario

# Scenarios: 

#scenario arguments:  scenario_id AMSR PeakRate GGR GGI Weight Ltraf Ctraf size capM capV 
# - AMSR refers to the upstream AMSR; downstream AMSR is fixed at 200 Mbps
# - PeakRate refers to the upstream Peak Rate
# - GGR is expressed as a data rate value (with units); zero means disabled
# - GGI is expressed as a microsecond value; zero defaults to one frame interval
# - Weight is an integer 1<weight<255
# - Ltraf and Ctraf are data rate values for LL and Classic UDP traffic
# - DCTCP is boolean (0 or 1) that configures absence/presence of DCTCP
#
# The following parameters are set globally in above bash variables
# - MAP interval
# - simulationEndTime 
# - estimator (QDelayCoupledL or QDelayCoupledV)
# - vqInterval
# 
# The expected result from a well-functioning VQ is that the queue delay
# estimate in the L queue remains at the minimum value (1 packet) for 
# scenarios 1-4, and that in scenarios 5 and 6 it grows at a slow rate.
# Similarly, for scenarios 7-12, only scenario 12 should experience queue
# growth.


# set the following variables consistently with the array declaration
# but without units (so they can be used in formulas)
export amsr=50
export peakRate=100
export ggi=1000
export weight=230

declare -a scenario=(\
#	S# AMSR     PeakRate GGR      GGI   Weight Ltraf Ctraf  DCTCP
# Best effort, L traffic near AMSR rate
	"1   50Mbps 100Mbps 0         0   230   49.9Mbps 0      0   "
# Best effort, mix of C and L queue traffic balanced based on sched. weight
	"2   50Mbps 100Mbps 0         0   230   44.9Mbps 5Mbps  0   "
# Scenario 2 but for weight of 128 
	"3   50Mbps 100Mbps 0         0   128   24.9Mbps 25Mbps 0   "
# Total 52.2 (not 50.1 due to scheduler overage)-- only c-queue buildup
	"4   50Mbps 100Mbps 0         0   230   44.9Mbps 7.3Mbps 0   "
# Total 52.2 (not 50.1 due to scheduler overage)-- only l-queue buildup
	"5   50Mbps 100Mbps 0         0   230   52.2Mbps 0      0   "
# Slightly over allocation for the L queue (compare with 4)
	"6   50Mbps 100Mbps 0         0   230   47.085Mbps  7.3Mbps 0 "
# Add PGS scenarios with gradually increasing GGR
# The smallest GGR (one minislot/MAP interval) is scenario 7 (387Kbps)
	"7   50Mbps 100Mbps 387000bps  1000   230   49.9Mbps 0      0  "
        "8   50Mbps 100Mbps 10Mbps  1000   230   49.9Mbps 0      0   "
	"9   50Mbps 100Mbps 20Mbps  1000   230   49.9Mbps 0      0   "
	"10  50Mbps 100Mbps 40Mbps  1000   230   49.9Mbps 0      0   "
	"11  50Mbps 100Mbps 49.9Mbps 1000  230   49.9Mbps 0      0   "
# Repeat scenario 6 but with PGS enabled
	"12  50Mbps 100Mbps 20Mbps  1000  230   47.085Mbps 7.3Mbps 0   "
# Repeat selected scenarios with DCTCP traffic as capacity-seeking flow
# Scenario 101 matches scenario 1 (best effort granting) but with DCTCP
	"101   50Mbps 100Mbps 0         0   230   0       0      1     "
# Best effort, mix of C and DCTCP traffic balanced based on sched. weight
	"102   50Mbps 100Mbps 0         0   230   0       5Mbps  1     "
# Scenario 103 matches scenario 3 (equal amount of L/C traffic) but with DCTCP
	"103   50Mbps 100Mbps 0         0   128   0       25Mbps 1   "
# Scenario 107 matches scenario 7 (minimum GGR) but with DCTCP
	"107   50Mbps 100Mbps 387000bps 1000   230   0       0      1     "
# Scenario 109 matches scenario 9 (40% GGR) but with DCTCP
	"109   50Mbps 100Mbps 20Mbps  1000   230  0       0      1     "
# Scenario 111 matches scenario 11 (full GGR) but with DCTCP
	"111   50Mbps 100Mbps 49.9Mbps  1000   230   0       0      1     "
	)

# Initial Scenarios
# 
#     LL=49.9, C=0 (total 49.9) no queue build up
#     LL=44.9, C=5 (total 49.9) no queue build up
#     LL=24.9, C=25 (total 49.9) no queue build up
#     LL=47.085, C=7.3 (total 54.4, only c-queue build up)
#     LL=52.2, C=0 (total 52.2 only L-queue build up)


# launch simulation scenarios using GNU Parallel if it is installed, otherwise use basic job control 
if hash parallel; then
	printf '%s\n' "${scenario[@]}" | parallel --no-notice --colsep '\s+' -u run-scenario {} 	
else
	for x in ${!scenario[@]}
	do
		run-scenario ${scenario[$x]} &
		[[ $(( (x+1) % numSims)) -eq 0 ]] && wait # pause every numSims until those jobs complete
	done
fi

wait

# Write summary statistics to summary.dat file
# The first field is the scenario ID.  The remaining fields are taken from
# the last line of log$id.out file
directories=`ls -1 -v | grep scenario`
for x in ${directories}
do
        id=`awk -v var="$x" 'BEGIN {print substr(var, 9) }'`
        printf "%s %6s %6s %6s %6s %6s %6s %6s" $id $amsr $peakRate $ggi $weight $freeCapacityMean $freeCapacityVariation $mapInterval  >> summary.txt
        tail -1 scenario$id/log$id.out >> summary.txt
done

if [[ "$foreground" -eq 1 ]]
then	
	for x in ${!scenario[@]}
	do
		id=`expr $x + 1`
        	echo "scenario $id" 
		tail -6 "scenario$id/log$id.out"
	done
fi

# Run pdfunite wrapper
./pdfunite.sh

rm -f delay-estimation
exit
