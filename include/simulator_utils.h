#pragma once
#include <vector>
#include "cache.h"
#include "record_parser.h"
#include "simulation_config_result.h"

void get_filter_blocks(std::vector<Block>& filter_block_list, const FileMetadata& file, const SimulationConfig& config) {
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

void release_file(Cache* cache, const FileMetadata& file, const SimulationConfig& config) {
    std::vector<Block> filter_blocks;
    get_filter_blocks(filter_blocks, file, config);
    for (const Block& b : filter_blocks)
        cache->remove_block(BlockType::kFilter, file.file_id, b.block_id);
}

// should be only called for files that don't have the search key
// bpk is a double so that a filter split into more modules than it has bits per key stays representable
bool get_filter_false_positive(double bpk) {
    // with the optimal k = ln(2) * bpk, the false positive rate is e ^ (-bpk * ln(2)^2)
    static const double ln2_squared = std::log(2.0) * std::log(2.0);
    static std::mt19937_64 generator(std::random_device{}());
    static std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(generator) < std::exp(-ln2_squared * bpk);
}
