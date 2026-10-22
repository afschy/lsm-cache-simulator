#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>
#include <unordered_map>
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/tree_policy.hpp>
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

inline uint16_t get_utility_based_module_count(
        const SimulationConfig& config,
        const uint16_t& total_modules,
        const Probe& curr_probe,
        const std::vector<std::pair<uint64_t, uint64_t>>& total_access_list,
        const std::unordered_map<uint64_t, uint64_t>& total_access_index,
        const std::vector<std::pair<uint64_t, uint64_t>>& empty_access_list,
        const std::unordered_map<uint64_t, uint64_t>& empty_access_index,
        uint64_t lookup_count,
        bool deleted_file=false) {

    if (deleted_file)
        return total_modules;

    uint16_t used_modules = total_modules;

    auto total_it = total_access_index.find(curr_probe.file_id);
    if (total_it == total_access_index.end())
        return used_modules;

    // a file missing from the empty index has had no empty probes
    auto empty_it = empty_access_index.find(curr_probe.file_id);
    double total_count = total_access_list[total_it->second].second;
    double empty_count = (empty_it == empty_access_index.end()) ? 0 : empty_access_list[empty_it->second].second;
    double found_frac = (total_count - empty_count) / total_count;
    // beta: fraction of the lookups in lookup_count that probed this file
    double beta = total_count / lookup_count;
    double fpr_per_module = std::exp(-std::log(2.0)*std::log(2.0)*config.bits_per_key/total_modules);

    double* exp_io = new double[total_modules+1];
    exp_io[0] = beta;
    used_modules = 0;

    for (int i=1; i<=total_modules; i++) {
        exp_io[i] = beta * (found_frac + (1.0 - found_frac) * pow(fpr_per_module, 1.0*i));
        // utility of module i is the expected I/O it saves
        double utility = exp_io[i-1] - exp_io[i];
        if (i > 1 && utility < config.mbf_partial_threshold) break;
        if (i == 1 && utility < config.mbf_full_threshold) break;
        used_modules++;
    }

    delete[] exp_io;
    return used_modules;
}

// for optimized modular policies, calculates the amount of modules to use based on access pattern
inline uint16_t get_module_count_file_percentile(
        const uint16_t& total_modules, 
        const uint64_t curr_index,
        const uint64_t max_index,
        const uint64_t empty_count,
        bool deleted_file=false) {

    if (deleted_file) 
        return total_modules;
    if (!empty_count)
        return 0;

    uint16_t module_limit = total_modules;

    uint32_t lower_equal_count = max_index - curr_index + 1;
    uint32_t higher_count = curr_index;

    // the file counts itself, so the denominator is at least 1
    module_limit = static_cast<uint16_t>(round(1.00 * total_modules * lower_equal_count / (higher_count + lower_equal_count)));
    module_limit = std::min(module_limit, total_modules);

    return module_limit;
}

// <adjusted_ratio, file_id> order-statistics tree, holds every file with a non-zero total count
using RatioKey = std::pair<double, uint64_t>;
using RatioTree = __gnu_pbds::tree<RatioKey, __gnu_pbds::null_type, std::less<RatioKey>,
                                   __gnu_pbds::rb_tree_tag, __gnu_pbds::tree_order_statistics_node_update>;

// keys must be rebuilt with this exact expression so that erasures find them
inline double get_adjusted_ratio(uint64_t empty_count, uint64_t total_count) {
    return 1.00 * empty_count * empty_count / total_count;
}

inline uint16_t get_module_count_file_ratio(
        const double& empty_fraction,
        const uint16_t& total_modules,
        const RatioTree& ratio_tree,
        const uint64_t total_count,
        const uint64_t empty_count,
        bool deleted_file=false) {

    if (deleted_file)
        return total_modules;
    if (!empty_count)
        return 0;

    // the file counts itself, so the denominator is at least 1
    uint64_t lower_equal_count = ratio_tree.order_of_key({get_adjusted_ratio(empty_count, total_count), UINT64_MAX});
    uint64_t higher_count = ratio_tree.size() - lower_equal_count;

    uint16_t module_limit = static_cast<uint16_t>(round(1.00 * total_modules * lower_equal_count / (higher_count + lower_equal_count)));
    module_limit = std::min(module_limit, total_modules);

    module_limit = empty_fraction * module_limit;
    return module_limit;
}

// for optimized modular policies, calculates the amoung of modules based on workload empty query percentage
inline uint16_t get_module_count_workload_ratio(
        const double& empty_fraction,
        const uint16_t& total_modules, 
        bool deleted_file=false) {

    if (empty_fraction < 0 || empty_fraction > 1)
        return total_modules;
    if (deleted_file)
        return total_modules;
    return ceil(empty_fraction * total_modules);
}

