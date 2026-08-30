#pragma once
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

struct SimulationConfig {
    uint64_t filter_cache_size = 512 << 10;
    uint64_t data_cache_size = 512 << 10;
    uint64_t default_block_size = 4096;
    uint8_t shard_count = 1;
    uint8_t bits_per_key = 16;
    uint16_t optimal_lookahead = 1000;
    uint16_t modular_lookahead = 100;
    uint32_t series_per_record = 10000;
    uint32_t filter_keys_per_block = default_block_size * 8 / bits_per_key;

    void read_from_file(const char* filename) {
        std::ifstream config_file(filename);
        if (!config_file) {
            fprintf(stderr, "Could not open config file %s\n", filename);
            exit(1);
        }

        std::string read_line, key, value, extra;
        while (std::getline(config_file, read_line)) {
            std::istringstream line_stream(read_line);
            if (!(line_stream >> key >> value)) continue;
            if (line_stream >> extra) continue;

            uint64_t number = 0;
            auto result = std::from_chars(value.data(), value.data() + value.size(), number);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) continue;

            if (key == "cache_size") { 
                filter_cache_size = number;
                data_cache_size = number;
            }
            else if (key == "filter_cache_size") filter_cache_size = number;
            else if (key == "data_cache_size") data_cache_size = number;
            else if (key == "default_block_size") default_block_size = number;
            else if (key == "shard_count") {
                if (number <= UINT8_MAX) shard_count = number;
            }
            else if (key == "optimal_lookahead") {
                if (number <= UINT16_MAX) optimal_lookahead = number;
            }
            else if (key == "modular_lookahead") {
                if (number <= UINT16_MAX) modular_lookahead = number;
            }
            else if (key == "bits_per_key") {
                if (number <= UINT8_MAX) bits_per_key = number;
            }
            else if (key == "series_per_record") {
                if (number <= UINT32_MAX) series_per_record = number;
            }
        }
        filter_keys_per_block = default_block_size * 8 / bits_per_key;
    }
};

struct SimulationResult {
    uint32_t hit_count = 0;
    uint32_t miss_count = 0;
    uint32_t extra_read_count = 0;
    std::vector<uint32_t> hit_series;
    std::vector<uint32_t> miss_series;
    std::vector<uint32_t> extra_read_series;
    uint32_t series_per_record = 10000;

    std::string result_filename = "result.txt";
    std::string cache_policy_name = "default";

    void push() {
        hit_series.push_back(hit_count);
        miss_series.push_back(miss_count);
        extra_read_series.push_back(extra_read_count);
    }

    void generate_result_filename(const SimulationConfig& config) {
        result_filename = cache_policy_name
                        + "_fcs" + std::to_string(config.filter_cache_size)
                        + "_dcs" + std::to_string(config.filter_cache_size)
                        + "_bs" + std::to_string(config.default_block_size)
                        + "_sc" + std::to_string(config.shard_count)
                        + "_look" + std::to_string(config.optimal_lookahead)
                        + "_mlook" + std::to_string(config.modular_lookahead)
                        + "_bpk" + std::to_string(config.bits_per_key)
                        + ".log";
        series_per_record = config.series_per_record;
    }

    void write_result() {
        FILE* file = fopen(result_filename.c_str(), "w");
        fprintf(file, "total_count %u\n", hit_count+miss_count);
        fprintf(file, "hit_count %u\n", hit_count);
        fprintf(file, "miss_count %u\n", miss_count);
        fprintf(file, "extra_read_count %u\n", extra_read_count);
        fprintf(file, "series_per_record %u\n", series_per_record);
        fprintf(file, "hit_series");
        for (uint32_t curr_hit : hit_series)
            fprintf(file, " %u", curr_hit);
        fprintf(file, "\nmiss_series");
        for (uint32_t curr_miss : miss_series)
            fprintf(file, " %u", curr_miss);
        fprintf(file, "\nextra_read_series");
        for (uint32_t curr_extra : extra_read_series)
            fprintf(file, " %u", curr_extra);
        fprintf(file, "\n");
        fclose(file);
    }
};