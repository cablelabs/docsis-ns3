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

pathToTopLevelDir="../../../.."
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:`pwd`/${pathToTopLevelDir}/build/lib

numSims=1 # number of simultaneous simulations to run.
	  # Set this less than or equal to the number of cores on your machine

# If a directory name (label) is provided, create the appropriate results directory, copy all necessary scripts (including this one)
# into that directory, cd there, and launch the copy of this script that lives there (using the -L flag), then exit.
if [ "$1" != "-L" ]
then
	dirname=$1
	if [ -z ${dirname} ]
	then
		dirname='results'
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
	EXECUTABLE_NAME=ns${VERSION}-simple-docsislink-${PROFILE}
	EXECUTABLE=${pathToTopLevelDir}/build/src/docsis/examples/${EXECUTABLE_NAME}
	if [ -f "$EXECUTABLE" ]; then
		cp ${EXECUTABLE} ${resultsDir}/simple-docsislink
	else
		echo "$EXECUTABLE not found, exiting"
		exit 1
	fi
	cp ${pathToTopLevelDir}/src/docsis/examples/simple-docsislink.cc ${resultsDir}/.
	cp plot-grants-sojourn.py ${resultsDir}/.
	cp $0 ${resultsDir}/.
	cd ${resultsDir}

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

function run-scenario () { 

	# process function arguments
	scenario_id=${1}
	numServiceFlows=${2}
        congestion=${3}
	freeCapacityVariation=${4}
	enableFtp=${5}
	udpRate=${6}
        name="${@:7}"

	echo starting scenario $scenario_id
	resultsfile=results${scenario_id}.pdf
	logfile=log${scenario_id}.out
	summaryFiles="$summaryFiles ${fileNameSummary}"

	mkdir scenario${scenario_id}
	cd scenario${scenario_id}

	../simple-docsislink \
		--numServiceFlows=$numServiceFlows \
		--congestion=$congestion \
		--freeCapacityVariation=$freeCapacityVariation \
		--enableFtp=$enableFtp \
		--udpRate=$udpRate \
		> $logfile  2>&1

	python3 ../plot-grants-sojourn.py "${name}" > /dev/null
	
	wait

	echo finished scenario $scenario_id
	
}
export -f run-scenario

# Scenarios: 


#scenario arguments:  scenario_id numServiceFlows congestion freeCapacityVariation enableFtp udpRate scenario_name
declare -a scenario=(\
#	S# NSF cong free ftp udpRate name 
	"1  2   0   0   0    5Mbps Two SF, no FTP, no dynamic BW"
	"2  2   0   0   1    5Mbps Two SF, FTP, no dynamic BW"
	"3  2   3   0   1    5Mbps Two SF, FTP, congestion model 3"
	"4  2   3   20  1    5Mbps Two SF, FTP, congestion model 3 with 20% variation"
	)

# launch simulation scenarios using GNU Parallel if it is installed, otherwise use basic job control 
if hash parallel; then
	printf '%s\n' "${scenario[@]}" | parallel --no-notice --colsep '\s+' -u run-scenario {} 	
else
	for x in ${!scenario[@]}
	do
		run-scenario "${scenario[$x]}" &
		[[ $(( (x+1) % numSims)) -eq 0 ]] && wait # pause every numSims until those jobs complete
	done
fi

wait

rm -f simple-docsislink
exit
