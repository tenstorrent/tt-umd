// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// Single process opens multiple clusters but uses them sequentially.
TEST(Perf, ContainersVSSize) {
    int NUM_ITER = 1e8;

    std::vector<int> time_unordered_existing;
    std::vector<int> time_unordered_nonexisting;
    std::vector<int> time_unordered_iter;
    std::vector<int> time_ordered_existing;
    std::vector<int> time_ordered_nonexisting;
    std::vector<int> time_ordered_iter;
    std::vector<int> time_unordered_map_existing;
    std::vector<int> time_unordered_map_nonexisting;
    std::vector<int> time_unordered_map_iter;
    std::vector<int> time_ordered_map_existing;
    std::vector<int> time_ordered_map_nonexisting;
    std::vector<int> time_ordered_map_iter;

    std::vector<int> elems = {1, 10, 100, 1000, 10000};

    using TimeType = std::chrono::milliseconds;

    for (auto num_elems : elems) {
        std::unordered_set<int> unordered_set;
        std::set<int> ordered_set;
        std::unordered_map<int, int> unordered_map;
        std::map<int, int> ordered_map;
        std::vector<int> indexes;

        auto start_time = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_elems; ++i) {
            int item = i * 1379845;
            indexes.push_back(item);
            unordered_set.insert(item);
            ordered_set.insert(item);
            unordered_map.insert({item, item});
            ordered_map.insert({item, item});
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        std::cout << "Inserting " << num_elems << " elements took: " << duration << " milliseconds" << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        int a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems);
            if (unordered_set.find(ind) != unordered_set.end()) {
                a += ind;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_unordered_existing.push_back(duration);
        std::cout << "Unordered Set: " << num_elems << " elements, existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems) + 1;
            if (unordered_set.find(ind) != unordered_set.end()) {
                a += ind;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_unordered_nonexisting.push_back(duration);
        std::cout << "Unordered Set: " << num_elems << " elements, non-existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER / num_elems; i++) {
            for (int elem : unordered_set) {
                a += elem;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_unordered_iter.push_back(duration);
        std::cout << "Unordered Set: " << num_elems << " elements, iterating times " << NUM_ITER / num_elems
                  << ", time: " << duration << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems);
            if (ordered_set.find(ind) != ordered_set.end()) {
                a += ind;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_ordered_existing.push_back(duration);
        std::cout << "Ordered Set: " << num_elems << " elements, existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems) + 1;
            if (ordered_set.find(ind) != ordered_set.end()) {
                a += ind;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_ordered_nonexisting.push_back(duration);
        std::cout << "Ordered Set: " << num_elems << " elements, non-existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER / num_elems; i++) {
            for (int elem : ordered_set) {
                a += elem;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_ordered_iter.push_back(duration);
        std::cout << "Ordered Set: " << num_elems << " elements, iterating times " << NUM_ITER / num_elems
                  << ", time: " << duration << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems);
            if (unordered_map.find(ind) != unordered_map.end()) {
                a += unordered_map[ind];
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_unordered_map_existing.push_back(duration);
        std::cout << "Unordered Map: " << num_elems << " elements, existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems) + 1;
            if (unordered_map.find(ind) != unordered_map.end()) {
                a += unordered_map[ind];
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_unordered_map_nonexisting.push_back(duration);
        std::cout << "Unordered Map: " << num_elems << " elements, non-existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER / num_elems; i++) {
            for (const auto& [key, value] : unordered_map) {
                a += value;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_unordered_map_iter.push_back(duration);
        std::cout << "Unordered Map: " << num_elems << " elements, iterating times " << NUM_ITER / num_elems
                  << ", time: " << duration << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems);
            if (ordered_map.find(ind) != ordered_map.end()) {
                a += ordered_map[ind];
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_ordered_map_existing.push_back(duration);
        std::cout << "Ordered Map: " << num_elems << " elements, existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER; ++i) {
            int ind = indexes.at(i % num_elems) + 1;
            if (ordered_map.find(ind) != ordered_map.end()) {
                a += ordered_map[ind];
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_ordered_map_nonexisting.push_back(duration);
        std::cout << "Ordered Map: " << num_elems << " elements, non-existing elems, time: " << duration
                  << " microseconds, result: " << a << std::endl;

        // start timing
        start_time = std::chrono::high_resolution_clock::now();
        a = 0;
        for (int i = 0; i < NUM_ITER / num_elems; i++) {
            for (const auto& [key, value] : ordered_map) {
                a += value;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<TimeType>(end_time - start_time).count();
        time_ordered_map_iter.push_back(duration);
        std::cout << "Ordered Map: " << num_elems << " elements, iterating times " << NUM_ITER / num_elems
                  << ", time: " << duration << " microseconds, result: " << a << std::endl;
    }

    std::cout << "#### Accessing existing elements:" << std::endl;
    std::cout << "| Number of elements | Unordered set | Set     | Unordered map | Map     |" << std::endl;
    std::cout << "|--------------------|---------------|---------|---------------|---------|" << std::endl;
    for (size_t i = 0; i < elems.size(); ++i) {
        std::cout << "| " << std::setw(18) << elems[i] << " | " << std::setw(13) << time_unordered_existing[i] << " | "
                  << std::setw(7) << time_ordered_existing[i] << " | " << std::setw(13)
                  << time_unordered_map_existing[i] << " | " << std::setw(7) << time_ordered_map_existing[i] << " |"
                  << std::endl;
    }
    std::cout << std::endl;

    std::cout << "#### Accessing non-existing elements:" << std::endl;
    std::cout << "| Number of elements | Unordered set | Set     | Unordered map | Map     |" << std::endl;
    std::cout << "|--------------------|---------------|---------|---------------|---------|" << std::endl;
    for (size_t i = 0; i < elems.size(); ++i) {
        std::cout << "| " << std::setw(18) << elems[i] << " | " << std::setw(13) << time_unordered_nonexisting[i]
                  << " | " << std::setw(7) << time_ordered_nonexisting[i] << " | " << std::setw(13)
                  << time_unordered_map_nonexisting[i] << " | " << std::setw(7) << time_ordered_map_nonexisting[i]
                  << " |" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "#### Iterating over the whole collection:" << std::endl;
    std::cout << "| Number of elements | Unordered set | Set     | Unordered map | Map     |" << std::endl;
    std::cout << "|--------------------|---------------|---------|---------------|---------|" << std::endl;
    for (size_t i = 0; i < elems.size(); ++i) {
        std::cout << "| " << std::setw(18) << elems[i] << " | " << std::setw(13) << time_unordered_iter[i] << " | "
                  << std::setw(7) << time_ordered_iter[i] << " | " << std::setw(13) << time_unordered_map_iter[i]
                  << " | " << std::setw(7) << time_ordered_map_iter[i] << " |" << std::endl;
    }
}
