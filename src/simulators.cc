#include <cmath>
#include <random>
#include <iostream>
#include <map>
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

static void release_file(Cache* cache, const FileMetadata& file, const SimulationConfig& config) {
    std::vector<Block> filter_blocks;
    get_filter_blocks(filter_blocks, file, config);
    for (const Block& b : filter_blocks)
        cache->remove_block(BlockType::kFilter, file.file_id, b.block_id);
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
        if (curr_record.record_type == kFileDelete) {
            auto it = file_map.find(curr_record.file_id);
            if (it != file_map.end()) release_file(cache, it->second, config);
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
                CacheBlock cache_block(BlockType::kFilter, curr_probe.file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes, curr_record.lookup_id);
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
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;
    std::unordered_map<uint64_t, FileMetadata> file_map;    // file_id to metadata object
    // key is <file_id,lookup_id>; stores the number of unnecessary lookups per file probe.
    // only the first filter block gets a file probe
    std::map<std::pair<uint64_t, uint64_t>, uint16_t> unnecessary_access_map;

    // consumes the oldest lookahead entry; the caller must ensure the lookahead isn't empty
    auto consume_lookahead = [&]() {
        CacheBlock lookahead_front = cache->lookahead_.front();
        if (cache->advance_lookahead()) result.hit_count++;
        else result.miss_count++;

        if (++curr_access == config.series_per_record) {
            curr_access = 0;
            result.push();
        }

        if (lookahead_front.block_id_ != 0) return;    // only the first block carries the probe
        std::pair<uint64_t, uint64_t> key_pair(lookahead_front.file_id_, lookahead_front.lookup_id_);
        auto it = unnecessary_access_map.find(key_pair);
        if (it == unnecessary_access_map.end()) return;
        result.extra_read_count += it->second;
        unnecessary_access_map.erase(it);
    };

    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if (curr_record.record_type == kFileCreate) {
            FileMetadata new_metadata(curr_record);
            file_map[curr_record.file_id] = new_metadata;
        }
        else if (curr_record.record_type == kFileMove) {
            if (file_map.find(curr_record.file_id) != file_map.end())
                file_map[curr_record.file_id].level = curr_record.new_level;
        }
        else if (curr_record.record_type == kGet) {
            std::vector<Block> filter_blocks;
            std::vector<Probe>& probes = curr_record.probes;

            for (Probe curr_probe: probes) {
                if (file_map.find(curr_probe.file_id) == file_map.end()) {
                    std::cerr << "Error: inconsistent simulation state, never-created file_id accessed\n";
                    exit(1);
                }

                get_filter_blocks(filter_blocks, file_map[curr_probe.file_id], config);
                for (Block filter_block: filter_blocks) {
                    CacheBlock cache_block(BlockType::kFilter, curr_probe.file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes, curr_record.lookup_id);
                    cache->add_lookahead(cache_block);
                }

                std::pair<uint64_t, uint64_t> key_pair(curr_probe.file_id, curr_record.lookup_id);
                unnecessary_access_map[key_pair] = 0;

                auto outcome = curr_probe.file_outcome;
                if (outcome == FileOutcome::kNotFound) {
                    bool bloom_result = get_filter_false_positive(config.bits_per_key);
                    if (bloom_result) unnecessary_access_map[key_pair] = curr_probe.blocks.size();
                }
            }
        }

        // keep optimal_lookahead blocks of future visible to the eviction policy, consume the excess
        while (cache->get_lookahead_size() > cache->get_max_lookahead())
            consume_lookahead();
    }

    while (cache->get_lookahead_size())
        consume_lookahead();

    result.cache_policy_name = cache->get_name();
    if (curr_access) result.push();
    return result;
}

SimulationResult filter_simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* cache) {
    
}
