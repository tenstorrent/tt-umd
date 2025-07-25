#pragma once

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {
class WormholeJTAGTTDevice : public TTDevice {
public:
    WormholeJTAGTTDevice(std::shared_ptr<JTAGDevice> jtag_device);

    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    void wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms = 1000) override;

    uint32_t get_clock() override;

    uint32_t get_max_clock_freq() override;

    uint32_t get_min_clock_freq() override;

    uint64_t get_board_id() override;

    bool get_noc_translation_enabled() override;

    std::vector<DramTrainingStatus> get_dram_training_status() override;

    void dma_d2h(void *dst, uint32_t src, size_t size) override;

    void dma_h2d(uint32_t dst, const void *src, size_t size) override;

    void dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) override;

    void dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) override;

    ChipInfo get_chip_info() override;
}
}