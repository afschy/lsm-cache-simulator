
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>
#include "cache.h"
#include "record_parser.h"
#include "simulation_config_result.h"
#include "simulator_utils.h"
#include "simulators.h"

SimulationResult simulate_optimal(const char* trace_file_name, const SimulationConfig& config, OptimalCache* filter_cache, OptimalCache* data_cache) {
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
        CacheBlock lookahead_front;
        if (filter_cache->lookahead_.size())
            lookahead_front = filter_cache->lookahead_.front();
        BlockType type = lookahead_front.block_type_;

        if (filter_cache->advance_lookahead()) {
            result.hit_count++;
            result.hit_bytes += lookahead_front.size_;
            if (type == BlockType::kFilter) {
                result.filter_hit_count++;
                result.filter_hit_bytes += lookahead_front.size_;
            }
            else if (type == BlockType::kIndex) {
                result.index_hit_count++;
                result.index_hit_bytes += lookahead_front.size_;
            }
        }
        else {
            result.miss_count++;
            result.miss_bytes += lookahead_front.size_;
            if (type == BlockType::kFilter) {
                result.filter_miss_count++;
                result.filter_miss_bytes += lookahead_front.size_;
            }
            else if (type == BlockType::kIndex) {
                result.index_miss_count++;
                result.index_miss_bytes += lookahead_front.size_;
                std::pair<uint64_t, uint64_t> key_pair(lookahead_front.file_id_, lookahead_front.lookup_id_);
                if (unnecessary_access_map.find(key_pair) != unnecessary_access_map.end()) {
                    result.extra_read_count++;
                    result.extra_read_bytes += lookahead_front.size_;
                }
            }
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
            result.hit_bytes += lookahead_front.size_;
            result.data_hit_count++;
            result.data_hit_bytes += lookahead_front.size_;
        }
        else {
            result.miss_count++;
            result.miss_bytes += lookahead_front.size_;
            result.data_miss_count++;
            result.data_miss_bytes += lookahead_front.size_;
            miss = true;
        }

        if (++curr_access == config.series_per_record) {
            curr_access = 0;
            result.push();
        }

        std::pair<uint64_t, uint64_t> key_pair(lookahead_front.file_id_, lookahead_front.lookup_id_);
        auto it = unnecessary_access_map.find(key_pair);
        if (it == unnecessary_access_map.end()) return;
        if (miss) {
            result.extra_read_count++;
            result.extra_read_bytes += lookahead_front.size_;
        }
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

                // filter-only: no data blocks are simulated, just count the extra reads
                if (config.mode == SimulationMode::kFilterOnly) {
                    if (curr_probe.file_outcome == FileOutcome::kNotFound && get_filter_false_positive(config.bits_per_key)) {
                        result.extra_read_count += curr_probe.blocks.size();
                        for (const Block& curr_block : curr_probe.blocks) result.extra_read_bytes += curr_block.uncomp_bytes;
                    }
                    continue;
                }
                std::pair<uint64_t, uint64_t> key_pair(curr_probe.file_id, curr_record.lookup_id);

                auto outcome = curr_probe.file_outcome;
                if (outcome == FileOutcome::kNotFound) {
                    bool bloom_result = get_filter_false_positive(config.bits_per_key);
                    if (bloom_result) unnecessary_access_map[key_pair] = true;
                    else continue;
                }

                for (const Block& curr_block: curr_probe.blocks) {
                    CacheBlock cache_block(curr_block.type, curr_probe.file_id, curr_block.block_id, curr_probe.level, curr_block.uncomp_bytes, curr_record.lookup_id);
                    if (cache_block.block_type_ == BlockType::kIndex)
                        filter_cache->add_lookahead(cache_block);
                    else if (cache_block.block_type_ == BlockType::kData)
                        data_cache->add_lookahead(cache_block);
                }
            }
        }

        // keep optimal_lookahead blocks of future visible to the eviction policy, consume the excess
        while (filter_cache->get_lookahead_size() > filter_cache->get_max_lookahead())
            consume_filter_lookahead();
        while (config.mode != SimulationMode::kFilterOnly && data_cache->get_lookahead_size() > data_cache->get_max_lookahead())
            consume_data_lookahead();
    }

    while (filter_cache->get_lookahead_size())
        consume_filter_lookahead();
    while (config.mode != SimulationMode::kFilterOnly && data_cache->get_lookahead_size())
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

SimulationResult simulate_optimal_modular(const char* trace_file_name, const SimulationConfig& config, OptimalCache* filter_cache, OptimalCache* data_cache) {
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;

    std::unordered_map<uint64_t, FileMetadata> file_map;             // file_id to metadata object

    std::vector<std::pair<uint64_t, uint64_t>> empty_access_list;    // list of <file_id, empty_count> in the current window, sorted on empty_count
    std::unordered_map<uint64_t, uint64_t> empty_access_index;       // file_id to index map for empty_access_list
    uint64_t empty_nonzero_count = 0;                                // non-zero entries form a prefix of empty_access_list

    std::vector<std::pair<uint64_t, uint64_t>> total_access_list;    // list of <file_id, probe_count> in the current window, sorted on probe_count
    std::unordered_map<uint64_t, uint64_t> total_access_index;       // file_id to index map for total_access_list

    RatioTree ratio_tree;
    // only valid for files with a non-zero total count
    auto ratio_key = [&](uint64_t file_id) {
        return RatioKey(get_adjusted_ratio(empty_access_list[empty_access_index[file_id]].second, total_access_list[total_access_index[file_id]].second), file_id);
    };

    // both return the updated count; lists are sorted in descending order of count
    auto increment_count = [](auto& access_list, auto& access_index, uint64_t file_id) {
        uint64_t index = access_index[file_id];
        access_list[index].second += 1;

        int64_t next_index;
        uint64_t access_count = access_list[index].second;
        for (next_index=index-1; next_index >= 0; next_index--)
            if (access_list[next_index].second >= access_count)
                break;

        next_index++;
        if (next_index != (int64_t)index) {
            std::swap(access_list[index], access_list[next_index]);
            access_index[access_list[index].first] = index;
            access_index[access_list[next_index].first] = next_index;
        }
        return access_count;
    };
    auto decrement_count = [](auto& access_list, auto& access_index, uint64_t file_id) {
        uint64_t index = access_index[file_id];
        access_list[index].second -= 1;

        uint64_t next_index;
        uint64_t access_count = access_list[index].second;
        for (next_index=index+1; next_index < access_list.size(); next_index++)
            if (access_list[next_index].second <= access_count)
                break;

        next_index--;
        if (next_index != index) {
            std::swap(access_list[index], access_list[next_index]);
            access_index[access_list[index].first] = index;
            access_index[access_list[next_index].first] = next_index;
        }
        return access_count;
    };

    // key is <file_id,lookup_id>
    // stores whether this block access resulted from a false-positive
    std::map<std::pair<uint64_t, uint64_t>, bool> unnecessary_access_map;

    uint64_t records_parsed = 0;
    std::vector<uint64_t> levelwise_total_count, levelwise_empty_count;

    // consumes the oldest lookahead entry; the caller must ensure the lookahead isn't empty
    auto consume_filter_lookahead = [&]() {
        CacheBlock lookahead_front;
        if (filter_cache->lookahead_.size())
            lookahead_front = filter_cache->lookahead_.front();
        BlockType type = lookahead_front.block_type_;

        if (filter_cache->advance_lookahead()) {
            result.hit_count++;
            result.hit_bytes += lookahead_front.size_;
            if (type == BlockType::kFilter) {
                result.filter_hit_count++;
                result.filter_hit_bytes += lookahead_front.size_;
            }
            else if (type == BlockType::kIndex) {
                result.index_hit_count++;
                result.index_hit_bytes += lookahead_front.size_;
            }
        }
        else {
            result.miss_count++;
            result.miss_bytes += lookahead_front.size_;
            if (type == BlockType::kFilter) {
                result.filter_miss_count++;
                result.filter_miss_bytes += lookahead_front.size_;
            }
            else if (type == BlockType::kIndex) {
                result.index_miss_count++;
                result.index_miss_bytes += lookahead_front.size_;
                std::pair<uint64_t, uint64_t> key_pair(lookahead_front.file_id_, lookahead_front.lookup_id_);
                if (unnecessary_access_map.find(key_pair) != unnecessary_access_map.end()) {
                    result.extra_read_count++;
                    result.extra_read_bytes += lookahead_front.size_;
                }
            }
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
            result.hit_bytes += lookahead_front.size_;
            result.data_hit_count++;
            result.data_hit_bytes += lookahead_front.size_;
        }
        else {
            result.miss_count++;
            result.miss_bytes += lookahead_front.size_;
            result.data_miss_count++;
            result.data_miss_bytes += lookahead_front.size_;
            miss = true;
        }

        if (++curr_access == config.series_per_record) {
            curr_access = 0;
            result.push();
        }

        std::pair<uint64_t, uint64_t> key_pair(lookahead_front.file_id_, lookahead_front.lookup_id_);
        auto it = unnecessary_access_map.find(key_pair);
        if (it == unnecessary_access_map.end()) return;
        if (miss) {
            result.extra_read_count++;
            result.extra_read_bytes += lookahead_front.size_;
        }
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

            uint16_t total_modules = filter_block_list.size();
            uint16_t used_modules = 0;
            uint16_t module_limit = total_modules;

            switch (config.module_limit_optimized) {
                case kCostModel:
                    module_limit = get_utility_based_module_count(config, total_modules, curr_probe, total_access_list, total_access_index, empty_access_list, empty_access_index, next_get_records.size());
                    break;
                case kFileInfoPercentile:
                    module_limit = get_module_count_file_percentile(total_modules, empty_access_index[curr_probe.file_id], empty_nonzero_count-1, empty_access_list[empty_access_index[curr_probe.file_id]].second);
                    break;
                case kFileInfoRatio: {
                    double empty_fraction = 0.0;
                    (levelwise_total_count.size() > curr_probe.level && levelwise_total_count[curr_probe.level])?
                        empty_fraction = 1.00 * levelwise_empty_count[curr_probe.level] / levelwise_total_count[curr_probe.level]
                    :empty_fraction = 0.0;
                    module_limit = get_module_count_file_ratio(empty_fraction, total_modules, ratio_tree, total_access_list[total_access_index[curr_probe.file_id]].second, empty_access_list[empty_access_index[curr_probe.file_id]].second);
                    break;
                }
                case kWorkInfo:
                    module_limit = get_module_count_workload_ratio(config.workload_empty_fraction, total_modules);
                    break;
                case kVanilla:
                default:
                    break;
            }

            auto outcome = curr_probe.file_outcome;
            bool real_verdict = (outcome==FileOutcome::kFoundValue) || (outcome==FileOutcome::kFoundMergeOperand) || (outcome==FileOutcome::kFoundTombstone);
            bool verdict = modular_filter_verdict(real_verdict, config.bits_per_key, total_modules, module_limit, used_modules);

            filter_block_list.resize(used_modules);

            for (const Block& filter_block: filter_block_list) {
                CacheBlock cache_block(BlockType::kFilter, curr_probe.file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes, get_record.lookup_id);
                filter_cache->add_lookahead(cache_block);
            }

            // filter-only: no data blocks are simulated, just count the extra reads
            if (config.mode == SimulationMode::kFilterOnly) {
                if (outcome == FileOutcome::kNotFound && verdict == true) {
                    result.extra_read_count += curr_probe.blocks.size();
                    for (const Block& curr_block : curr_probe.blocks) result.extra_read_bytes += curr_block.uncomp_bytes;
                }
                continue;
            }

            if (outcome == FileOutcome::kNotFound && verdict == false)
                continue;

            std::pair<uint64_t, uint64_t> key_pair(curr_probe.file_id, get_record.lookup_id);
            if (outcome == FileOutcome::kNotFound) unnecessary_access_map[key_pair] = true;

            for (const Block& curr_block: curr_probe.blocks) {
                CacheBlock cache_block(curr_block.type, curr_probe.file_id, curr_block.block_id, curr_probe.level, curr_block.uncomp_bytes, get_record.lookup_id);
                if (cache_block.block_type_ == BlockType::kIndex)
                    filter_cache->add_lookahead(cache_block);
                else if (cache_block.block_type_ == BlockType::kData)
                    data_cache->add_lookahead(cache_block);
            }
        }

        for (const Probe& curr_probe: get_record.probes) {
            if (config.module_limit_optimized == kFileInfoRatio) ratio_tree.erase(ratio_key(curr_probe.file_id));
            uint64_t total_count = decrement_count(total_access_list, total_access_index, curr_probe.file_id);
            levelwise_total_count[curr_probe.level]--;
            if (curr_probe.file_outcome == FileOutcome::kNotFound) {
                if (!decrement_count(empty_access_list, empty_access_index, curr_probe.file_id)) empty_nonzero_count--;
                levelwise_empty_count[curr_probe.level]--;
            }
            if (config.module_limit_optimized == kFileInfoRatio && total_count) ratio_tree.insert(ratio_key(curr_probe.file_id));
        }
        next_get_records.pop_front();
    };

    while (parser.parse_next_record(&curr_record)) {
        if ((++records_parsed & ((1<<20)-1)) == 0) std::cout << "parsed: " << records_parsed << std::endl;
        bool is_get = false;
        if (curr_record.record_type == kFileCreate) {
            FileMetadata new_metadata(curr_record);
            file_map[curr_record.file_id] = new_metadata;

            std::pair<uint64_t, uint64_t> input_pair(new_metadata.file_id, 0);
            total_access_list.push_back(input_pair);
            total_access_index[new_metadata.file_id] = total_access_list.size() - 1;
            empty_access_list.push_back(input_pair);
            empty_access_index[new_metadata.file_id] = empty_access_list.size() - 1;
        }
        else if (curr_record.record_type == kFileMove) {
            if (file_map.find(curr_record.file_id) != file_map.end())
                file_map[curr_record.file_id].level = curr_record.new_level;
        }
        else if (curr_record.record_type == kGet) {
            is_get = true;
            get_counter++;
            next_get_records.push_back(curr_record);
            for (const Probe& probe: curr_record.probes) {
                if (levelwise_total_count.size() <= probe.level) {
                    levelwise_total_count.resize(probe.level+1);
                    levelwise_empty_count.resize(probe.level+1);
                }
                if (config.module_limit_optimized == kFileInfoRatio && total_access_list[total_access_index[probe.file_id]].second) ratio_tree.erase(ratio_key(probe.file_id));
                increment_count(total_access_list, total_access_index, probe.file_id);
                levelwise_total_count[probe.level]++;
                if (probe.file_outcome == FileOutcome::kNotFound) {
                    if (increment_count(empty_access_list, empty_access_index, probe.file_id) == 1) empty_nonzero_count++;
                    levelwise_empty_count[probe.level]++;
                }
                if (config.module_limit_optimized == kFileInfoRatio) ratio_tree.insert(ratio_key(probe.file_id));
            }
        }
        if (!is_get || get_counter < config.modular_lookahead)
            continue;

        update_lookahead();
        while (filter_cache->get_lookahead_size() > filter_cache->get_max_lookahead())
            consume_filter_lookahead();
        while (config.mode != SimulationMode::kFilterOnly && data_cache->get_lookahead_size() > data_cache->get_max_lookahead())
            consume_data_lookahead();
    }

    while (next_get_records.size()) update_lookahead();
    while (filter_cache->get_lookahead_size())
        consume_filter_lookahead();
    while (config.mode != SimulationMode::kFilterOnly && data_cache->get_lookahead_size())
        consume_data_lookahead();

    std::string filter_name = filter_cache->get_name();
    std::string data_name = data_cache->get_name();
    if (filter_name == data_name)
        result.cache_policy_name = filter_name;
    else
        result.cache_policy_name = filter_name + "_" + data_name;
    result.cache_policy_name = "MODULAR_" + result.cache_policy_name;
    switch (config.module_limit_optimized) {
        case kCostModel: result.cache_policy_name = "COSTMODEL_" + result.cache_policy_name; break;
        case kFileInfoPercentile: result.cache_policy_name = "FILEINFO_PERCENTILE_" + result.cache_policy_name; break;
        case kFileInfoRatio: result.cache_policy_name = "FILEINFO_RATIO_" + result.cache_policy_name; break;
        case kWorkInfo: result.cache_policy_name = "WORKINFO_" + result.cache_policy_name; break;
        case kVanilla: result.cache_policy_name = "VANILLA_" + result.cache_policy_name; break;
        default: break;
    }
    if (curr_access) result.push();
    std::cout << "parsed: " << records_parsed << std::endl;
    return result;
}
