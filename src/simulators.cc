#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <vector>
#include "cache.h"
#include "record_parser.h"
#include "simulation_config_result.h"
#include "simulator_utils.h"
#include "simulators.h"

SimulationResult simulate_normal(const char* trace_file_name, const SimulationConfig& config, Cache* filter_cache, Cache* data_cache) {
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
                filter_cache->remove_file_blocks(it->second);
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
                    result.hit_bytes += cache_block.size_;
                    result.filter_hit_count++;
                    result.filter_hit_bytes += cache_block.size_;
                }
                else {
                    filter_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.miss_bytes += cache_block.size_;
                    result.filter_miss_count++;
                    result.filter_miss_bytes += cache_block.size_;
                }
                
                if (++curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }

            // filter-only: no data blocks are simulated, just count the extra reads
            if (config.mode == SimulationMode::kFilterOnly) {
                if (curr_probe.file_outcome == FileOutcome::kNotFound && get_filter_false_positive(config.bits_per_key)) {
                    result.extra_read_count += curr_probe.blocks.size();
                    for (const Block& curr_block : curr_probe.blocks) result.extra_read_bytes += curr_block.uncomp_bytes;
                }
                continue;
            }

            auto outcome = curr_probe.file_outcome;
            if (outcome == FileOutcome::kNotFound) {
                bool bloom_result = get_filter_false_positive(config.bits_per_key);
                if (!bloom_result) continue;    // no false positive, no blocks read
            }
            
            for (const Block& curr_block : curr_probe.blocks) {
                CacheBlock cache_block(curr_block.type, curr_probe.file_id, curr_block.block_id, curr_probe.level, curr_block.uncomp_bytes, curr_record.lookup_id);

                if (cache_block.block_type_ == BlockType::kIndex) {
                    if (filter_cache->block_exists(cache_block)) {
                        filter_cache->record_access(cache_block);
                        result.hit_count++;
                        result.hit_bytes += cache_block.size_;
                        result.index_hit_count++;
                        result.index_hit_bytes += cache_block.size_;
                    }
                    else {
                        filter_cache->insert_block(cache_block);
                        result.miss_count++;
                        result.miss_bytes += cache_block.size_;
                        result.index_miss_count++;
                        result.index_miss_bytes += cache_block.size_;
                        if (outcome == FileOutcome::kNotFound) {
                            result.extra_read_count++;
                            result.extra_read_bytes += cache_block.size_;
                        }
                    }

                    if (++curr_access == config.series_per_record) {
                        curr_access = 0;
                        result.push();
                    }
                    continue;
                }
                if (cache_block.block_type_ != BlockType::kData) continue;

                // Data block handling logic
                if (data_cache->block_exists(cache_block)) {
                    data_cache->record_access(cache_block);
                    result.hit_count++;
                    result.hit_bytes += cache_block.size_;
                    result.data_hit_count++;
                    result.data_hit_bytes += cache_block.size_;
                }
                else {
                    data_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.miss_bytes += cache_block.size_;
                    result.data_miss_count++;
                    result.data_miss_bytes += cache_block.size_;
                    if (outcome == FileOutcome::kNotFound) {
                        result.extra_read_count++;
                        result.extra_read_bytes += cache_block.size_;
                    }
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

SimulationResult simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* filter_cache, Cache* data_cache) {
    RecordParser parser(trace_file_name);
    SimulationResult result;
    uint16_t curr_access = 0;

    std::unordered_map<uint64_t, FileMetadata> file_map;             // file_id to metadata object

    std::vector<std::pair<uint64_t, uint64_t>> empty_access_list;    // list of <file_id, empty_count> sorted on empty_count
    std::unordered_map<uint64_t, uint64_t> empty_access_index;       // file_id to index map for empty_access_list
    uint64_t empty_nonzero_count = 0;                                // non-zero entries form a prefix of empty_access_list

    std::vector<std::pair<uint64_t, uint64_t>> total_access_list;    // list of <file_id, empty_count> sorted on empty_count
    std::unordered_map<uint64_t, uint64_t> total_access_index;       // file_id to index map for empty_access_list

    RatioTree ratio_tree;
    // only valid for files with a non-zero total count
    auto ratio_key = [&](uint64_t file_id) {
        return RatioKey(get_adjusted_ratio(empty_access_list[empty_access_index[file_id]].second, total_access_list[total_access_index[file_id]].second), file_id);
    };

    uint64_t records_parsed = 0;
    uint64_t lookup_count = 0;  // completed get lookups
    std::vector<uint64_t> levelwise_total_count, levelwise_empty_count;

    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if ((++records_parsed & ((1<<20)-1)) == 0) std::cout << "parsed: " << records_parsed << std::endl;
        if (curr_record.record_type == kIterator) continue;
        if (curr_record.record_type == kFileCreate) {
            FileMetadata new_metadata(curr_record);
            new_metadata.creation_lookup = lookup_count;
            file_map[curr_record.file_id] = new_metadata;

            std::pair<uint64_t, uint64_t> input_pair(new_metadata.file_id, 0);
            total_access_list.push_back(input_pair);
            total_access_index[new_metadata.file_id] = total_access_list.size() - 1;
            empty_access_list.push_back(input_pair);
            empty_access_index[new_metadata.file_id] = empty_access_list.size() - 1;

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
                filter_cache->remove_file_blocks(it->second);
                data_cache->remove_file_blocks(it->second);
                it->second.deleted = true;
            }

            if (config.module_limit_optimized == kFileInfoRatio && total_access_list[total_access_index[curr_record.file_id]].second) ratio_tree.erase(ratio_key(curr_record.file_id));
            if (empty_access_list[empty_access_index[curr_record.file_id]].second) empty_nonzero_count--;
            empty_access_list.erase(empty_access_list.begin() + empty_access_index[curr_record.file_id]);
            for (size_t i=empty_access_index[curr_record.file_id]; i<empty_access_list.size(); i++)
                empty_access_index[empty_access_list[i].first] = i;
            empty_access_index.erase(curr_record.file_id);

            total_access_list.erase(total_access_list.begin() + total_access_index[curr_record.file_id]);
            for (size_t i=total_access_index[curr_record.file_id]; i<total_access_list.size(); i++)
                total_access_index[total_access_list[i].first] = i;
            total_access_index.erase(curr_record.file_id);
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
            uint16_t total_modules = filter_blocks.size();
            uint16_t used_modules = 0;
            uint16_t module_limit = total_modules;

            switch (config.module_limit_optimized) {
                case kCostModel:
                    module_limit = get_utility_based_module_count(config, total_modules, curr_probe, total_access_list, total_access_index, empty_access_list, empty_access_index, lookup_count - file_metadata.creation_lookup, file_metadata.deleted);
                    break;
                case kFileInfoPercentile:
                    module_limit = file_metadata.deleted ? total_modules : get_module_count_file_percentile(total_modules, empty_access_index[file_id], empty_nonzero_count-1,  empty_access_list[empty_access_index[file_id]].second, file_metadata.deleted);
                    break;
                case kFileInfoRatio: {
                    double empty_fraction = 0.0;
                    (levelwise_total_count.size() > curr_probe.level && levelwise_total_count[curr_probe.level])?
                        empty_fraction = 1.00 * levelwise_empty_count[curr_probe.level] / levelwise_total_count[curr_probe.level]
                    :empty_fraction = 0.0;
                    module_limit = file_metadata.deleted ? total_modules : get_module_count_file_ratio(empty_fraction, total_modules, ratio_tree, total_access_list[total_access_index[file_id]].second, empty_access_list[empty_access_index[file_id]].second, file_metadata.deleted);
                    break;
                }
                case kWorkInfo: {
                    double empty_fraction = 0.0;
                    (levelwise_total_count.size() > curr_probe.level && levelwise_total_count[curr_probe.level])?
                        empty_fraction = 1.00 * levelwise_empty_count[curr_probe.level] / levelwise_total_count[curr_probe.level]
                    :empty_fraction = 0.0;
                    module_limit = get_module_count_workload_ratio(empty_fraction, total_modules, file_metadata.deleted);
                    break;
                }
                case kVanilla:
                default:
                    break;
            }

            if (levelwise_total_count.size() <= curr_probe.level) {
                levelwise_total_count.resize(curr_probe.level+1);
                levelwise_empty_count.resize(curr_probe.level+1);
            }
            if (!file_metadata.deleted) {
                uint64_t index = total_access_index[file_id];
                if (config.module_limit_optimized == kFileInfoRatio && total_access_list[index].second) ratio_tree.erase(ratio_key(file_id));
                total_access_list[index].second += 1;

                int64_t next_index;
                uint64_t access_count = total_access_list[index].second;
                for (next_index=index-1; next_index >= 0; next_index--)
                    if (total_access_list[next_index].second >= access_count)
                        break;
                
                next_index++;
                if (next_index != (int64_t)index) {
                    std::swap(total_access_list[index], total_access_list[next_index]);
                    total_access_index[total_access_list[index].first] = index;
                    total_access_index[total_access_list[next_index].first] = next_index;
                }
                levelwise_total_count[curr_probe.level]++;
            }
            if (curr_probe.file_outcome == FileOutcome::kNotFound && !file_metadata.deleted) {
                uint64_t index = empty_access_index[file_id];
                if (++empty_access_list[index].second == 1) empty_nonzero_count++;

                int64_t next_index;
                uint64_t access_count = empty_access_list[index].second;
                for (next_index=index-1; next_index >= 0; next_index--)
                    if (empty_access_list[next_index].second >= access_count)
                        break;
                
                next_index++;
                if (next_index != (int64_t)index) {
                    std::swap(empty_access_list[index], empty_access_list[next_index]);
                    empty_access_index[empty_access_list[index].first] = index;
                    empty_access_index[empty_access_list[next_index].first] = next_index;
                }
                levelwise_empty_count[curr_probe.level]++;
            }
            if (config.module_limit_optimized == kFileInfoRatio && !file_metadata.deleted) ratio_tree.insert(ratio_key(file_id));

            auto outcome = curr_probe.file_outcome;
            bool real_verdict = (outcome==FileOutcome::kFoundValue) || (outcome==FileOutcome::kFoundMergeOperand) || (outcome==FileOutcome::kFoundTombstone);
            bool verdict = modular_filter_verdict(real_verdict, config.bits_per_key, total_modules, module_limit, used_modules);
            
            filter_blocks.resize(used_modules);
            for (const Block& filter_block: filter_blocks) {
                CacheBlock cache_block(BlockType::kFilter, file_id, filter_block.block_id, curr_probe.level, filter_block.uncomp_bytes, curr_record.lookup_id);
                if (filter_cache->block_exists(cache_block)) {
                    filter_cache->record_access(cache_block);
                    result.hit_count++;
                    result.hit_bytes += cache_block.size_;
                    result.filter_hit_count++;
                    result.filter_hit_bytes += cache_block.size_;
                }
                else {
                    filter_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.miss_bytes += cache_block.size_;
                    result.filter_miss_count++;
                    result.filter_miss_bytes += cache_block.size_;
                }

                curr_access++;
                if (curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }

            // filter-only: no data blocks are simulated, just count the extra reads
            if (config.mode == SimulationMode::kFilterOnly) {
                if (curr_probe.file_outcome == FileOutcome::kNotFound && verdict == true) {
                    result.extra_read_count += curr_probe.blocks.size();
                    for (const Block& curr_block : curr_probe.blocks) result.extra_read_bytes += curr_block.uncomp_bytes;
                }
                continue;
            }

            if (outcome == FileOutcome::kNotFound && verdict == false)
                continue;
            
            for (const Block& curr_block : curr_probe.blocks) {
                CacheBlock cache_block(curr_block.type, curr_probe.file_id, curr_block.block_id, curr_probe.level, curr_block.uncomp_bytes, curr_record.lookup_id);

                if (cache_block.block_type_ == BlockType::kIndex) {
                    if (filter_cache->block_exists(cache_block)) {
                        filter_cache->record_access(cache_block);
                        result.hit_count++;
                        result.hit_bytes += cache_block.size_;
                        result.index_hit_count++;
                        result.index_hit_bytes += cache_block.size_;
                    }
                    else {
                        filter_cache->insert_block(cache_block);
                        result.miss_count++;
                        result.miss_bytes += cache_block.size_;
                        result.index_miss_count++;
                        result.index_miss_bytes += cache_block.size_;
                        if (outcome == FileOutcome::kNotFound) {
                            result.extra_read_count++;
                            result.extra_read_bytes += cache_block.size_;
                        }
                    }

                    if (++curr_access == config.series_per_record) {
                        curr_access = 0;
                        result.push();
                    }
                    continue;
                }
                if (cache_block.block_type_ != BlockType::kData) continue;

                // Data block handling logic
                if (data_cache->block_exists(cache_block)) {
                    data_cache->record_access(cache_block);
                    result.hit_count++;
                    result.hit_bytes += cache_block.size_;
                    result.data_hit_count++;
                    result.data_hit_bytes += cache_block.size_;
                }
                else {
                    data_cache->insert_block(cache_block);
                    result.miss_count++;
                    result.miss_bytes += cache_block.size_;
                    result.data_miss_count++;
                    result.data_miss_bytes += cache_block.size_;
                    if (outcome == FileOutcome::kNotFound) {
                        result.extra_read_count++;
                        result.extra_read_bytes += cache_block.size_;
                    }
                }

                if (++curr_access == config.series_per_record) {
                    curr_access = 0;
                    result.push();
                }
            }
        }
        if (curr_record.record_type == kGet) lookup_count++;
    }

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

