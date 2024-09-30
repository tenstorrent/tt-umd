/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "chip.h"
#include "tt_device.h"

#include <optional>

namespace boost::interprocess{
    class named_mutex;
}

namespace tt::umd {

/**
 * @brief Silicon Driver Class, derived from the tt_device class
 * Implements APIs to communicate with a physical Tenstorrent Device.
 */
class LocalChip : public Chip {
   public:
    // Constructor
    /**
     * @brief Silicon Driver constructor.
     * \param sdesc_path Location of the SOC descriptor containing the default description of a single chip in the
     * cluster (this does not have to account for product level changes such as harvesting). \param ndesc_path Location
     * of the Network Descriptor specifying the network topology of the system. \param target_devices Logical Device ids
     * being targeted by workload. \param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages)
     * per MMIO mapped device. The driver may allocated less per device, depending on availability. \param
     * dynamic_tlb_config_ Map specifying dynamic tlb names and the indices they correspond to \param skip_driver_allocs
     * Specifies if the Silicon Driver object should be initialized + started without modifying device state (ex:
     * bringing device out of reset or shared host state (ex: initializing hugepages) \param clean_system_resource
     * Specifies if potentially corrupted shared host state from previous runs needs to be cleaned up. Should only be
     * set by the main thread/process running on a device. Setting this across multiple processes per device will cause
     * issues since objects required by the driver will be cleared. \param perform_harvesting Allow the driver to modify
     * the SOC descriptors per chip by considering the harvesting configuration of the cluster.
     */
    LocalChip(const std::string &sdesc_path, 
        const std::string &ndesc_path, 
        const std::set<chip_id_t>& target_devices = {},
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const std::unordered_map<std::string, std::int32_t>& dynamic_tlb_config_ = {},
        const bool skip_driver_allocs = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {});

    // Setup/Teardown Functions
    virtual std::unordered_map<chip_id_t, SocDescriptor>& get_virtual_soc_descriptors();
    virtual void set_device_l1_address_params(const device_l1_address_params& l1_address_params_);
    virtual void set_device_dram_address_params(const device_dram_address_params& dram_address_params_);
    virtual void set_driver_host_address_params(const driver_host_address_params& host_address_params_);
    virtual void set_driver_eth_interface_params(const driver_eth_interface_params& eth_interface_params_);
    virtual void configure_tlb(
        chip_id_t logical_device_id,
        xy_pair core,
        std::int32_t tlb_index,
        std::int32_t address,
        uint64_t ordering = tlb_data::Posted);
    virtual void set_fallback_tlb_ordering_mode(const std::string& fallback_tlb, uint64_t ordering = tlb_data::Posted);
    virtual void setup_core_to_tlb_map(std::function<std::int32_t(xy_pair)> mapping_function);
    virtual void configure_active_ethernet_cores_for_mmio_device(
        chip_id_t mmio_chip, const std::unordered_set<xy_pair>& active_eth_cores_per_chip);
    virtual void start_device(const device_params& device_params);
    virtual void assert_risc_reset();
    virtual void deassert_risc_reset();
    virtual void deassert_risc_reset_at_core(cxy_pair core);
    virtual void assert_risc_reset_at_core(cxy_pair core);
    virtual void close_device();

    // Runtime Functions
    virtual void write_to_device(
        const void* mem_ptr, uint32_t size_in_bytes, cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
    virtual void write_to_device(
        std::vector<uint32_t>& vec, cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude,
        const std::string& fallback_tlb);

    virtual void read_from_device(
        void* mem_ptr, cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    virtual void read_from_device(
        std::vector<uint32_t>& vec, cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use);
    virtual void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void write_to_sysmem(
        const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void read_from_sysmem(
        std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    virtual void read_from_sysmem(
        void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    virtual void wait_for_non_mmio_flush();
    void l1_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<xy_pair>& cores = {});
    void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels);
    void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<xy_pair>& cores = {});
    // These functions are used by Debuda, so make them public
    void bar_write32(int logical_device_id, uint32_t addr, uint32_t data);
    uint32_t bar_read32(int logical_device_id, uint32_t addr);
    /**
     * @brief If the tlbs are initialized, returns a tuple with the TLB base address and its size
     */
    std::optional<std::tuple<uint32_t, uint32_t>> get_tlb_data_from_target(const xy_pair& target);
    /**
     * @brief This API allows you to write directly to device memory that is addressable by a static TLB
     */
    std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable(int device_id);

    /**
     * @brief Provide fast write access to a statically-mapped TLB.
     * It is the caller's responsibility to ensure that
     * - the target has a static TLB mapping configured.
     * - the mapping is unchanged during the lifetime of the returned object.
     * - the tt_SiliconDevice instance outlives the returned object.
     * - use of the returned object is congruent with the target's TLB setup.
     * @param target The target chip and core to write to.
     * @throws std::runtime_error on error.
     * @returns a Writer instance that can be used to write to the target.
     */
    Writer get_static_tlb_writer(cxy_pair target);

    // Misc. Functions to Query/Set Device State
    virtual int arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);
    virtual bool using_harvested_soc_descriptors();
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c);
    virtual int get_number_of_chips_in_cluster();
    virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster();
    virtual ClusterDescriptor* get_cluster_description();
    static int detect_number_of_chips();
    static std::vector<chip_id_t> detect_available_device_ids();
    virtual std::set<chip_id_t> get_target_mmio_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual std::map<int, int> get_clocks();
    virtual void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;
    virtual std::uint64_t get_pcie_base_addr_from_device() const;
    static std::vector<int> extract_rows_to_remove(
        const Arch& arch, const int worker_grid_rows, const int harvested_rows);
    static void remove_worker_row_from_descriptor(
        SocDescriptor& full_soc_descriptor, const std::vector<int>& row_coordinates_to_remove);
    static void harvest_rows_in_soc_descriptor(Arch arch, SocDescriptor& sdesc, uint32_t harvested_rows);
    static std::unordered_map<xy_pair, xy_pair> create_harvested_coord_translation(
        const Arch arch, bool identity_map);
    std::unordered_map<xy_pair, xy_pair> get_harvested_coord_translation_map(chip_id_t logical_device_id);
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id);
    virtual std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id);
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id);
    virtual tt_version get_ethernet_fw_version() const;

    // Destructor
    virtual ~LocalChip();

   private:
    // Helper functions
    // Startup + teardown
    void create_device(
        const std::unordered_set<chip_id_t>& target_mmio_device_ids,
        const uint32_t& num_host_mem_ch_per_mmio_device,
        const bool skip_driver_allocs,
        const bool clean_system_resources);
    void initialize_interprocess_mutexes(int pci_interface_id, bool cleanup_mutexes_in_shm);
    void cleanup_shared_host_state();
    void initialize_pcie_devices();
    void broadcast_pcie_tensix_risc_reset(TTDevice* device, const TensixSoftResetOptions& cores);
    void broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions& soft_resets);
    void send_remote_tensix_risc_reset_to_core(const cxy_pair& core, const TensixSoftResetOptions& soft_resets);
    void send_tensix_risc_reset_to_core(const cxy_pair& core, const TensixSoftResetOptions& soft_resets);
    void perform_harvesting_and_populate_soc_descriptors(const SocDescriptor& soc_descriptor, const bool perform_harvesting);
    void populate_cores();
    void init_pcie_iatus();  // No more p2p support.
    bool init_hugepage(chip_id_t device_id);
    void check_pcie_device_initialized(int device_id);
    void set_pcie_power_state(DevicePowerState state);
    int set_remote_power_state(const chip_id_t& chip, DevicePowerState device_state);
    void set_power_state(DevicePowerState state);
    uint32_t get_power_state_arc_msg(TTDevice* pci_device, DevicePowerState state);
    void enable_local_ethernet_queue(const chip_id_t& chip, int timeout);
    void enable_ethernet_queue(int timeout);
    void enable_remote_ethernet_queue(const chip_id_t& chip, int timeout);
    void deassert_resets_and_set_power_state();
    int open_hugepage_file(const std::string& dir, chip_id_t device_id, uint16_t channel);
    int iatu_configure_peer_region(
        int logical_device_id, uint32_t peer_region_id, uint64_t bar_addr_64, uint32_t region_size);
    uint32_t get_harvested_noc_rows(uint32_t harvesting_mask);
    uint32_t get_harvested_rows(int logical_device_id);
    int get_clock(int logical_device_id);

    // Communication Functions
    void read_buffer(
        void* mem_ptr,
        std::uint32_t address,
        std::uint16_t channel,
        std::uint32_t size_in_bytes,
        chip_id_t src_device_id);
    void write_buffer(
        const void* mem_ptr, std::uint32_t size, std::uint32_t address, std::uint16_t channel, chip_id_t src_device_id);
    void write_device_memory(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        cxy_pair target,
        std::uint32_t address,
        const std::string& fallback_tlb);
    void write_to_non_mmio_device(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        cxy_pair core,
        uint64_t address,
        bool broadcast = false,
        std::vector<int> broadcast_header = {});
    void read_device_memory(
        void* mem_ptr,
        cxy_pair target,
        std::uint32_t address,
        std::uint32_t size_in_bytes,
        const std::string& fallback_tlb);
    void read_from_non_mmio_device(void* mem_ptr, cxy_pair core, uint64_t address, uint32_t size_in_bytes);
    void read_mmio_device_register(
        void* mem_ptr, cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    void write_mmio_device_register(
        const void* mem_ptr, cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    void pcie_broadcast_write(
        chip_id_t chip,
        const void* mem_ptr,
        uint32_t size_in_bytes,
        std::uint32_t addr,
        const xy_pair& start,
        const xy_pair& end,
        const std::string& fallback_tlb);
    void ethernet_broadcast_write(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        const std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& cols_to_exclude,
        const std::string& fallback_tlb,
        bool use_virtual_coords);
    void set_membar_flag(
        const chip_id_t chip,
        const std::unordered_set<xy_pair>& cores,
        const uint32_t barrier_value,
        const uint32_t barrier_addr,
        const std::string& fallback_tlb);
    void insert_host_to_device_barrier(
        const chip_id_t chip,
        const std::unordered_set<xy_pair>& cores,
        const uint32_t barrier_addr,
        const std::string& fallback_tlb);
    void init_membars();
    uint64_t get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint64_t offset);
    uint16_t get_sys_rack(uint32_t rack_x, uint32_t rack_y);
    bool is_non_mmio_cmd_q_full(uint32_t curr_wptr, uint32_t curr_rptr);
    int remote_arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);
    bool address_in_tlb_space(
        uint32_t address, uint32_t size_in_bytes, int32_t tlb_index, uint64_t tlb_size, uint32_t chip);
    TTDevice* get_pci_device(int pci_intf_id) const;
    std::shared_ptr<boost::interprocess::named_mutex> get_mutex(const std::string& tlb_name, int pci_interface_id);
    virtual uint32_t get_harvested_noc_rows_for_chip(
        int logical_device_id);  // Returns one-hot encoded harvesting mask for PCIe mapped chips
    void generate_tensix_broadcast_grids_for_grayskull(
        std::set<std::pair<xy_pair, xy_pair>>& broadcast_grids,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& cols_to_exclude);
    std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& get_ethernet_broadcast_headers(
        const std::set<chip_id_t>& chips_to_exclude);
    // Test functions
    void verify_eth_fw();
    void verify_sw_fw_versions(int device_id, std::uint32_t sw_version, std::vector<std::uint32_t>& fw_versions);
    int test_setup_interface();

    // State variables
    device_dram_address_params dram_address_params;
    device_l1_address_params l1_address_params;
    driver_host_address_params host_address_params;
    driver_eth_interface_params eth_interface_params;
    std::vector<Arch> archs_in_cluster = {};
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    SocDescriptor& get_soc_descriptor(chip_id_t chip_id);
    Arch arch_name;
    // brosko: right, not a mpa but a single chip
    std::map<chip_id_t, std::unique_ptr<TTDevice>> m_pci_device_map;  // Map of enabled pci devices
    int m_num_pci_devices;                                    // Number of pci devices in system (enabled or disabled)
    std::shared_ptr<ClusterDescriptor> ndesc;
    // Level of printouts. Controlled by env var TT_PCI_LOG_LEVEL
    // 0: no debugging messages, 1: less verbose, 2: more verbose
    int m_pci_log_level;

    // remote eth transfer setup
    static constexpr std::uint32_t NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 6;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 4;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_START_ID = 0;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_MASK = (NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS - 1);

    static constexpr std::uint32_t EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS =
        NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS - NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_START_ID =
        NON_EPOCH_ETH_CORES_START_ID + NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_MASK = (EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS - 1);

    int active_core = NON_EPOCH_ETH_CORES_START_ID;
    std::vector<std::vector<cxy_pair>> remote_transfer_ethernet_cores;
    bool flush_non_mmio = false;
    bool non_mmio_transfer_cores_customized = false;
    std::unordered_map<chip_id_t, int> active_eth_core_idx_per_chip = {};
    std::unordered_map<chip_id_t, bool> noc_translation_enabled_for_chip = {};
    std::map<std::string, std::shared_ptr<boost::interprocess::named_mutex>> hardware_resource_mutex_map = {};
    std::unordered_map<chip_id_t, std::unordered_map<xy_pair, xy_pair>> harvested_coord_translation = {};
    std::unordered_map<chip_id_t, std::uint32_t> num_rows_harvested = {};
    std::unordered_map<chip_id_t, std::unordered_set<xy_pair>> workers_per_chip = {};
    std::unordered_set<xy_pair> eth_cores = {};
    std::unordered_set<xy_pair> dram_cores = {};
    uint32_t m_num_host_mem_channels = 0;
    std::unordered_map<chip_id_t, std::unordered_map<int, void*>> hugepage_mapping;
    std::unordered_map<chip_id_t, std::unordered_map<int, std::size_t>> hugepage_mapping_size;
    std::unordered_map<chip_id_t, std::unordered_map<int, std::uint64_t>> hugepage_physical_address;
    std::map<chip_id_t, std::unordered_map<std::int32_t, std::int32_t>> tlb_config_map = {};
    std::set<chip_id_t> all_target_mmio_devices;
    std::unordered_map<chip_id_t, std::vector<uint32_t>> host_channel_size;
    std::function<std::int32_t(xy_pair)> map_core_to_tlb;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {};
    std::unordered_map<std::string, uint64_t> dynamic_tlb_ordering_modes = {};
    std::map<std::set<chip_id_t>, std::unordered_map<chip_id_t, std::vector<std::vector<int>>>> bcast_header_cache = {};
    bool perform_harvesting_on_sdesc = false;
    bool use_ethernet_ordered_writes = true;
    bool use_ethernet_broadcast = true;
    bool use_virtual_coords_for_eth_broadcast = true;
    tt_version eth_fw_version;  // Ethernet FW the driver is interfacing with
    // Named Mutexes
    static constexpr char NON_MMIO_MUTEX_NAME[] = "NON_MMIO";
    static constexpr char ARC_MSG_MUTEX_NAME[] = "ARC_MSG";
    static constexpr char MEM_BARRIER_MUTEX_NAME[] = "MEM_BAR";
    // ERISC FW Version Required by UMD
    static constexpr std::uint32_t SW_VERSION = 0x06060000;
};

}  // namespace tt::umd