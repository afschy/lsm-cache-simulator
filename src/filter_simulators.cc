#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>
#include "cache.h"
#include "record_parser.h"
#include "simulator_utils.h"
#include "simulators.h"

SimulationResult filter_simulate_normal(const char* trace_file_name, const SimulationConfig& config, Cache* cache) {
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
    std::cout << "parsed: " << records_parsed << std::endl;
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
    uint64_t records_parsed = 0;

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
    std::cout << "parsed: " << records_parsed << std::endl;
    return result;
}

SimulationResult filter_simulate_optimal_modular(const char* trace_file_name, const SimulationConfig& config, OptimalCache* cache) {
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;
    std::unordered_map<uint64_t, FileMetadata> file_map;    // file_id to metadata object
    std::unordered_map<uint64_t, uint64_t> empty_access_map;    // file_id to query miss count mapping in the current window
    // key is <file_id,lookup_id>; stores the number of unnecessary lookups per file probe.
    // only the first filter block gets a file probe
    std::map<std::pair<uint64_t, uint64_t>, uint16_t> unnecessary_access_map;
    uint64_t records_parsed = 0;

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

    std::deque<Record> next_get_records;
    uint64_t get_counter = 0;
    Record curr_record;
    auto update_lookahead = [&]() {
        const Record& get_record = next_get_records.front();
        for (const Probe& curr_probe: get_record.probes) {
            if (file_map.find(curr_probe.file_id) == file_map.end()) {
                std::cerr << "Error: inconsistent simulation state, never-created file_id accessed\n";
                exit(1);
            }

            std::vector<Block> filter_block_list;
            get_filter_blocks(filter_block_list, file_map[curr_probe.file_id], config);

            uint16_t module_count = filter_block_list.size();
            if (!module_count) {
                // nothing to read and nothing to filter with, so a miss always falls through to the data blocks
                if (curr_probe.file_outcome == FileOutcome::kNotFound)
                    result.extra_read_count += curr_probe.blocks.size();
                continue;
            }
            // every module covers every key, so the file's bit budget is split evenly across them
            double bpk_per_module = 1.00 * config.bits_per_key / module_count;

            uint32_t higher_count = 0, lower_equal_count = 0, self_count = 0;
            auto self_it = empty_access_map.find(curr_probe.file_id);
            if (self_it != empty_access_map.end()) self_count = self_it->second;
            for (const auto& it : empty_access_map) {
                if (it.second > self_count) higher_count++;
                else lower_equal_count++;
            }

            uint16_t used_modules = static_cast<uint16_t>(round(1.00 * module_count * lower_equal_count / (higher_count + lower_equal_count)));
            if (!self_count) used_modules = 0;
            else used_modules = std::max(uint16_t(1), used_modules);
            used_modules = std::min(used_modules, module_count);

            filter_block_list.resize(used_modules);
            for (const Block& filter_block: filter_block_list) {
                CacheBlock cache_block(BlockType::kFilter, curr_probe.file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes, get_record.lookup_id);
                cache->add_lookahead(cache_block);
            }

            if (curr_probe.file_outcome != FileOutcome::kNotFound) continue;

            // the modules are independent, so every one of them has to report positive for a false positive
            if (used_modules && !get_filter_false_positive(used_modules * bpk_per_module)) continue;

            // with no module read there is no block to carry the probe into consume_lookahead
            if (!used_modules) {
                result.extra_read_count += curr_probe.blocks.size();
                continue;
            }
            std::pair<uint64_t, uint64_t> key_pair(curr_probe.file_id, get_record.lookup_id);
            unnecessary_access_map[key_pair] = curr_probe.blocks.size();
        }

        for (const Probe& curr_probe: get_record.probes) {
            if (curr_probe.file_outcome != FileOutcome::kNotFound) continue;
            auto it = empty_access_map.find(curr_probe.file_id);
            if (it != empty_access_map.end() && !--it->second) empty_access_map.erase(it);
        }
        next_get_records.pop_front();
    };

    while (parser.parse_next_record(&curr_record)) {
        if ((++records_parsed & ((1<<20)-1)) == 0) std::cout << "parsed: " << records_parsed << std::endl;
        bool is_get = false;
        if (curr_record.record_type == kFileCreate) {
            FileMetadata new_metadata(curr_record);
            file_map[curr_record.file_id] = new_metadata;
        }
        else if (curr_record.record_type == kFileMove) {
            if (file_map.find(curr_record.file_id) != file_map.end())
                file_map[curr_record.file_id].level = curr_record.new_level;
        }
        else if (curr_record.record_type == kGet) {
            is_get = true;
            get_counter++;
            next_get_records.push_back(curr_record);
            for (const Probe& probe: curr_record.probes)
                if (probe.file_outcome == FileOutcome::kNotFound) empty_access_map[probe.file_id]++;
        }
        if (!is_get || get_counter < config.modular_lookahead)
            continue;

        update_lookahead();
        while (cache->get_lookahead_size() > cache->get_max_lookahead())
            consume_lookahead();
    }

    while (next_get_records.size()) update_lookahead();
    while (cache->get_lookahead_size()) consume_lookahead();

    // the cache is an OptimalCache here too, so the policy name has to distinguish the two runs
    result.cache_policy_name = "MODULAR_" + cache->get_name();
    if (curr_access) result.push();
    std::cout << "parsed: " << records_parsed << std::endl;
    return result;
}

SimulationResult filter_simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* cache) {
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
                release_file(cache, it->second, config);
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

            bool bloom_result = true;
            if (used_modules) bloom_result = get_filter_false_positive(1.00 * config.bits_per_key * used_modules / module_count);
            if (!bloom_result) continue;    // no false positive, no extra read
            result.extra_read_count += curr_probe.blocks.size();
        }
    }

    result.cache_policy_name = "MODULAR_" + cache->get_name();
    if (curr_access) result.push();
    std::cout << "parsed: " << records_parsed << std::endl;
    return result;
}
