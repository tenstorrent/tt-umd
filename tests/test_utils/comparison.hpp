#pragma once
#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <vector>
#include "tt_cluster_descriptor.h"
#include "tt_cluster_descriptor_types.h"
#include "tt_device.h"
#include "tt_soc_descriptor.h"
#include "tt_xy_pair.h"
#include "types.hpp"

#include <mutex>
#include <optional>

#include <atomic>

class ExecutionMutex {
  public:
    ExecutionMutex() : _is_locked(false) {}

    void lock() {
        while (this->_is_locked.exchange(true)) {
            // spin
        }
    }

    void unlock() {
        this->_is_locked.store(false);
    }

    bool try_lock() {
        return !this->_is_locked.exchange(true);
    }

  private:
    std::atomic<bool> _is_locked;
};


namespace tt::umd::test::utils {

// compact representation of a data transfer payload
// where the payload starts at `start` value and increments by `increment` every `run_length` words (4B)
template <typename DATUM_T>
struct payload_spec_t {
    DATUM_T start;
    DATUM_T increment;
    std::size_t run_length;

    void write_payload(DATUM_T *data_ptr, std::size_t num_words) const {
        assert(num_words > 0);
        assert(this->run_length > 0);
        assert(this->increment > 0);
        
        DATUM_T write_value = this->start;
        for (std::size_t i = 0; i < num_words; i += this->run_length, write_value += this->increment) {
            std::size_t run_length = std::min(this->run_length, num_words - i);
            std::fill_n(data_ptr + i, run_length, write_value);
        }
    }

    void write_payload(std::vector<DATUM_T> &data) const {
        this->write_payload(data.data(), data.size());
    }
};

template<typename T>
bool payload_matches(T const* data_ptr, std::size_t num_words, payload_spec_t<T> const& payload_spec) {
    assert(num_words > 0);
    assert(payload_spec.run_length > 0);
    assert(payload_spec.increment > 0);
    
    T expected_value = payload_spec.start;
    for (std::size_t i = 0; i < num_words; i += payload_spec.run_length, expected_value += payload_spec.increment) {
        std::size_t run_length = std::min(payload_spec.run_length, num_words - i);
        for (std::size_t j = 0; j < run_length; j++) {
            if (data_ptr[i + j] != expected_value) {
                return false;
            }
        }
    }

    return true;
}

template<typename T>
bool payload_matches(std::vector<T> const& data, payload_spec_t<T> const& payload_spec) {
    return payload_matches(data.data(), data.size(), payload_spec);
}




template <typename DATUM_T>
struct write_payload_t {
    payload_spec_t<DATUM_T> payload_spec;
    std::size_t start_address;
    std::size_t num_words;


    bool payload_matches(std::vector<DATUM_T> const& data) const {
        return payload_matches(data, payload_spec);
    }

    void write_payload(std::vector<DATUM_T> &data) const {
        payload_spec.write_payload(data);
    }
};

struct dram_location_t {
    chip_id_t chip_id;
    int channel;
};


template <typename DATUM_T>
class ThreadSafeWriteHistoryCircularBuffer {
    using write_history_t = std::deque<write_payload_t<DATUM_T>>;
  public:
    ThreadSafeWriteHistoryCircularBuffer(
        tt_SocDescriptor const& soc_desc, 
        chip_id_t chip, 
        int dram_channel, 
        std::size_t write_region_start, 
        std::size_t write_region_end,
        std::size_t max_writes_per_buffer,
        std::size_t num_history_buffers = 2) :
        chip(chip),
        dram_channel(dram_channel),
        channel_core(tt_cxy_pair(chip, soc_desc.dram_cores.at(dram_channel).at(0))),
        _read_queue_index(0),
        _write_queue_index(0),
        writer_lock(writer_mutex,std::defer_lock),
        reader_lock(reader_mutex,std::defer_lock),
        max_writes_per_buffer(max_writes_per_buffer),
        num_history_buffers(num_history_buffers),
        write_history_buffers(2)
    {
        assert(write_region_start < write_region_end);
        assert(write_region_start == address_aligner(write_region_start));//, "write_region_start must be aligned to 32B");
        assert(write_region_end == address_aligner(write_region_end));//, "write_region_end must be aligned to 32B");
        std::size_t total_available_write_size = write_region_end - write_region_start;

        std::size_t buffer_region_size = address_aligner(total_available_write_size / this->num_history_buffers);
        this->max_allowed_write_size = buffer_region_size;
        for (std::size_t i = 0; i < this->num_history_buffers; i++) {
            auto start = write_region_start + i * buffer_region_size;
            auto end = std::min(write_region_start + (i + 1) * buffer_region_size, write_region_end);
            this->write_region_starts.push_back(start);
            this->write_region_ends.push_back(end);
            this->max_allowed_write_size = std::min(this->max_allowed_write_size, end - start);

        }
        assert(this->_read_queue_index == 0);
        assert(this->_write_queue_index == 0);

        std::stringstream label_ss;
        label_ss << "chip_" << chip << "_channel_" << dram_channel;
        this->my_label = label_ss.str();
    }

    std::optional<address_t> push_write_non_blocking(tt_SiliconDevice &driver, payload_spec_t<DATUM_T> const& payload_spec, std::vector<DATUM_T> & payload_buffer, std::size_t num_words) {
        const std::size_t num_bytes = num_words * sizeof(DATUM_T);

        assert (num_bytes <= this->max_allowed_write_size_bytes());

        std::stringstream ss;
        ss << this->my_label << " push_write_non_blocking" << std::endl;
        // std::cout << ss.str();

        if (not this->can_write_to_current_buffer()) {
            std::stringstream ss2;
            ss2 << this->my_label << " push_write_non_blocking:not can_write_to_current_buffer" << std::endl;
            // std::cout << ss2.str();

            return std::nullopt;
        }

        std::unique_lock writer_lock{writer_mutex, std::defer_lock};
        bool lock_acquired = writer_lock.try_lock();
        if (not lock_acquired) {
            std::stringstream ss3;
            ss3 << this->my_label << " push_write_non_blocking:lock_not_acquired" << std::endl;
            // std::cout << ss3.str();

            return std::nullopt;
        }

        // lock was acquired past this point //

        auto &write_history = this->write_history_buffers.at(this->get_write_queue_index());
        std::size_t next_write_address = this->write_region_starts.at(this->get_write_queue_index());
        if (write_history.size() > 0) {
            next_write_address = write_history.front().start_address + write_history.front().num_words * sizeof(DATUM_T);
        }
        bool fits = next_write_address + num_bytes <= this->write_region_ends.at(this->get_write_queue_index());
        assert(payload_spec.run_length * sizeof(DATUM_T) <= this->write_region_ends.at(this->get_write_queue_index()) - this->write_region_starts.at(this->get_write_queue_index()));

        std::stringstream ss4;
        ss4 << this->my_label << " push_write_non_blocking:fits=" << (fits ? "T" : "F") << std::endl;
        // std::cout << ss4.str();

        if (!fits) {
            std::stringstream ss5;
            ss5 << this->my_label << " push_write_non_blocking:(pre)incremented_writer_index=" << this->_write_queue_index << std::endl;
            // std::cout << ss5.str();

            this->increment_writer_index();
            std::stringstream ss6;
            ss6 << this->my_label << " push_write_non_blocking:incremented_writer_index=" << this->_write_queue_index << std::endl;
            // std::cout << ss6.str();
            
            writer_lock.unlock();
            return std::nullopt;
        }

        auto const& write_entry = write_payload_t<DATUM_T>{
            .payload_spec = payload_spec,
            .start_address = next_write_address,
            .num_words = num_words
        };
        
        std::stringstream ss7;
        ss7 << this->my_label << " push_write_non_blocking:dispatch_write" << std::endl;
        // std::cout << ss7.str();

        this->dispatch_write(driver, write_entry, payload_buffer);

        if (write_history.size() == this->max_writes_per_buffer) {
            this->increment_writer_index();

            std::stringstream ss8;
            ss8 << this->my_label << " push_write_non_blocking:increment_writer_index=" << this->get_write_queue_index() << std::endl;
            // std::cout << ss8.str();
        }
        writer_lock.unlock();
        
        std::stringstream ss9;
        ss9 << this->my_label << " push_write_non_blocking:lock_released" << std::endl;
        // std::cout << ss9.str();

        return write_entry.start_address;
    }

    std::optional<std::pair<bool, write_payload_t<DATUM_T>>> readback_and_check_oldest_entry_non_blocking(tt_SiliconDevice &driver, std::vector<DATUM_T> &buffer, std::string const& tlb_to_use = "LARGE_READ_TLB") {
        
        std::unique_lock reader_lock{reader_mutex, std::defer_lock};
        if (reader_lock.try_lock()) {
            bool write_buffer_ready_for_readback = this->write_read_distance() > 0;
            std::stringstream ss;
            ss << "readback_and_check_oldest_entry_non_blocking. write_buffer_ready_for_readback = " << (write_buffer_ready_for_readback? "T": "F") << std::endl;
            // std::cout << ss.str() << std::endl;
            if (not write_buffer_ready_for_readback) {
                reader_lock.unlock();
                return std::nullopt;
            }

            std::stringstream ss2;
            ss2 << this->my_label << " readback_and_check_oldest_entry_non_blocking:acquired_lock" << std::endl;
            // std::cout << ss2.str();

            assert (this->_read_queue_index != this->_write_queue_index);
            auto &write_history_buffer = this->write_history_buffers[this->get_read_queue_index()];
            assert (write_history_buffer.size() > 0);
            auto const oldest_write = write_history_buffer.back();
            buffer.resize(oldest_write.num_words);
            buffer.at(0) = -1; // To make sure we got new data
            
            std::stringstream ss3;
            ss3 << this->my_label << " readback_and_check_oldest_entry_non_blocking:reading" << std::endl;
            // std::cout << ss3.str();

            driver.read_from_device(buffer, channel_core, oldest_write.start_address, oldest_write.num_words * sizeof(DATUM_T), tlb_to_use);
    
            write_history_buffer.pop_back();
            if (write_history_buffer.size() == 0) {
                // drained the write history for this buffer so it's time to advance
                this->increment_reader_index();
                std::stringstream ss3_2;
                ss3_2 << this->my_label << " readback_and_check_oldest_entry_non_blocking:increment reader index ->" << this->get_read_queue_index() << std::endl;
                // std::cout << ss3_2.str();
            }
            reader_lock.unlock();

            std::stringstream ss4;
            ss4 << this->my_label << " readback_and_check_oldest_entry_non_blocking:comparing" << std::endl;
            // std::cout << ss4.str();

            bool matches = payload_matches(buffer, oldest_write.payload_spec);
            return std::pair<bool, write_payload_t<DATUM_T>>{matches, oldest_write};
        } else {
            return std::nullopt;
        }
    }

    std::size_t max_allowed_write_size_bytes() const {
        return this->max_allowed_write_size * sizeof(DATUM_T);
    }

  private:

    void dispatch_write(tt_SiliconDevice &driver, write_payload_t<DATUM_T> const& write_entry, std::vector<DATUM_T> & payload, std::string const& tlb_to_use = "LARGE_WRITE_TLB") {
        assert (payload.size() >= write_entry.num_words);
        auto &write_history = this->write_history_buffers[this->get_write_queue_index()];
        write_history.push_front(write_entry);
        
        std::stringstream ss;
        ss << "dispatch_write: " << this->my_label << " write_entry.start_address=" << write_entry.start_address << " size=" << write_entry.num_words << " start_value=" << write_entry.payload_spec.start << std::endl;
        // std::cout << ss.str();

        assert(payload[0] == write_entry.payload_spec.start);// debug while we are only using default payload
        driver.write_to_device(payload, channel_core, write_entry.start_address, tlb_to_use);
        assert(payload[0] == write_entry.payload_spec.start);// debug while we are only using default payload
    }

    const chip_id_t chip;
    const int dram_channel;
    const tt_cxy_pair channel_core;

    std::vector<std::size_t> write_region_starts;
    std::vector<std::size_t> write_region_ends;
    const int max_writes_per_buffer;
    const std::size_t num_history_buffers;

    std::mutex reader_mutex;
    std::mutex writer_mutex;
    std::unique_lock<std::mutex> writer_lock;
    std::unique_lock<std::mutex> reader_lock;

    int write_read_distance() const {
        assert(this->_write_queue_index >= 0);
        assert(this->_read_queue_index >= 0);
        return (this->_write_queue_index < this->_read_queue_index) ? 
            ((2 * this->num_history_buffers - this->_read_queue_index) + this->_write_queue_index) : 
            (this->_write_queue_index - this->_read_queue_index);
    }

    bool can_write_to_current_buffer() const { return this->write_read_distance() < this->num_history_buffers; }

    void increment_reader_index() { this->_read_queue_index = ((this->_read_queue_index + 1) % (2 * this->num_history_buffers)); }
    void increment_writer_index() { this->_write_queue_index = ((this->_write_queue_index + 1) % (2 * this->num_history_buffers)); }

    int get_read_queue_index() const {  int result = this->_read_queue_index % this->num_history_buffers; assert (result >= 0 and result < this->num_history_buffers); return result;}
    int get_write_queue_index() const { int result = this->_write_queue_index % this->num_history_buffers; assert (result >= 0 and result < this->num_history_buffers); return result;}

    int _read_queue_index;
    int _write_queue_index;

    std::string my_label;

    std::size_t max_allowed_write_size;
    // Have two separate write histories so we can double buffer (one can write while the other is read back)
    std::vector<write_history_t> write_history_buffers;
};

class ThreadSafeNonOverlappingWriteAddressGenerator {
    using DATUM_T = std::uint32_t;
  public:
    ThreadSafeNonOverlappingWriteAddressGenerator(
        tt_SocDescriptor const& soc_desc,
        std::unordered_set<chip_id_t> const& chips, 
        std::vector<std::size_t> const& per_channel_write_region_start, 
        std::vector<std::size_t> const& per_channel_write_region_end,
        std::size_t max_writes_per_buffer,
        std::size_t num_history_buffers = 2) :
        chips(chips),
        channels_per_chip(per_channel_write_region_start.size()),
        is_done(false),
        default_payload_spec(payload_spec_t<DATUM_T>{.start = 1,.increment = 1,.run_length = 32}),
        reference_default_payload(),
        reference_payload_buffer_copy_counter(0)
    {
        assert(per_channel_write_region_start.size() == per_channel_write_region_end.size());
        int chip_index = 0;
        for (chip_id_t c : chips) {
            chip_indices.insert({c, chip_index});
            for (std::size_t ch = 0; ch < channels_per_chip; ch++) {
                assert(per_channel_write_region_start[ch] <= per_channel_write_region_end[ch]);
                dram_channel_active_write_histories.push_back(
                    std::make_unique<ThreadSafeWriteHistoryCircularBuffer<DATUM_T>>(
                        soc_desc, 
                        c, 
                        ch, 
                        per_channel_write_region_start.at(ch), 
                        per_channel_write_region_end.at(ch),
                        max_writes_per_buffer,
                        num_history_buffers
                    )
                );
            }
            
            chip_index++;
        }
    }

    static ThreadSafeNonOverlappingWriteAddressGenerator build(tt_SocDescriptor const& soc_desc, std::unordered_set<chip_id_t> const& chips, std::size_t writes_per_chunk, std::size_t chunks_per_channel) {
        auto num_dram_channels = soc_desc.dram_cores.size();
        
        // 512MB to end of bank
        std::vector<std::size_t> per_channel_write_region_start(num_dram_channels,0x20000000);
        std::vector<std::size_t> per_channel_write_region_end(num_dram_channels, soc_desc.dram_bank_size);
        return ThreadSafeNonOverlappingWriteAddressGenerator(soc_desc, chips, per_channel_write_region_start, per_channel_write_region_end, writes_per_chunk, chunks_per_channel);
    }


    static ThreadSafeNonOverlappingWriteAddressGenerator build_default(tt_SocDescriptor const& soc_desc, std::unordered_set<chip_id_t> const& chips) {
        auto num_dram_channels = soc_desc.dram_cores.size();
        
        // 512MB to end of bank
        std::vector<std::size_t> per_channel_write_region_start(num_dram_channels,0x20000000);
        std::vector<std::size_t> per_channel_write_region_end(num_dram_channels, soc_desc.dram_bank_size);
        return ThreadSafeNonOverlappingWriteAddressGenerator(soc_desc, chips, per_channel_write_region_start, per_channel_write_region_end, 100);
    }

    // Payload buffer should be populated by this point
    std::optional<address_t> write_to_next_address_non_blocking(
        tt_SiliconDevice &driver, dram_location_t const& dram_location, payload_spec_t<DATUM_T> const& payload_spec, std::vector<uint32_t> &payload, std::size_t num_bytes
    ) {
        assert(this->chips.find(dram_location.chip_id) != this->chips.end());
        assert(dram_location.channel < channels_per_chip);

        auto index = this->flat_index(dram_location);
        ThreadSafeWriteHistoryCircularBuffer<DATUM_T> *dram_channel_active_write_history = dram_channel_active_write_histories.at(index).get();
        auto result = dram_channel_active_write_history->push_write_non_blocking(driver, payload_spec, payload, num_bytes);
        return result;
    }

    void write_to_next_address_blocking(tt_SiliconDevice &driver, dram_location_t const& dram_location, std::vector<uint32_t> &payload, std::size_t num_bytes) {
        const std::size_t num_words = num_bytes / sizeof(DATUM_T);
        if (num_words > reference_default_payload.size()) {
            // Resize the reference vector.
            // Resizing vector invalidates iterators though, which may be in use in the copy below
            // by other threads, so we could end up messing up their copy commands if we aren't
            // careful about when we do the resize. To avoid this thread conflict we do the following:
            // 1) Use an atomic counter of the number of active copy commands 
            //     - (`reference_payload_buffer_copy_counter`)
            //    a) Both the increment (during before copy) and the check (here) are inside
            //       the scope of a synchronization primitive (mutex: `reference_payload_buffer_upsize_mutex`)
            //    b) If we are a copying thread, as soon as we do the increment, we can release the mutex
            //       because (given code below), it's only possible for us to increment if there is no active
            //       upsizing happening.
            //    c) If we are upsizing, we can spin on the counter until it reaches 0, because if we are in
            //       this block with lock acquired, it means other threads can't further increment the counter
            //       (it can only monotonically decrease to 0) 
            reference_payload_buffer_upsize_mutex.lock();

            while (std::atomic_load(&reference_payload_buffer_copy_counter) > 0) {
                // spin until all writers have finished copying
            }

            // We want to check the vector size again in case another thread entered this block
            // along with this thread, before this thread acquired the lock, and resized to a size
            // larger than this thread needs to size it to.
            if (num_words > reference_default_payload.size()) {
                reference_default_payload.resize(num_words);
                default_payload_spec.write_payload(reference_default_payload);
            }
            reference_payload_buffer_upsize_mutex.unlock();
        }
        payload.resize(num_words);

        { // copy from reference to the payload
            reference_payload_buffer_upsize_mutex.lock();
            std::atomic_fetch_add(&reference_payload_buffer_copy_counter, 1);
            reference_payload_buffer_upsize_mutex.unlock();
            std::copy(reference_default_payload.begin(), reference_default_payload.begin() + num_words, payload.begin());
            std::atomic_fetch_sub(&reference_payload_buffer_copy_counter, 1);
            assert(reference_payload_buffer_copy_counter >= 0);
        }

        assert (payload.at(0) == 1);

        int retry_count = 0;
        while (write_to_next_address_non_blocking(driver, dram_location, default_payload_spec, payload, num_words) == std::nullopt) {
            retry_count++;
            if (retry_count % 100000 == 0)   {
                std::stringstream ss;
                ss << "NOTE: (" << (retry_count / 50000) << "x) blocking at write_to_next_address_blocking @chip=" << dram_location.chip_id << ", channel=" << dram_location.channel << std::endl;
                // std::cout << ss.str();
            }
        }
    }


    void write_to_next_address_blocking_custom(
        tt_SiliconDevice &driver, 
        dram_location_t const& dram_location, 
        std::vector<uint32_t> &payload, 
        std::size_t num_bytes,
        std::vector<uint32_t> &reference_custom_payload, 
        payload_spec_t<DATUM_T> const& custom_payload_spec
        ) {
        const std::size_t num_words = num_bytes / sizeof(DATUM_T);
        if (num_words > reference_custom_payload.size()) {
            reference_custom_payload.resize(num_words);
            custom_payload_spec.write_payload(reference_custom_payload);
        }

        payload.resize(num_words);
        std::copy(reference_custom_payload.begin(), reference_custom_payload.begin() + num_words, payload.begin());

        assert (payload.at(0) == custom_payload_spec.start);

        std::size_t retry_count = 0;
        while (write_to_next_address_non_blocking(driver, dram_location, custom_payload_spec, payload, num_words) == std::nullopt) {
            retry_count++;
            if (retry_count % 100000 == 0) {
                std::stringstream ss;
                ss << "NOTE: (" << (retry_count / 50000) << "x) blocking at write_to_next_address_blocking @chip=" << dram_location.chip_id << ", channel=" << dram_location.channel << std::endl;
                std::cout << ss.str();
            }
        }
    }

    // uncomment for blocking call
    // void pop_write_address(dram_location_t const& dram_location) {
    //     assert(this->chips.find(dram_location.chip_id) != this->chips.end());
    //     assert(dram_location.channel < channels_per_chip);

    //     auto index = this->flat_index(dram_location);
    //     auto &dram_channel_active_write_history = dram_channel_active_write_histories.at(index);
    //     // dram_channel_active_write_history.pop_write_blocking();
    //     dram_channel_active_write_history.pop_write_blocking();
    // }

    void readback_writes_on_dram_channel_non_blocking(tt_SiliconDevice &driver, dram_location_t const& dram_location, std::vector<uint32_t> &readback_buffer) {
        assert(this->chips.find(dram_location.chip_id) != this->chips.end());
        assert(dram_location.channel < channels_per_chip);

        auto index = this->flat_index(dram_location);
        auto *dram_channel_active_write_history = dram_channel_active_write_histories.at(index).get();

        auto result = dram_channel_active_write_history->readback_and_check_oldest_entry_non_blocking(driver, readback_buffer);

        if (result != std::nullopt) {
            auto const& [matches, write_info] = result.value();
            if (not matches) {
                is_done = false;

                std::stringstream error_message;
                error_message << "ERROR: Readback on chip: " << dram_location.chip_id << 
                    ", channel: " << dram_location.channel << 
                    ", address: " << write_info.start_address << 
                    ", size (words 4 B): " << write_info.num_words << ".\n";

                error_message << "Expected payload(start_value=" << write_info.payload_spec.start << ", increment=" << write_info.payload_spec.increment << ", run_length=" << write_info.payload_spec.run_length << ")\n";

                error_message << "Actual payload: {";
                for (auto d : readback_buffer) {
                    error_message << d << ",";
                } 
                error_message << "}\n";
                std::cerr << error_message.str();
                assert(false);
            }
        }
    }

    volatile bool is_active() const {
        return !this->is_done;
    }

    void finish_writing() {
        this->is_done = true;
    }

    std::unordered_set<chip_id_t> get_chips() const {
        return this->chips;
    }

  private:
    std::size_t flat_index(dram_location_t const& dram_location) {
        return chip_indices.at(dram_location.chip_id) * channels_per_chip + dram_location.channel;
    }
    const std::unordered_set<chip_id_t> chips;
    std::unordered_map<chip_id_t,std::size_t> chip_indices;

    const int channels_per_chip;
    
    std::mutex readback_mutex;
    std::vector<std::unique_ptr<ThreadSafeWriteHistoryCircularBuffer<DATUM_T>>> dram_channel_active_write_histories;

    bool is_done;

    const payload_spec_t<DATUM_T> default_payload_spec;
    std::vector<DATUM_T> reference_default_payload;

    ExecutionMutex reference_payload_buffer_upsize_mutex;  
    std::atomic<int> reference_payload_buffer_copy_counter;
};





}; // namespace tt::umd::test::utils