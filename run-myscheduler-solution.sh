#!/bin/bash

URL="https://secure.csse.uwa.edu.au/run/myscheduler-sample"
#
usage() {
    echo "Usage: $0 [-v] sysconfig-file command-file"
    exit 1
}
#
vv=""
if [ "$1" == "-v" ];
then
    vv="-F run-verbose=y"
    shift
fi
#
if [ $# != 2 ];
then
    usage
fi
#
sys="$1"
cmd="$2"
#
if [ ! -r "$sys" ];
then
    echo "$0: cannot read $sys"
    usage
fi
if [ ! -r "$cmd" ];
then
    echo "$0: cannot read $cmd"
    usage
fi

curl $(echo $vv) -F "file-sysconfig=@$sys" -F "file-commands=@$cmd" $URL
