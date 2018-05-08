#!/usr/bin/env bash

if [[ $EUID > 0 ]]; then
    exec sudo -E "$0" "$@"
fi

base=$(dirname $0)

APP="$base/build-root/install-vpp_debug-native/vpp/bin/vpp"
ARGS="-c $base/startup.conf"

USAGE="Usage: run.sh [ debug ]
       debug:	executes vpp under gdb"

if [ "$#" -gt 1 ]; then
	echo "Usage: run.sh [ <startup_conf> ]"
	exit 1
fi

if [ "$#" -eq 1 ]; then
	ARGS="-c $base/$1"
fi

$APP $ARGS

#if [ -z "$1" ]; then
#    $APP $ARGS
#elif [ "$1" == "debug" ]; then
#     GDB_EX="-ex 'set print pretty on' "
#     gdb $GDB_EX --args $APP $ARGS
#else
#	echo "$USAGE"
#fi
