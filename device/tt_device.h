#pragma once
#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <set>

#include "tt_soc_descriptor.h"
#include "tt_xy_pair.h"
#include "tt_silicon_driver_common.hpp"
#include "device/tt_cluster_descriptor_types.h"

namespace boost::interprocess{
    class named_mutex;
}

class PCIDevice;
class tt_ClusterDescriptor;

enum tt_DevicePowerState {
    BUSY,
    SHORT_IDLE,
    LONG_IDLE
};

enum tt_MutexType {
    LARGE_READ_TLB,
    LARGE_WRITE_TLB,
    SMALL_READ_WRITE_TLB,
    ARC_MSG
};

inline std::ostream &operator <<(std::ostream &os, const tt_DevicePowerState power_state) {
    switch (power_state) {
        case tt_DevicePowerState::BUSY: os << "Busy"; break;
        case tt_DevicePowerState::SHORT_IDLE: os << "SHORT_IDLE"; break;
        case tt_DevicePowerState::LONG_IDLE: os << "LONG_IDLE"; break;
        default: throw ("Unknown DevicePowerState");
    }
    return os;
}

struct tt_device_l1_address_params {
    std::int32_t NCRISC_FW_BASE = 0;
    std::int32_t FW_BASE = 0;
    std::int32_t TRISC0_SIZE = 0;
    std::int32_t TRISC1_SIZE = 0;
    std::int32_t TRISC2_SIZE = 0;
    std::int32_t TRISC_BASE = 0;
};

struct tt_driver_host_address_params {
    std::int32_t ETH_ROUTING_BLOCK_SIZE = 0;
    std::int32_t ETH_ROUTING_BUFFERS_START = 0;
};

struct tt_driver_eth_interface_params {
    std::int32_t NOC_ADDR_LOCAL_BITS = 0;
    std::int32_t NOC_ADDR_NODE_ID_BITS = 0;
    std::int32_t ETH_RACK_COORD_WIDTH = 0;
    std::int32_t CMD_BUF_SIZE_MASK = 0;
    std::int32_t MAX_BLOCK_SIZE = 0;
    std::int32_t REQUEST_CMD_QUEUE_BASE = 0;
    std::int32_t RESPONSE_CMD_QUEUE_BASE = 0;
    std::int32_t CMD_COUNTERS_SIZE_BYTES = 0;
    std::int32_t REMOTE_UPDATE_PTR_SIZE_BYTES = 0;
    std::int32_t CMD_DATA_BLOCK = 0;
    std::int32_t CMD_WR_REQ = 0;
    std::int32_t CMD_WR_ACK = 0;
    std::int32_t CMD_RD_REQ = 0;
    std::int32_t CMD_RD_DATA = 0;
    std::int32_t CMD_BUF_SIZE = 0;
    std::int32_t CMD_DATA_BLOCK_DRAM = 0;
    std::int32_t ETH_ROUTING_DATA_BUFFER_ADDR = 0;
    std::int32_t REQUEST_ROUTING_CMD_QUEUE_BASE = 0;
    std::int32_t RESPONSE_ROUTING_CMD_QUEUE_BASE = 0;
    std::int32_t CMD_BUF_PTR_MASK = 0;
};

struct tt_device_params {
    bool register_monitor = false;
    bool enable_perf_scoreboard = false;
    std::vector<std::string> vcd_dump_cores;
    std::vector<std::string> plusargs;
    bool init_device = true;
    bool early_open_device = false;
    bool skip_driver_allocs = false;
    int aiclk = 0;
    // The command-line input for vcd_dump_cores can have the following format:
    // {"*-2", "1-*", "*-*", "1-2"}
    // '*' indicates we must dump all the cores in that dimension.
    // This function takes the vector above and unrolles the coords with '*' in one or both dimensions.
    std::vector<std::string> unroll_vcd_dump_cores(tt_xy_pair grid_size) const {
        std::vector<std::string> unrolled_dump_core;
        for (auto &dump_core: vcd_dump_cores) {
            // If the input is a single *, then dump all cores.
            if (dump_core == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                for (size_t y = 0; y < grid_size.y; y++) {
                    std::string current_core_coord(std::to_string(x) + "-" + std::to_string(y));
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                        unrolled_dump_core.push_back(current_core_coord);
                    }
                }
                }
                continue;
            }
            // Each core coordinate must contain three characters: "core.x-core.y".
            assert(dump_core.size() <= 5);
            size_t delimiter_pos = dump_core.find('-');
            assert (delimiter_pos != std::string::npos); // y-dim should exist in core coord.

            std::string core_dim_x = dump_core.substr(0, delimiter_pos);
            size_t core_dim_y_start = delimiter_pos + 1;
            std::string core_dim_y = dump_core.substr(core_dim_y_start, dump_core.length() - core_dim_y_start);

            if (core_dim_x == "*" && core_dim_y == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                    for (size_t y = 0; y < grid_size.y; y++) {
                        std::string current_core_coord(std::to_string(x) + "-" + std::to_string(y));
                        if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                            unrolled_dump_core.push_back(current_core_coord);
                        }
                    }
                }
            } else if (core_dim_x == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                    std::string current_core_coord(std::to_string(x) + "-" + core_dim_y);
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                        unrolled_dump_core.push_back(current_core_coord);
                    }
                }
            } else if (core_dim_y == "*") {
                for (size_t y = 0; y < grid_size.y; y++) {
                    std::string current_core_coord(core_dim_x + "-" + std::to_string(y));
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                        unrolled_dump_core.push_back(current_core_coord);
                    }
                }
            } else {
                unrolled_dump_core.push_back(dump_core);
            }
        }
        return unrolled_dump_core;
    }

    std::vector<std::string> expand_plusargs() const {
        std::vector<std::string> all_plusargs {
            "+enable_perf_scoreboard=" + std::to_string(enable_perf_scoreboard),
            "+register_monitor=" + std::to_string(register_monitor)
        };

        all_plusargs.insert(all_plusargs.end(), plusargs.begin(), plusargs.end());

        return all_plusargs;
    }
};

class tt_device
{
    public:
    tt_device(const std::string& sdesc_path);
    virtual ~tt_device();

    // Setup/Teardown Functions
    virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
        throw std::runtime_error("---- tt_device::set_device_l1_address_params is not implemented\n");
    }

    virtual void set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_) {
        throw std::runtime_error("---- tt_device::set_driver_host_address_params is not implemented\n");
    }

    virtual void set_driver_eth_interface_params(const tt_driver_eth_interface_params& eth_interface_params_) {
        throw std::runtime_error("---- tt_device::set_driver_eth_interface_params is not implemented\n");
    }

    virtual void configure_tlb(chip_id_t logical_device_id, tt_xy_pair core, std::int32_t tlb_index, std::int32_t address, bool posted = true) {
        throw std::runtime_error("---- tt_device::configure_tlb is not implemented\n");
    }

    virtual void setup_core_to_tlb_map(std::function<std::int32_t(tt_xy_pair)> mapping_function) {
        throw std::runtime_error("---- tt_device::setup_core_to_tlb_map is not implemented\n");
    }

    virtual void start_device(const tt_device_params &device_params) {
        throw std::runtime_error("---- tt_device::start_device is not implemented\n");
    }

    virtual void deassert_risc_reset(int target_device) {
        throw std::runtime_error("---- tt_device::deassert_risc_reset is not implemented\n");
    }

    virtual void deassert_risc_reset_at_core(tt_cxy_pair core) {
        throw std::runtime_error("---- tt_device::deassert_risc_reset_at_core is not implemented\n");
    }

    virtual void assert_risc_reset(int target_device) {
        throw std::runtime_error("---- tt_device::assert_risc_reset is not implemented\n");
    }

    virtual void assert_risc_reset_at_core(tt_cxy_pair core) {
        throw std::runtime_error("---- tt_device::assert_risc_reset_at_core is not implemented\n");
    }

    virtual void clean_system_resources(){
        std::runtime_error("---- tt_device::clean_system_resources is not implemented\n");
    }

    virtual void close_device() {
        throw std::runtime_error("---- tt_device::close_device is not implemented\n");
    }

    // Runtime functions
    virtual void wait_for_non_mmio_flush() {
        throw std::runtime_error("---- tt_device::wait_for_non_mmio_flush is not implemented\n");
    }
    virtual void write_to_device(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

    virtual void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true) {
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

    virtual void rolled_write_to_device(uint32_t* mem_ptr, uint32_t len, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::rolled_write_to_device is not implemented\n");
    }

    virtual void rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
        throw std::runtime_error("---- tt_device::rolled_write_to_device is not implemented\n");
    }

    virtual void read_from_device(uint32_t* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

    virtual void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

    virtual void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::write_to_sysmem is not implemented\n");
    }

    virtual void read_from_sysmem(std::vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::read_from_sysmem is not implemented\n");
    }

    // Misc. Functions to Query/Set Device State
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors() {
        throw std::runtime_error("---- tt_device:get_virtual_soc_descriptors is not implemented\n");
    }
    virtual uint32_t get_harvested_noc_rows_for_chip(int logical_device_id){
        throw std::runtime_error("---- tt_device:get_harvested_noc_rows_for_chip is not implemented\n");
        return 0;
    }

    virtual bool using_harvested_soc_descriptors() {
        throw std::runtime_error("---- tt_device:using_harvested_soc_descriptors is not implemented\n");
        return 0;
    }
    
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors() {
        throw std::runtime_error("---- tt_device:get_harvesting_masks_for_soc_descriptors is not implemented\n");
    }

    virtual bool noc_translation_en() {
        throw std::runtime_error("---- tt_device:noc_translation_en is not implemented\n");
    }
    virtual int arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr) {
        throw std::runtime_error("---- tt_device::arc_msg is not implemented\n");
    }

    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {
        throw std::runtime_error("---- tt_device::translate_to_noc_table_coords is not implemented\n");
    }

    virtual int get_number_of_chips_in_cluster() {
        throw std::runtime_error("---- tt_device::get_number_of_chips_in_cluster is not implemented\n");
    }

    virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster() {
        throw std::runtime_error("---- tt_device::get_all_chips_in_cluster is not implemented\n");
    }

    virtual tt_ClusterDescriptor* get_cluster_description() {
        throw std::runtime_error("---- tt_device::get_cluster_description is not implemented\n");
    }

    virtual std::set<chip_id_t> get_target_mmio_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_mmio_device_ids is not implemented\n");
    }

    virtual std::set<chip_id_t> get_target_remote_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_remote_device_ids is not implemented\n");
    }

    virtual std::map<int,int> get_clocks() {
        throw std::runtime_error("---- tt_device::get_clocks is not implemented\n");
        return std::map<int,int>();
    }

    virtual uint32_t dma_allocation_size(chip_id_t src_device_id = -1) {
        throw std::runtime_error("---- tt_device::dma_allocation_size is not implemented\n");
        return 0;
    }

    virtual void *channel_0_address(std::uint32_t offset, std::uint32_t device_id) const {
        throw std::runtime_error("---- tt_device::channel_0_address is not implemented\n");
        return nullptr;
    }

    virtual void *host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
        throw std::runtime_error("---- tt_device::host_dma_address is not implemented\n");
        return nullptr;
    }

    const tt_SocDescriptor *get_soc_descriptor(chip_id_t chip) const;

    virtual std::optional<std::tuple<std::uint32_t, std::uint32_t>> describe_tlb(std::int32_t tlb_index) {
        throw std::runtime_error("---- tt_device::describe_tlb is not implemented\n");
    }

    std::optional<std::tuple<std::uint32_t, std::uint32_t>> describe_tlb(tt_xy_pair coord) {
        throw std::runtime_error("---- tt_device::describe_tlb is not implemented\n");
    }

    bool performed_harvesting = false;
    std::unordered_map<chip_id_t, uint32_t> harvested_rows_per_target = {};
    bool translation_tables_en = false;
    bool tlbs_init = false;

    protected:
    std::unordered_map<chip_id_t, tt_SocDescriptor> soc_descriptor_per_chip = {};
};

class c_versim_core;
namespace nuapi {namespace device {template <typename, typename>class Simulator;}}
namespace versim {
  struct VersimSimulatorState;
  using VersimSimulator = nuapi::device::Simulator<c_versim_core *, VersimSimulatorState>;
}

class tt_VersimDevice: public tt_device
{
    public:
    virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_);
     tt_VersimDevice(const std::string &sdesc_path, const std::string &ndesc_path);
     virtual std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors();
     virtual void start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool init_device, bool skip_driver_allocs);
     virtual void start_device(const tt_device_params &device_params);
     virtual void close_device();
     virtual void deassert_risc_reset(int target_device);
     virtual void deassert_risc_reset_at_core(tt_cxy_pair core);
     virtual void assert_risc_reset(int target_device);
     virtual void assert_risc_reset_at_core(tt_cxy_pair core);
     virtual void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true);
     virtual void rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
     virtual void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use);

     virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c);
     virtual bool using_harvested_soc_descriptors();
     virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
     virtual bool noc_translation_en();
     virtual std::set<chip_id_t> get_target_mmio_device_ids();
     virtual std::set<chip_id_t> get_target_remote_device_ids();
     virtual ~tt_VersimDevice();
     virtual tt_ClusterDescriptor* get_cluster_description();
     virtual int get_number_of_chips_in_cluster();
     virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster();
     static int detect_number_of_chips();
     virtual std::map<int,int> get_clocks();
    private:
    bool stop();
    tt_device_l1_address_params l1_address_params;
    versim::VersimSimulator* versim;
    std::shared_ptr<tt_ClusterDescriptor> ndesc;
    void* p_ca_soc_manager;
};

class tt_SiliconDevice: public tt_device
{
    public:
    // Constructor
    tt_SiliconDevice(const std::string &sdesc_path, const std::string &ndesc_path = "", const std::set<chip_id_t> &target_devices = {}, const uint32_t &num_host_mem_ch_per_mmio_device = 1, const std::unordered_map<std::string, std::int32_t>& dynamic_tlb_config_ = {}, const bool skip_driver_allocs = false, bool perform_harvesting = true);
    
    //Setup/Teardown Functions
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors();
    virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_);
    virtual void set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_);
    virtual void set_driver_eth_interface_params(const tt_driver_eth_interface_params& eth_interface_params_);
    virtual void configure_tlb(chip_id_t logical_device_id, tt_xy_pair core, std::int32_t tlb_index, std::int32_t address, bool posted = true);
    virtual void setup_core_to_tlb_map(std::function<std::int32_t(tt_xy_pair)> mapping_function);
    virtual void start_device(const tt_device_params &device_params);
    virtual void assert_risc_reset(int target_device);
    virtual void deassert_risc_reset(int target_device);
    virtual void deassert_risc_reset_at_core(tt_cxy_pair core);
    virtual void assert_risc_reset_at_core(tt_cxy_pair core);
    virtual void clean_system_resources();
    virtual void close_device();

    // Runtime Functions
    virtual void write_to_device(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true);
    virtual void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true);
    virtual void write_epoch_cmd_to_device(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool last_send_epoch_cmd);
    virtual void write_epoch_cmd_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool last_send_epoch_cmd);
    virtual void rolled_write_to_device(uint32_t* mem_ptr, uint32_t len, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb);
    virtual void rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
    virtual void read_from_device(uint32_t* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    virtual void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use);
    virtual void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void read_from_sysmem(std::vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    virtual void wait_for_non_mmio_flush();
    // These functions are used by Debuda, so make them public
    void bar_write32 (int logical_device_id, uint32_t addr, uint32_t data);
    uint32_t bar_read32 (int logical_device_id, uint32_t addr);

    // Misc. Functions to Query/Set Device State
    virtual uint32_t get_harvested_noc_rows_for_chip(int logical_device_id); // Returns one-hot encoded harvesting mask for PCIe mapped chips
    virtual int arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr);
    virtual bool using_harvested_soc_descriptors();
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
    virtual bool noc_translation_en();
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c);
    virtual int get_number_of_chips_in_cluster();
    virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster();
    virtual tt_ClusterDescriptor* get_cluster_description();
    static int detect_number_of_chips(bool respect_reservations = false);
    static std::vector<chip_id_t> detect_available_device_ids(bool respect_reservations = false, bool verbose = false);
    static std::vector<chip_id_t> get_available_devices_from_reservations(std::vector<chip_id_t> device_ids, bool verbose);
    static std::map<chip_id_t, chip_id_t> get_logical_to_physical_mmio_device_id_map(std::vector<chip_id_t> physical_device_ids);
    virtual std::set<chip_id_t> get_target_mmio_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual std::map<int,int> get_clocks();
    virtual uint32_t dma_allocation_size(chip_id_t src_device_id = -1);
    virtual void *channel_0_address(std::uint32_t offset, std::uint32_t device_id) const;
    virtual void *host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;
    virtual std::optional<std::tuple<std::uint32_t, std::uint32_t>> describe_tlb(std::int32_t tlb_index);
    std::optional<std::tuple<std::uint32_t, std::uint32_t>> describe_tlb(tt_xy_pair coord);
    static std::vector<int> extract_harvest_info_for_simulation(std::string harvest_info);
    static std::vector<int> extract_rows_to_remove(const tt::ARCH &arch, const int worker_grid_rows, const int harvested_rows);
    static void remove_worker_row_from_descriptor(tt_SocDescriptor& full_soc_descriptor, const std::vector<int>& row_coordinates_to_remove);
    static void harvest_rows_in_soc_descriptor(tt::ARCH arch, tt_SocDescriptor& sdesc, uint32_t harvested_rows);
    std::unordered_map<tt_xy_pair, tt_xy_pair> get_harvested_coord_translation_map(chip_id_t logical_device_id);
    // Destructor
    virtual ~tt_SiliconDevice ();

    private:
    // Helper functions
    // Startup + teardown
    void create_device(const std::unordered_set<chip_id_t> &target_mmio_device_ids, const uint32_t &num_host_mem_ch_per_mmio_device, const bool skip_driver_allocs);
    void init_system(const tt_device_params &device_params, const tt_xy_pair &grid_size);
    void start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool init_device, bool skip_driver_allocs);
    void broadcast_tensix_risc_reset(struct PCIdevice *device, const TensixSoftResetOptions &cores);
    void broadcast_remote_tensix_risc_reset(const chip_id_t &chip, const TensixSoftResetOptions &soft_resets);
    void set_remote_tensix_risc_reset(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets);
    void send_tensix_risc_reset_to_core(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets);
    void perform_harvesting_and_populate_soc_descriptors(const std::string& sdesc_path, const bool perform_harvesting);
    void init_pcie_iatus();
    void init_pcie_iatus_no_p2p();
    bool init_hugepage(chip_id_t device_id);
    bool init_dmabuf(chip_id_t device_id);
    void init_device(int device_id);
    void create_harvested_coord_translation(chip_id_t device_id, bool identity_map);
    bool init_dma_turbo_buf(struct PCIdevice* pci_device);
    bool uninit_dma_turbo_buf(struct PCIdevice* pci_device);
    static std::map<chip_id_t, std::string> get_physical_device_id_to_bus_id_map(std::vector<chip_id_t> physical_device_ids);
    void set_pcie_power_state(tt_DevicePowerState state);
    int set_remote_power_state(const chip_id_t &chip, tt_DevicePowerState device_state);
    void set_power_state(tt_DevicePowerState state);
    uint32_t get_power_state_arc_msg(tt_DevicePowerState state);
    void enable_local_ethernet_queue(const chip_id_t& chip, int timeout);
    void enable_ethernet_queue(int timeout);
    void enable_remote_ethernet_queue(const chip_id_t& chip, int timeout);
    void stop_remote_chip(const chip_id_t &chip);
    int open_hugepage_file(const std::string &dir, chip_id_t device_id, uint16_t channel);
    int iatu_configure_peer_region (int logical_device_id, uint32_t peer_region_id, uint64_t bar_addr_64, uint32_t region_size);
    uint32_t get_harvested_noc_rows (uint32_t harvesting_mask);
    uint32_t get_harvested_rows (int logical_device_id);
    int get_clock(int logical_device_id);
    bool stop();

    // Communication Functions
    void read_dma_buffer(std::vector<std::uint32_t> &mem_vector, std::uint32_t address, std::uint16_t channel, std::uint32_t size_in_bytes, chip_id_t src_device_id);
    void write_dma_buffer(std::vector<std::uint32_t> &mem_vector, std::uint32_t address, std::uint16_t channel, chip_id_t src_device_id);
    void write_device_memory(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair target, std::uint32_t address, const std::string& fallback_tlb);
    void read_device_memory(uint32_t *mem_ptr, tt_cxy_pair target, std::uint32_t address, std::uint32_t size_in_bytes, const std::string& fallback_tlb);
    void write_to_non_mmio_device(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t address);
    void write_to_non_mmio_device_send_epoch_cmd(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t address, bool last_send_epoch_cmd);
    void rolled_write_to_non_mmio_device(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t address, uint32_t unroll_count);
    void read_from_non_mmio_device(uint32_t* mem_ptr, tt_cxy_pair core, uint64_t address, uint32_t size_in_bytes);
    uint64_t get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint64_t offset);
    uint16_t get_sys_rack(uint32_t rack_x, uint32_t rack_y);
    bool is_non_mmio_cmd_q_full(uint32_t curr_wptr, uint32_t curr_rptr);
    int pcie_arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr);
    int remote_arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr);
    std::optional<std::uint64_t> get_tlb_data(std::uint32_t tlb_index, const TLB_DATA & data) const ;
    bool address_in_tlb_space(uint32_t address, uint32_t size_in_bytes, int32_t tlb_index, uint32_t tlb_size, uint32_t chip);
    struct PCIdevice* get_pci_device(int pci_intf_id) const;
    boost::interprocess::named_mutex* get_mutex(const std::string& tlb_name, int pci_interface_id);

    // Test functions
    int test_pcie_tlb_setup (struct PCIdevice* pci_device);
    int test_setup_interface ();
    int test_broadcast (int logical_device_id);

    // State variables
    tt_device_l1_address_params l1_address_params;
    tt_driver_host_address_params host_address_params;
    tt_driver_eth_interface_params eth_interface_params;
    std::vector<tt::ARCH> archs_in_cluster = {};
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    tt_SocDescriptor& get_soc_descriptor(chip_id_t chip_id);
    tt::ARCH arch_name;
    std::map<chip_id_t, struct PCIdevice*> m_pci_device_map;    // Map of enabled pci devices
    int m_num_pci_devices;                                      // Number of pci devices in system (enabled or disabled)
    std::shared_ptr<tt_ClusterDescriptor> ndesc;
    // Level of printouts. Controlled by env var TT_PCI_LOG_LEVEL
    // 0: no debugging messages, 1: less verbose, 2: more verbose
    int m_pci_log_level;

    // remote eth transfer setup
    static constexpr std::uint32_t NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 6;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 4;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_START_ID = 0;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_MASK = (NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS-1);

    static constexpr std::uint32_t EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS = NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS - NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_START_ID = NON_EPOCH_ETH_CORES_START_ID + NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_MASK = (EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS-1);

    int active_core = NON_EPOCH_ETH_CORES_START_ID;
    int active_core_epoch = EPOCH_ETH_CORES_START_ID;
    std::vector<std::uint32_t> erisc_q_ptrs_epoch;
    tt_cxy_pair remote_transfer_ethernet_cores[NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS];
    bool flush_non_mmio = false;
    // Size of the PCIE DMA buffer
    // The setting should not exceed MAX_DMA_BYTES
    std::uint32_t m_dma_buf_size;
    std::unordered_map<chip_id_t, bool> noc_translation_enabled_for_chip = {};
    std::map<std::string, std::map<int, std::pair<std::string, boost::interprocess::named_mutex*>>> m_per_device_mutexes_map;
    std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>> harvested_coord_translation = {};
    std::unordered_map<chip_id_t, std::uint32_t> num_rows_harvested = {};
    uint32_t m_num_host_mem_channels = 0;
    std::unordered_map<chip_id_t, std::unordered_map<int, void *>> hugepage_mapping;
    std::unordered_map<chip_id_t, std::unordered_map<int, std::size_t>> hugepage_mapping_size;
    std::unordered_map<chip_id_t, std::unordered_map<int, std::uint64_t>> hugepage_physical_address;
    std::map<chip_id_t, std::unordered_map<std::int32_t, std::int32_t>> tlb_config_map = {};
    std::function<std::int32_t(tt_xy_pair)> map_core_to_tlb;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {};
    std::uint64_t buf_physical_addr = 0;
    void * buf_mapping = nullptr;
    int driver_id;  
    bool perform_harvesting_on_sdesc = false;
};

tt::ARCH detect_arch(uint16_t device_id = 0);

struct tt_version {
    std::uint16_t major;
    std::uint8_t minor;
    std::uint8_t patch;

    tt_version(std::uint32_t version) {
        major = (version >> 16) & 0xff;
        minor = (version >> 12) & 0xf;
        patch = version & 0xfff;
    }
    std::string str() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }
};

constexpr inline bool operator==(const tt_version &a, const tt_version &b) {
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

