#!/bin/bash -e
(return 0 2>/dev/null) && sourced=1 || sourced=0

if [ "$sourced" == "1" ]; then echo "Do not source this script. Execute it directly."; exit; fi

# Resetting board
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
DRIVER_VERSION=`modinfo tenstorrent --field=version`
DRIVER_VERSION_AS_NUM=`modinfo tenstorrent --field=version | tr -d ',.'`
NUM_CHIPS=`lspci -d 1e52: | wc -l`

echo "Resetting driver version=$DRIVER_VERSION (a.k.a $DRIVER_VERSION_AS_NUM). Num_chips=$NUM_CHIPS"

# Go through all inputs and put them into an array
MOBOS=("$@")

# If no inputs, then exit out
if [ ${#MOBOS[@]} -eq 0 ]; then
  echo "No mobos specified, please specify at least 1 mobo"
  exit 1
fi

# Go through all the mobos and turn their modules off at the same time
for MOBO in "${MOBOS[@]}"; do
  echo "Powering off modules on $MOBO"
  curl ${MOBO}:8000/shutdown/modules -u admin:admin -X POST -d '{"group": null}' &
done
wait
sleep 5

# Go through all the mobos and turn their modules on at the same time
for MOBO in "${MOBOS[@]}"; do
  echo "Powering on modules on $MOBO"
  curl ${MOBO}:8000/boot/modules -u admin:admin -X POST -d '{"group": null}' &
done
wait

# Rescan the PCI bus
echo 1 |sudo tee /sys/bus/pci/devices/0000:$(lspci -d 1e52: | awk '{print $1}')/remove && echo 1 |sudo tee /sys/bus/pci/rescan