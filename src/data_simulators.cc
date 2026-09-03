#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>
#include "cache.h"
#include "record_parser.h"
#include "simulator_utils.h"
#include "simulators.h"

SimulationResult data_simulate_normal(const char* trace_file_name, const SimulationConfig& config, Cache* filter_cache, Cache* data_cache) {
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;
    std::unordered_map<uint64_t, FileMetadata> file_map;    // file_id to metadata object
    uint64_t records_parsed = 0;

    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if ((++records_parsed & ((1<<20)-1)) == 0) std::cout << "parsed: " << records_parsed << std::endl;
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
            if (it != file_map.end()) {
                release_filters_of_file(filter_cache, it->second, config);
                data_cache->remove_file_blocks(it->second);
            }
            continue;
        }

        std::vector<Block> filter_blocks;
        std::vector<Probe>& probes = curr_record.probes;

        for (const Probe& curr_probe: probes) {
            if (file_map.find(curr_probe.file_id) == file_map.end()) {
                std::cerr << "Error: inconsistent simulation state, never-created file_id accessed\n";
                exit(1);
            }

            get_filter_blocks(filter_blocks, file_map[curr_probe.file_id], config);
            for (Block filter_block: filter_blocks) {
                CacheBlock cache_block(BlockType::kFilter, curr_probe.file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes, curr_record.lookup_id);
                if (filter_cache->block_exists(cache_block)) {
                    filter_cache->record_access(cache_block);
                    result.hit_count++;
                    result.filter_hit_count++;
                }
                else {
                    filter_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.filter_miss_count++;
                }
                
                if (++curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }

            auto outcome = curr_probe.file_outcome;
            if (outcome == FileOutcome::kNotFound) {
                bool bloom_result = get_filter_false_positive(config.bits_per_key);
                if (!bloom_result) continue;    // no false positive, no blocks read
            }
            
            for (const Block& data_block : curr_probe.blocks) {
                CacheBlock cache_block(BlockType::kData, curr_probe.file_id, data_block.block_id, curr_probe.level, data_block.uncomp_bytes, curr_record.lookup_id);

                if (data_cache->block_exists(cache_block)) {
                    data_cache->record_access(cache_block);
                    result.hit_count++;
                    result.data_hit_count++;
                }
                else {
                    data_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.data_miss_count++;
                    if (outcome == FileOutcome::kNotFound) result.extra_read_count++;
                }

                if (++curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }
        }
    }
    
    std::string filter_name = filter_cache->get_name();
    std::string data_name = data_cache->get_name();
    if (filter_name == data_name)
        result.cache_policy_name = filter_name;
    else
        result.cache_policy_name = filter_name + "_" + data_name;
    if (curr_access) result.push();
    std::cout << "parsed: " << records_parsed << std::endl;
    return result;
}

SimulationResult data_simulate_optimal(const char* trace_file_name, const SimulationConfig& config, OptimalCache* filter_cache, OptimalCache* data_cache) {
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;
    std::unordered_map<uint64_t, FileMetadata> file_map;    // file_id to metadata object
    // key is <file_id,lookup_id>
    // stores whether this block access resulted from a false-positive
    std::map<std::pair<uint64_t, uint64_t>, bool> unnecessary_access_map;
    uint64_t records_parsed = 0;

    // consumes the oldest lookahead entry; the caller must ensure the lookahead isn't empty
    auto consume_filter_lookahead = [&]() {
        if (filter_cache->advance_lookahead()) {
            result.hit_count++;
            result.filter_hit_count++;
        }
        else {
            result.miss_count++;
            result.filter_miss_count++;
        }

        if (++curr_access == config.series_per_record) {
            curr_access = 0;
            result.push();
        }
    };

    // consumes the oldest lookahead entry; the caller must ensure the lookahead isn't empty
    auto consume_data_lookahead = [&]() {
        CacheBlock lookahead_front = data_cache->lookahead_.front();
        bool miss = false;

        if (data_cache->advance_lookahead()) {
            result.hit_count++;
            result.data_hit_count++;
        }
        else {
            result.miss_count++;
            result.data_miss_count++;
            miss = true;
        }

        if (++curr_access == config.series_per_record) {
            curr_access = 0;
            result.push();
        }

        std::pair<uint64_t, uint64_t> key_pair(lookahead_front.file_id_, lookahead_front.lookup_id_);
        auto it = unnecessary_access_map.find(key_pair);
        if (it == unnecessary_access_map.end()) return;
        if (miss) result.extra_read_count++;
    };

    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if ((++records_parsed & ((1<<20)-1)) == 0) std::cout << "parsed: " << records_parsed << std::endl;
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
                    filter_cache->add_lookahead(cache_block);
                }

                std::pair<uint64_t, uint64_t> key_pair(curr_probe.file_id, curr_record.lookup_id);
                auto outcome = curr_probe.file_outcome;
                if (outcome == FileOutcome::kNotFound) {
                    bool bloom_result = get_filter_false_positive(config.bits_per_key);
                    if (bloom_result) unnecessary_access_map[key_pair] = true;
                    else continue;
                }

                for (const Block& data_block: curr_probe.blocks) {
                    CacheBlock cache_block(BlockType::kData, curr_probe.file_id, data_block.block_id, curr_probe.level, data_block.uncomp_bytes, curr_record.lookup_id);
                    data_cache->add_lookahead(cache_block);
                }
            }
        }

        // keep optimal_lookahead blocks of future visible to the eviction policy, consume the excess
        while (filter_cache->get_lookahead_size() > filter_cache->get_max_lookahead())
            consume_filter_lookahead();
        while (data_cache->get_lookahead_size() > data_cache->get_max_lookahead())
            consume_data_lookahead();
    }

    while (filter_cache->get_lookahead_size())
        consume_filter_lookahead();
    while (data_cache->get_lookahead_size())
        consume_data_lookahead();

    std::string filter_name = filter_cache->get_name();
    std::string data_name = data_cache->get_name();
    if (filter_name == data_name)
        result.cache_policy_name = filter_name;
    else
        result.cache_policy_name = filter_name + "_" + data_name;
    if (curr_access) result.push();
    std::cout << "parsed: " << records_parsed << std::endl;
    return result;
}

SimulationResult data_simulate_optimal_modular(const char* trace_file_name, const SimulationConfig& config, OptimalCache* filter_cache, OptimalCache* data_cache) {

}

SimulationResult data_simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* filter_cache, Cache* data_cache) {
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;
    std::unordered_map<uint64_t, FileMetadata> file_map;    // file_id to metadata object
    std::unordered_map<uint64_t, uint64_t> empty_access_map;    // file_id to empty access count map
    uint64_t records_parsed = 0;

    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if ((++records_parsed & ((1<<20)-1)) == 0) std::cout << "parsed: " << records_parsed << std::endl;
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
            if (it != file_map.end()) {
                release_filters_of_file(filter_cache, it->second, config);
                it->second.deleted = true;
            }

            empty_access_map.erase(curr_record.file_id);
            continue;
        }

        std::vector<Block> filter_blocks;
        std::vector<Probe>& probes = curr_record.probes;

        for (const Probe& curr_probe: probes) {
            uint64_t file_id = curr_probe.file_id;
            auto file_entry = file_map.find(file_id);
            if (file_entry == file_map.end()) {
                std::cerr << "Error: inconsistent simulation state, never-created file_id accessed\n";
                exit(1);
            }
            FileMetadata& file_metadata = file_entry->second;

            get_filter_blocks(filter_blocks, file_metadata, config);
            size_t module_count = filter_blocks.size(), used_modules;

            auto own_entry = empty_access_map.find(file_id);
            // a deleted file has no live access history to be ranked against, so read the whole filter
            if (file_metadata.deleted) used_modules = module_count;
            // a live file that has never come up empty has nothing to filter for yet
            else if (own_entry == empty_access_map.end()) used_modules = 0;
            else {
                uint64_t own_empty_count = own_entry->second;
                uint64_t less_than_equal = 0, above = 0;
                for (const auto& it: empty_access_map) {
                    if (it.second > own_empty_count) above++;
                    else less_than_equal++;
                }

                // the file counts itself, so the denominator is at least 1
                used_modules = round(1.00 * module_count * less_than_equal / (above + less_than_equal));
                // max before min, so that a file with no filter at all still lands on 0
                used_modules = std::min(std::max(used_modules, size_t(1)), module_count);
            }
            filter_blocks.resize(used_modules);

            if (curr_probe.file_outcome == FileOutcome::kNotFound && !file_metadata.deleted)
                empty_access_map[file_id]++;
            
            for (const Block& filter_block: filter_blocks) {
                CacheBlock cache_block(BlockType::kFilter, file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes, curr_record.lookup_id);
                if (filter_cache->block_exists(cache_block)) {
                    filter_cache->record_access(cache_block);
                    result.hit_count++;
                    result.filter_hit_count++;
                }
                else {
                    filter_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.filter_miss_count++;
                }

                curr_access++;
                if (curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }

            auto outcome = curr_probe.file_outcome;
            if (outcome == FileOutcome::kNotFound) {
                bool bloom_result = true;
                if (used_modules) bloom_result = get_filter_false_positive(1.00 * config.bits_per_key * used_modules / module_count);
                if (!bloom_result) continue;    // no false positive, no block read
            }
            
            for (const Block& data_block : curr_probe.blocks) {
                CacheBlock cache_block(BlockType::kData, curr_probe.file_id, data_block.block_id, curr_probe.level, data_block.uncomp_bytes, curr_record.lookup_id);

                if (data_cache->block_exists(cache_block)) {
                    data_cache->record_access(cache_block);
                    result.hit_count++;
                    result.data_hit_count++;
                }
                else {
                    data_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.data_miss_count++;
                    if (outcome == FileOutcome::kNotFound) result.extra_read_count++;
                }

                if (++curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }
        }
    }

    std::string filter_name = filter_cache->get_name();
    std::string data_name = data_cache->get_name();
    if (filter_name == data_name)
        result.cache_policy_name = filter_name;
    else
        result.cache_policy_name = filter_name + "_" + data_name;
    result.cache_policy_name = "MODULAR_" + result.cache_policy_name;
    if (curr_access) result.push();
    std::cout << "parsed: " << records_parsed << std::endl;
    return result;
}
