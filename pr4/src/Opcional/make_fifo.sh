#!/usr/bin/env bash

module="fifodev"
device="fifodev"
mode="666"

if [[ -c /dev/${device} ]]; then
    rm -rf /dev/${device}
fi

major=$(awk -v module=${module} '$2 == module {print $1}' /proc/devices)

mknod /dev/${device} c ${major} 0

chmod $mode /dev/${device}
