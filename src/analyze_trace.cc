#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include "record_parser.h"

struct AccessCounts {
    uint64_t total = 0;
    uint64_t empty = 0;
};

struct FileInfo {
    std::string smallest;
    std::string largest;
    uint64_t size = 0;
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Usage: bin/analyze_trace filename\n";
        exit(1);
    }

    std::map<std::pair<uint8_t, uint64_t>, AccessCounts> access_map;   // (level, file_id) to counts
    std::unordered_map<uint64_t, FileInfo> file_map;                    // file_id to key range and size
    std::unordered_map<uint64_t, uint8_t> live_levels;                  // file_id to current level
    std::vector<uint64_t> placed;                                       // files created or moved since the last Get

    RecordParser parser(argv[1]);
    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if (curr_record.record_type == kFileCreate) {
            file_map[curr_record.file_id] = {curr_record.smallest_key, curr_record.largest_key, curr_record.file_size};
            live_levels[curr_record.file_id] = curr_record.level;
            placed.push_back(curr_record.file_id);
            continue;
        }
        if (curr_record.record_type == kFileMove) {
            live_levels[curr_record.file_id] = curr_record.new_level;
            placed.push_back(curr_record.file_id);
            continue;
        }
        if (curr_record.record_type == kFileDelete) {
            live_levels.erase(curr_record.file_id);
            continue;
        }
        if (curr_record.record_type != kGet) continue;

        // a file in place when a Get runs gets a row even if no Get ever probes it
        for (uint64_t file_id : placed) {
            auto it = live_levels.find(file_id);
            if (it != live_levels.end()) access_map.try_emplace({it->second, file_id});
        }
        placed.clear();

        for (const Probe& probe : curr_record.probes) {
            AccessCounts& counts = access_map[{probe.level, probe.file_id}];
            counts.total++;
            if (probe.file_outcome == FileOutcome::kNotFound) counts.empty++;
        }
    }

    static const FileInfo kNoInfo;
    auto info_of = [&](uint64_t file_id) -> const FileInfo& {
        auto it = file_map.find(file_id);
        return it == file_map.end() ? kNoInfo : it->second;
    };

    // stable, so files with identical key ranges keep their file_id order from the map
    std::vector<std::pair<std::pair<uint8_t, uint64_t>, AccessCounts>> rows(access_map.begin(), access_map.end());
    std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) {
        if (a.first.first != b.first.first) return a.first.first < b.first.first;
        const FileInfo& ka = info_of(a.first.second);
        const FileInfo& kb = info_of(b.first.second);
        return std::tie(ka.smallest, ka.largest) < std::tie(kb.smallest, kb.largest);
    });

    uint64_t missing_keys = 0;
    for (const auto& [key, counts] : rows)
        if (info_of(key.second).smallest.empty()) missing_keys++;
    if (missing_keys > 0)
        std::cerr << "Warning: " << missing_keys << " files have no key range; they sort first within their level\n";

    const std::string out_path = std::filesystem::path(argv[1]).stem().string() + "_file_access.csv";
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Cannot open " << out_path << "\n";
        exit(1);
    }

    out << "level,file_id,file_size,min_key,max_key,empty_accesses,total_accesses\n";
    for (const auto& [key, counts] : rows) {
        const FileInfo& info = info_of(key.second);
        out << static_cast<int>(key.first) << ',' << key.second << ',' << info.size << ',' << info.smallest << ','
            << info.largest << ',' << counts.empty << ',' << counts.total << '\n';
    }

    std::cout << "Wrote " << rows.size() << " rows to " << out_path << "\n";
}
