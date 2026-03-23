#!/bin/bash

# Use this to match available NIC devices with the devices listed with `ip link show`
# This will allow you to identity the NIC holding the IP allowing you to remotely
# connect. You do not want to attempt to bind this (management) interface, because
# it will break your ssh connection.

echo "Matching NICs with PCIe address"
echo -e "\n"

for iface in $(ls /sys/class/net/); do
    echo "$iface: $(ethtool -i $iface 2>/dev/null | grep bus-info)"
done