#include "stimulus_generators.hpp"


namespace tt::umd::test::utils {

void RunWriteTransfers(
    tt_SiliconDevice& device, 
    int num_samples,
    int seed,

    // WriteCommandSampler<WRITE_DEST_DISTR_T, /*ThreadSafeNonOverlappingWriteAddressGenerator,*/ WRITE_SIZE_DISTR_OUT_T, WRITE_SIZE_DISTR_T> const& 
    std::unique_ptr<CommandSampler> write_command_generator,
    
    ThreadSafeNonOverlappingWriteAddressGenerator &write_history_address_generator,

    counting_barrier_t &barrier
) {
    SCOPED_TRACE("RunWriteTransfers");
    auto test_generator = ReadbackWriteGenerator(
        seed,
        std::move(write_command_generator),
        device.get_virtual_soc_descriptors().at(0),
        write_history_address_generator);

    barrier.arrive_and_wait();

    std::vector<uint32_t> payload = {};
    for (int i = 0; i < num_samples; i++) {
        test_generator.generate_sample(device);
    }

    int finish_index = barrier.arrive_and_wait();
    bool last_to_exit = finish_index == 1;
    if (last_to_exit) {
        write_history_address_generator.finish_writing();
    }
}

void RunMixedTransfers(
    tt_SiliconDevice& device, 
    int num_samples,
    int seed,

    std::vector<float> const& generator_weights,
    
    std::vector<std::unique_ptr<CommandGenerator>> sample_generators,
    
    bool record_command_history,
    std::vector<remote_transfer_sample_t> *command_history
) {
    assert(generator_weights.size() == sample_generators.size());
    std::mt19937 random_generator(seed);
    std::discrete_distribution<int> generator_distribution(generator_weights.begin(), generator_weights.end());
    for (std::size_t i = 0; i < num_samples; i++) {
        int generator_index = generator_distribution(random_generator);
        sample_generators.at(generator_index)->generate_sample(device);
    }
    
}

void RunReadbackChecker(
    tt_SiliconDevice& device,
    int seed,
    ThreadSafeNonOverlappingWriteAddressGenerator &write_history_address_generator
) {
    SCOPED_TRACE("RunWriteTransfers");

    std::vector<uint32_t> readback_buffer = {};
    int num_chips = device.get_number_of_chips_in_cluster();
    int num_channels = device.get_num_dram_channels(0);
    auto chips = write_history_address_generator.get_chips();
    auto chip_iter = chips.begin();
    assert (chip_iter != chips.end());
    int channel = 0;
    while (write_history_address_generator.is_active()) {
        assert (chip_iter != chips.end());
        auto dram_location = dram_location_t{.chip_id=*chip_iter,.channel=channel};
        write_history_address_generator.readback_writes_on_dram_channel_non_blocking(device, dram_location, readback_buffer);

        if (channel == num_channels - 1) {
            channel = 0;
            ++chip_iter;
            if (chip_iter == chips.end()) {
                chip_iter = chips.begin();
            }
        } else {
            channel++;
        }
    }
}

}; // namespace tt::umd::test::utils