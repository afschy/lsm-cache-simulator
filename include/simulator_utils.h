#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>
#include "cache.h"
#include "record_parser.h"
#include "simulation_config_result.h"

inline void get_filter_blocks(std::vector<Block>& filter_block_list, const FileMetadata& file, const SimulationConfig& config) {
    filter_block_list.clear();
    uint32_t filter_block_count = ceil(1.00 * file.entry_count / config.filter_keys_per_block);
    for (size_t i=0; i<filter_block_count; i++) {
        Block new_block;
        new_block.block_id = i;
        new_block.seq = 0;
        new_block.read_bytes = config.default_block_size;
        new_block.uncomp_bytes = config.default_block_size;
        filter_block_list.push_back(new_block);
    }
}

inline void release_filters_of_file(Cache* cache, const FileMetadata& file, const SimulationConfig& config) {
    std::vector<Block> filter_blocks;
    get_filter_blocks(filter_blocks, file, config);
    for (const Block& b : filter_blocks)
        cache->remove_block(BlockType::kFilter, file.file_id, b.block_id);
}

// should be only called for files that don't have the search key
// bpk is a double so that a filter split into more modules than it has bits per key stays representable
inline bool get_filter_false_positive(double bpk) {
    // with the optimal k = ln(2) * bpk, the false positive rate is e ^ (-bpk * ln(2)^2)
    static const double ln2_squared = std::log(2.0) * std::log(2.0);
    static std::mt19937_64 generator(std::random_device{}());
    static std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(generator) < std::exp(-ln2_squared * bpk);
}

// using modular filters with a total module count and bpk, returns the number of modules used and the verdict
inline bool modular_filter_verdict(bool real_verdict, double total_bpk,
                                       uint16_t total_modules, uint16_t module_limit,
                                       uint16_t& used_modules) {
    // if the actual verdict is true, the modular bf will use all modules and give find no negative value
    // if module_limit is 0, default to true
    if (real_verdict || module_limit==0) {
        used_modules = module_limit;
        return true;
    }

    double bpk_per_module = total_bpk / total_modules;
    double verdict = true;
    used_modules = 0;

    while (verdict && used_modules < module_limit) {
        used_modules++;
        verdict = get_filter_false_positive(bpk_per_module);
    }
    return verdict;
}

