
#include "umd/device/chip_helpers/simulation_sysmem_buffer.hpp"
#include <memory>
#include "umd/device/chip_helpers/tlb_manager.hpp"


namespace tt::umd{

SimulationSysmemBuffer::SimulationSysmemBuffer(void* buffer_va, size_t buffer_size) {
    buffer_va_ = buffer_va;
    buffer_size_ = buffer_size;
    mapped_buffer_size_ = buffer_size;
    offset_from_aligned_addr_ = 0;
    
    // For simulation: device_io_addr is just the host VA since there's no real DMA
    device_io_addr_ = reinterpret_cast<uint64_t>(buffer_va);
    noc_addr_ = std::nullopt;
    tlb_manager_ = nullptr;
    cached_tlb_window = nullptr;
}

}
