#include <cmath>
#include <random>
#include <iostream>
#include "simulators.h"
#include "record_parser.h"

static void get_filter_blocks(std::vector<Block>& filter_block_list, const FileMetadata& file, const SimulationConfig& config) {
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

// should be only called for files that don't have the search key
static bool get_filter_false_positive(uint8_t bpk) {
    // with the optimal k = ln(2) * bpk, the false positive rate is e ^ (-bpk * ln(2)^2)
    static const std::vector<double> fp_rate_table = []() {
        const double ln2_squared = std::log(2.0) * std::log(2.0);
        std::vector<double> table(UINT8_MAX + 1);
        for (size_t i = 0; i <= UINT8_MAX; i++)
            table[i] = std::exp(-ln2_squared * i);
        return table;
    }();

    static std::mt19937_64 generator(std::random_device{}());
    static std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(generator) < fp_rate_table[bpk];
}

SimulationResult filter_simulate_normal(const char* trace_file_name, const SimulationConfig& config, Cache* cache) {
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;
    std::unordered_map<uint64_t, FileMetadata> file_map;    // file_id to metadata object

    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if (curr_record.record_type == kIterator) continue;
        if (curr_record.record_type == kFileCreate) {
            FileMetadata new_metadata(curr_record);
            file_map[curr_record.file_id] = new_metadata;
            continue;
        }
        if (curr_record.record_type == kFileMove) {
            if (file_map.find(curr_record.file_id) == file_map.end()) continue;
            file_map[curr_record.file_id].level = curr_record.new_level;
            continue;
        }
        
        std::vector<Block> filter_blocks;
        std::vector<Probe>& probes = curr_record.probes;

        for (Probe curr_probe: probes) {
            if (file_map.find(curr_probe.file_id) == file_map.end()) {
                std::cerr << "Error: inconsistent simulation state, never-created file_id accessed\n";
                exit(1);
            }

            get_filter_blocks(filter_blocks, file_map[curr_probe.file_id], config);
            for (Block filter_block: filter_blocks) {
                CacheBlock cache_block(BlockType::kFilter, curr_probe.file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes);
                if (cache->block_exists(cache_block)) {
                    cache->record_access(cache_block);
                    result.hit_count++;
                }
                else {
                    cache->insert_block(cache_block);
                    result.miss_count++;
                }

                curr_access++;
                if (curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }

            auto outcome = curr_probe.file_outcome;
            if (outcome != FileOutcome::kNotFound) continue;

            bool bloom_result = get_filter_false_positive(config.bits_per_key);
            if (!bloom_result) continue;    // no false positive, no extra read
            result.extra_read_count += curr_probe.blocks.size();
        }
    }

    result.cache_policy_name = cache->get_name();
    if (curr_access) result.push();
    return result;
}

SimulationResult filter_simulate_optimal(const char* trace_file_name, const SimulationConfig& config, OptimalCache* cache) {

}

SimulationResult filter_simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* cache) {

}
