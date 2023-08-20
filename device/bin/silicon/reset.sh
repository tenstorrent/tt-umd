#!/bin/bash -e
(return 0 2>/dev/null) && sourced=1 || sourced=0

if [ "$sourced" == "1" ]; then echo "Do not source this script. Execute it directly."; else
# Resetting board
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
DRIVER_VERSION=`modinfo tenstorrent --field=version`
DRIVER_VERSION_AS_NUM=`modinfo tenstorrent --field=version | tr -d ',.'`
NUM_CHIPS=`lspci -d 1e52: | wc -l`

echo "Resetting driver version=$DRIVER_VERSION (a.k.a $DRIVER_VERSION_AS_NUM). Num_chips=$NUM_CHIPS"

LAST_PIN_TO_TOGGLE=$((8 + NUM_CHIPS - 1))
PINS_TO_TOGGLE=$(seq -s " " 8 $LAST_PIN_TO_TOGGLE)

if [ -z $BOARD_HOSTNAME ]; then
HOSTNAME=`hostname`
else
HOSTNAME=$BOARD_HOSTNAME
fi

if [ ! -z $1 ]; then
TIMEOUT="--timeout $1"
fi

if [[ 13 -lt $DRIVER_VERSION_AS_NUM ]]; then
  sudo modprobe -r tenstorrent

  BUS_SLOT_FUN_ARRAY=$(lspci -n -d 1e52: | awk '{print $1}')

  if [ ${#BUS_SLOT_FUN_ARRAY[@]} -gt 0 ]; then
    BRIDGE_PATH_ARRAY=()

    for bsf in $BUS_SLOT_FUN_ARRAY; do
      PCI_DEVICE_PATH="/sys/bus/pci/devices/0000:$bsf"
      BRIDGE_PATH_ARRAY+=($(realpath $PCI_DEVICE_PATH/..))
      echo "Removing the device $PCI_DEVICE_PATH"
      echo 1 | sudo tee $PCI_DEVICE_PATH/remove
    done

    sudo $SCRIPTPATH/reset-chip $TIMEOUT $PINS_TO_TOGGLE --no-cfg-restore
    
    echo "Sleeping for 0.5 seconds"
    sleep 0.5

    for bridge_path in ${BRIDGE_PATH_ARRAY[@]}; do
      echo "Rescanning the bridge $bridge_path"
      echo 1 | sudo tee $bridge_path/rescan
    done
  else
    echo "No Tenstorrent device found"
  fi

  echo "Reloading driver"
  sudo modprobe tenstorrent
else
  echo Update Tenstorrent driver. You have an old driver $DRIVER_VERSION
fi
echo DONE

fi
:
