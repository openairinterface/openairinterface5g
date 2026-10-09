#!/bin/bash
# SPDX-License-Identifier: LicenseRef-CSSL-1.0
# Run all Open5GS NFs in one container; exit as soon as any NF exits

cfg=/opt/open5gs/etc/open5gs

# UPF N6 interface: open5gs-upfd does not configure it
ip tuntap add name ogstun mode tun 2>/dev/null
ip addr replace 12.1.1.1/24 dev ogstun || exit 1
ip link set ogstun up || exit 1

trap 'kill $(jobs -p) 2>/dev/null; wait; exit 0' TERM INT

open5gs-nrfd -c $cfg/nrf.yaml &
sleep 1
for nf in ausf udm udr pcf bsf nssf upf smf amf; do
  open5gs-${nf}d -c $cfg/$nf.yaml &
done

wait -n
echo "an Open5GS NF exited, stopping all NFs"
kill $(jobs -p) 2>/dev/null
wait
exit 1
