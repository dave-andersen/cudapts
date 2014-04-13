#!/bin/bash

if [ -f "/etc/redhat-release" ]; then
	linuxdist="redhat"
    flavor=$(awk '{print $1}' /etc/redhat-release)
    if [ "$flavor" == "Fedora" ]; then
        linuxdist="fedora"
    fi
elif [ -f "/etc/debian_version" ]; then
	linuxdist="debian"
else
	linuxdist="unknown"
fi

echo $linuxdist

exit 0
