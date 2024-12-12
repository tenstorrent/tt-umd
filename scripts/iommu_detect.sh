#!/bin/bash

# Device IDs
TT_VID="1e52"
GS_PID="faca"
WH_PID="401e"
BH_PID="b140"

# Find all Tenstorrent PCI devices with full domain info
tt_devices=$(lspci -D -d ${TT_VID}: | cut -d' ' -f1)

if [ -z "$tt_devices" ]; then
    echo "No Tenstorrent devices found"
    exit 1
fi

found_gs_wh=false
found_bh=false

for dev in $tt_devices; do
    device_id=$(lspci -D -n -s "$dev" | cut -d' ' -f3 | cut -d: -f2)
    echo "Checking device $dev (ID: $device_id):"
    
    case $device_id in
        $GS_PID|$WH_PID)
            found_gs_wh=true
            if [ -f "/sys/bus/pci/devices/${dev}/iommu_group/type" ]; then
                iommu_type=$(cat "/sys/bus/pci/devices/${dev}/iommu_group/type")
                if [[ "$iommu_type" == *"DMA"* ]]; then
                    echo "  WARNING: Grayskull/Wormhole device with IOMMU enabled (type: $iommu_type) - this configuration is not supported"
                else
                    echo "  Grayskull/Wormhole device detected - hugepages required"
                fi
            else
                echo "  Grayskull/Wormhole device detected - hugepages required"
            fi
            ;;
        $BH_PID)
            found_bh=true
            if [ -f "/sys/bus/pci/devices/${dev}/iommu_group/type" ]; then
                iommu_type=$(cat "/sys/bus/pci/devices/${dev}/iommu_group/type")
                if [[ "$iommu_type" == *"DMA"* ]]; then
                    echo "  Blackhole device with IOMMU enabled (type: $iommu_type) - hugepages optional"
                else
                    echo "  Blackhole device with IOMMU in passthrough mode (type: $iommu_type) - hugepages required"
                fi
            else
                echo "  Blackhole device with no IOMMU configuration - hugepages required"
            fi
            ;;
        *)
            echo "  Unknown device ID: $device_id"
            ;;
    esac
done

echo -e "\nSummary:"
if [ "$found_gs_wh" = true ]; then
    echo "- System has Grayskull/Wormhole devices - hugepages required"
    echo "- IOMMU must be disabled or in passthrough mode"
elif [ "$found_bh" = true ]; then
    echo "- System has Blackhole devices - check IOMMU status above to determine if hugepages are needed"
fi
