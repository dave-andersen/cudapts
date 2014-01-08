#!/bin/bash

if [ -f "/etc/redhat-release" ]; then
	linuxdist="redhat"
elif [ -f "/etc/debian_version" ]; then
	linuxdist="debian"
else
	linuxdist="unknown"
fi

echo $linuxdist

exit 0
