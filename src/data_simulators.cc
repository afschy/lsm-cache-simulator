#include <cstdint>
#include <cstdlib>
#include <iostream>
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
            }

            curr_access++;
            if (curr_access == config.series_per_record) {
                curr_access = 0;
                result.push();
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

}

SimulationResult data_simulate_optimal_modular(const char* trace_file_name, const SimulationConfig& config, OptimalCache* filter_cache, OptimalCache* data_cache) {

}

SimulationResult data_simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* filter_cache, Cache* data_cache) {

}
