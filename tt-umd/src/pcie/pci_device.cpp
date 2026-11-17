// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/pci_device.hpp"

#include <tt-kmd-lib/pci_ids.h>

#include <string>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

tt::ARCH arch_from_pci_device_id(uint16_t device_id) {
    switch (device_id) {
        case TT_WORMHOLE_PCI_DEVICE_ID:
            return tt::ARCH::WORMHOLE_B0;
        case TT_BLACKHOLE_PCI_DEVICE_ID:
            return tt::ARCH::BLACKHOLE;
        case TT_QUASAR_PCI_DEVICE_ID:
            return tt::ARCH::QUASAR;
        default:
            return tt::ARCH::Invalid;
    }
}

tt::ARCH PciDeviceInfo::get_arch() const { return arch_from_pci_device_id(device_id); }

PCIDevice::PCIDevice(int pci_device_number) :
    device_path("/dev/tenstorrent/" + std::to_string(pci_device_number)),
    pci_device_num(pci_device_number),
    info{},
    numa_node(-1),
    revision(0),
    arch(tt::ARCH::Invalid),
    kmd_version{},
    iommu_enabled(false) {
    UMD_THROW(error::RuntimeError, "PCIDevice::PCIDevice is not implemented yet");
}

PCIDevice::~PCIDevice() { release_resources(); }

void PCIDevice::release_resources() {}

std::vector<int> PCIDevice::enumerate_devices() {
    UMD_THROW(error::RuntimeError, "PCIDevice::enumerate_devices is not implemented yet");
}

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    UMD_THROW(error::RuntimeError, "PCIDevice::enumerate_devices_info is not implemented yet");
}

std::optional<int> PCIDevice::get_pci_device_id(int umd_logical_id) {
    UMD_THROW(error::RuntimeError, "PCIDevice::get_pci_device_id is not implemented yet");
}

PciDeviceInfo PCIDevice::read_device_info(const std::string &device_path) {
    UMD_THROW(error::RuntimeError, "PCIDevice::read_device_info is not implemented yet");
}

bool PCIDevice::detect_iommu(const PciDeviceInfo &device_info) {
    UMD_THROW(error::RuntimeError, "PCIDevice::detect_iommu is not implemented yet");
}

std::optional<uint32_t> PCIDevice::get_current_link_width() const {
    UMD_THROW(error::RuntimeError, "PCIDevice::get_current_link_width is not implemented yet");
}

uint64_t PCIDevice::map_for_hugepage(void *buffer, size_t size) {
    UMD_THROW(error::RuntimeError, "PCIDevice::map_for_hugepage is not implemented yet");
}

std::pair<uint64_t, uint64_t> PCIDevice::map_buffer_to_noc(
    void *buffer, size_t size, DeviceBufferAccess device_access) {
    UMD_THROW(error::RuntimeError, "PCIDevice::map_buffer_to_noc is not implemented yet");
}

std::pair<uint64_t, uint64_t> PCIDevice::map_hugepage_to_noc(void *hugepage, size_t size) {
    UMD_THROW(error::RuntimeError, "PCIDevice::map_hugepage_to_noc is not implemented yet");
}

uint64_t PCIDevice::map_for_dma(void *buffer, size_t size, DeviceBufferAccess device_access) {
    UMD_THROW(error::RuntimeError, "PCIDevice::map_for_dma is not implemented yet");
}

bool PCIDevice::is_read_only_page_pinning_supported() const {
    UMD_THROW(error::RuntimeError, "PCIDevice::is_read_only_page_pinning_supported is not implemented yet");
}

void PCIDevice::unmap_for_dma(void *buffer, size_t size) {
    UMD_THROW(error::RuntimeError, "PCIDevice::unmap_for_dma is not implemented yet");
}

SemVer PCIDevice::read_kmd_version() {
    UMD_THROW(error::RuntimeError, "PCIDevice::read_kmd_version is not implemented yet");
}

SemVer PCIDevice::read_kernel_version() {
    UMD_THROW(error::RuntimeError, "PCIDevice::read_kernel_version is not implemented yet");
}

std::unique_ptr<TlbHandle> PCIDevice::allocate_tlb(
    const size_t tlb_size, const TlbMapping tlb_mapping, const bool verify_config) {
    UMD_THROW(error::RuntimeError, "PCIDevice::allocate_tlb is not implemented yet");
}

void PCIDevice::configure_tlb(const uint32_t tlb_index, const tlb_data &tlb_config, const bool verify) {
    UMD_THROW(error::RuntimeError, "PCIDevice::configure_tlb is not implemented yet");
}

uint8_t PCIDevice::read_command_byte(const int pci_device_num) {
    UMD_THROW(error::RuntimeError, "PCIDevice::read_command_byte is not implemented yet");
}

void PCIDevice::send_reset_ioctl_to_devices(
    const std::unordered_set<int> &pci_target_devices, TenstorrentResetDevice flag, bool ignore_failures) {
    UMD_THROW(error::RuntimeError, "PCIDevice::send_reset_ioctl_to_devices is not implemented yet");
}

tt::ARCH PCIDevice::get_pcie_arch() {
    UMD_THROW(error::RuntimeError, "PCIDevice::get_pcie_arch is not implemented yet");
}

bool PCIDevice::is_arch_agnostic_reset_supported() {
    UMD_THROW(error::RuntimeError, "PCIDevice::is_arch_agnostic_reset_supported is not implemented yet");
}

bool PCIDevice::is_tlb_dmabuf_export_supported() {
    UMD_THROW(error::RuntimeError, "PCIDevice::is_tlb_dmabuf_export_supported is not implemented yet");
}

int PCIDevice::export_tlb_dmabuf(size_t window_size, const tlb_data &config, uint64_t offset, uint64_t size) {
    UMD_THROW(error::RuntimeError, "PCIDevice::export_tlb_dmabuf is not implemented yet");
}

uint64_t PCIDevice::noc_read_scalar(uint64_t addr, uint32_t width, NocAddressSpace space) {
    UMD_THROW(error::RuntimeError, "PCIDevice::noc_read_scalar is not implemented yet");
}

void PCIDevice::noc_write_scalar(uint64_t addr, uint64_t value, uint32_t width, NocAddressSpace space) {
    UMD_THROW(error::RuntimeError, "PCIDevice::noc_write_scalar is not implemented yet");
}

void PCIDevice::set_power_state(bool busy) {
    UMD_THROW(error::RuntimeError, "PCIDevice::set_power_state is not implemented yet");
}

void PCIDevice::construct_resources() {
    UMD_THROW(error::RuntimeError, "PCIDevice::construct_resources is not implemented yet");
}

void PCIDevice::allocate_pcie_dma_buffer() {
    UMD_THROW(error::RuntimeError, "PCIDevice::allocate_pcie_dma_buffer is not implemented yet");
}

bool PCIDevice::try_allocate_pcie_dma_buffer_iommu(const size_t dma_buf_size) {
    UMD_THROW(error::RuntimeError, "PCIDevice::try_allocate_pcie_dma_buffer_iommu is not implemented yet");
}

bool PCIDevice::try_allocate_pcie_dma_buffer_no_iommu(const size_t dma_buf_size) {
    UMD_THROW(error::RuntimeError, "PCIDevice::try_allocate_pcie_dma_buffer_no_iommu is not implemented yet");
}

std::vector<int> PCIDevice::get_all_device_ids() {
    UMD_THROW(error::RuntimeError, "PCIDevice::get_all_device_ids is not implemented yet");
}

std::vector<int> PCIDevice::sort_ids_based_on_bdf(const std::vector<int> &pci_device_ids) {
    UMD_THROW(error::RuntimeError, "PCIDevice::sort_ids_based_on_bdf is not implemented yet");
}

std::map<std::string, int> PCIDevice::get_bdf_to_device_id_map() {
    UMD_THROW(error::RuntimeError, "PCIDevice::get_bdf_to_device_id_map is not implemented yet");
}

}  // namespace tt::umd
