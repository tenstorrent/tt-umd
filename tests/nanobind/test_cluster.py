# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd  # Import the nanobind Python module

class TestCluster(unittest.TestCase):
    def test_cluster_creation(self):
        cluster = tt_umd.Cluster()  # Create a Cluster instance
        self.assertIsNotNone(cluster)
        
        cluster_descriptor = cluster.create_cluster_descriptor("", {})
        print("Cluster descriptor:", cluster_descriptor)
        for chip in cluster_descriptor.get_all_chips():
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")
                
        print("All chips but local first: ", cluster_descriptor.get_chips_local_first(cluster_descriptor.get_all_chips()))
        
        for chip in cluster_descriptor.get_all_chips():
            print(f"Chip id {chip} has arch {cluster_descriptor.get_arch(chip)}")

    def test_cluster_functionality(self):
        cluster = tt_umd.Cluster()
        target_device_ids = cluster.get_target_device_ids()
        print("Cluster device IDs:", target_device_ids)
        clocks = cluster.get_clocks()
        print("Cluster clocks:", clocks)

    def test_low_level_tt_device(self):
        dev_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", dev_ids)
        if (len(dev_ids) == 0):
            print("No PCI devices found.")
            return

        dev = tt_umd.TTDevice.create(dev_ids[0])
        print(f"TTDevice id {dev_ids[0]} has arch {dev.get_arch()} and board id {dev.get_board_id()}")
        pci_dev = dev.get_pci_device()
        pci_info = pci_dev.get_device_info().get_pci_bdf()
        print("So the pci bdf is ", pci_info)

    def test_read_low_level_tt_device(self):
        dev_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", dev_ids)
        if (len(dev_ids) == 0):
            print("No PCI devices found.")
            return
        dev = tt_umd.TTDevice.create(dev_ids[0])
        print(f"TTDevice id {dev_ids[0]} has arch {dev.get_arch()} and board id {dev.get_board_id()}")
        # val = bytearray(4) # A mutable byte buffer of 4 bytes
        # dev.read_from_device(val, tt_umd.tt_xy_pair(9, 0), 0, 4)
        val2 = dev.noc_read32(9, 0, 0)
        print("Read value from device, core 9,0 addr 0x0: ", val2)
        
class TestTelemetry(unittest.TestCase):
    def test_telemetry(self):
        dev_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", dev_ids)
        if (len(dev_ids) == 0):
            print("No PCI devices found.")
            return
    
        dev = tt_umd.TTDevice.create(dev_ids[0])
        tel_reader = dev.get_arc_telemetry_reader()
        tag = int(tt_umd.wormhole.TelemetryTag.ASIC_TEMPERATURE)
        print("Telemetry reading for asic temperature: ", tel_reader.read_entry(tag))
        
    def test_remote_telemetry(self):
        cluster_descriptor = tt_umd.Cluster.create_cluster_descriptor("", {})
        umd_local_chips = {}
        umd_tt_devices = {}
        tag = int(tt_umd.wormhole.TelemetryTag.ASIC_TEMPERATURE)
        chip_to_mmio_map = cluster_descriptor.get_chips_with_mmio()
        chip_eth_coords = cluster_descriptor.get_chip_locations()
        for chip in cluster_descriptor.get_chips_local_first(cluster_descriptor.get_all_chips()):
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
                umd_tt_devices[chip] = tt_umd.TTDevice.create(chip_to_mmio_map[chip])
                # For some reason when we give out a TTDevice to LocalChip and get it back it doesn't work.
                # So just create a separate one for LocalChip
                tt_dev = tt_umd.TTDevice.create(chip_to_mmio_map[chip])
                umd_local_chips[chip] = tt_umd.LocalChip(tt_dev)
                umd_local_chips[chip].set_remote_transfer_ethernet_cores(cluster_descriptor.get_active_eth_channels(chip))
                tel_reader = umd_tt_devices[chip].get_arc_telemetry_reader()
                print(f"Telemetry reading for chip {chip} ASIC temperature: ", tel_reader.read_entry(tag))
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")
                umd_tt_devices[chip] = tt_umd.RemoteWormholeTTDevice(umd_local_chips[closest_mmio], chip_eth_coords[chip])
                tel_reader = umd_tt_devices[chip].get_arc_telemetry_reader()
                print(f"Telemetry reading for remote chip {chip} ASIC temperature: ", tel_reader.read_entry(tag))


if __name__ == "__main__":
    unittest.main()
