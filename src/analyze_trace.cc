#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include "record_parser.h"

struct AccessCounts {
    uint64_t total = 0;
    uint64_t empty = 0;
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Usage: bin/analyze_trace filename\n";
        exit(1);
    }

    // keyed on (level, file_id), so map order is the output order
    std::map<std::pair<uint8_t, uint64_t>, AccessCounts> access_map;

    RecordParser parser(argv[1]);
    Record curr_record;
    while (parser.parse_next_record(&curr_record)) {
        if (curr_record.record_type != kGet) continue;
        for (const Probe& probe : curr_record.probes) {
            AccessCounts& counts = access_map[{probe.level, probe.file_id}];
            counts.total++;
            if (probe.file_outcome == FileOutcome::kNotFound) counts.empty++;
        }
    }

    const std::string out_path = std::filesystem::path(argv[1]).stem().string() + "_file_access.csv";
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Cannot open " << out_path << "\n";
        exit(1);
    }

    out << "level,file_id,total_accesses,empty_accesses\n";
    for (const auto& [key, counts] : access_map)
        out << static_cast<int>(key.first) << ',' << key.second << ',' << counts.total << ',' << counts.empty << '\n';

    std::cout << "Wrote " << access_map.size() << " rows to " << out_path << "\n";
}
