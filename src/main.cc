#include <cstdlib>
#include <iostream>
#include <string>
#include "lfu_cache.h"
#include "lru_cache.h"
#include "optimal_cache.h"
#include "simulators.h"

enum SimulationMode {
    kFilterOnly,
    kFilterData,
};

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cout<< "Usage: bin/lsm-sim filename 0/1[filter-only/dual]\n";
        exit(1);
    }

    SimulationConfig config;
    config.read_from_file("config");

    SimulationMode mode = kFilterOnly;
    if (argc == 3) {
        int mode = std::stoi(argv[2]);
        switch (mode)
        {
        case 1:
            mode = kFilterData;
            break;
        default:
            mode = kFilterOnly;
            break;
        }
    }

    if (mode == kFilterOnly) {
        LRUCache lru(config.filter_cache_size);
        HeapLFUCache lfu(config.filter_cache_size, false);
        HeapLFUCache lfu_absolute(config.filter_cache_size, true);
        OptimalCache optimal(config.filter_cache_size, config.optimal_lookahead);
        LRUCache modular_lru(config.filter_cache_size);
        HeapLFUCache modular_lfu(config.filter_cache_size, false);
        HeapLFUCache modular_lfuabs(config.filter_cache_size, true);
        OptimalCache modular_optimal(config.filter_cache_size, config.optimal_lookahead);

        auto lru_res = filter_simulate_normal(argv[1], config, &lru);
        std::cout << "LRU done" << std::endl;
        auto lfu_res = filter_simulate_normal(argv[1], config, &lfu);
        std::cout << "LFU done" << std::endl;
        auto lfu_abs_res = filter_simulate_normal(argv[1], config, &lfu_absolute);
        std::cout << "LFU-ABSOLUTE done" << std::endl;
        auto optimal_res = filter_simulate_optimal(argv[1], config, &optimal);
        std::cout << "OPTIMAL done" << std::endl;
        auto modular_lru_res = filter_simulate_modular(argv[1], config, &modular_lru);
        std::cout << "MODULAR-LRU done" << std::endl;
        auto modular_lfu_res = filter_simulate_modular(argv[1], config, &modular_lfu);
        std::cout << "MODULAR-LFU done" << std::endl;
        auto modular_lfuabs_res = filter_simulate_modular(argv[1], config, &modular_lfuabs);
        std::cout << "MODULAR-LFUABS done" << std::endl;
        auto modular_optimal_res = filter_simulate_optimal_modular(argv[1], config, &modular_optimal);
        std::cout << "MODULAR-OPTIMAL done" << std::endl;
        
        lru_res.generate_result_filename(config); lru_res.write_result();
        lfu_res.generate_result_filename(config); lfu_res.write_result();
        lfu_abs_res.generate_result_filename(config); lfu_abs_res.write_result();
        optimal_res.generate_result_filename(config); optimal_res.write_result();
        modular_lru_res.generate_result_filename(config); modular_lru_res.write_result();
        modular_lfu_res.generate_result_filename(config); modular_lfu_res.write_result();
        modular_lfuabs_res.generate_result_filename(config); modular_lfuabs_res.write_result();
        modular_optimal_res.generate_result_filename(config); modular_optimal_res.write_result();
    }

    else if (mode == kFilterData) {
        LRUCache lru1(config.filter_cache_size), lru2(config.data_cache_size);
        HeapLFUCache lfu1(config.filter_cache_size, false), lfu2(config.data_cache_size, false);
        HeapLFUCache lfu_absolute1(config.filter_cache_size, true), lfu_absolute2(config.data_cache_size, true);
        OptimalCache optimal1(config.filter_cache_size, config.optimal_lookahead), optimal2(config.data_cache_size, config.optimal_lookahead);
        LRUCache modular_lru1(config.filter_cache_size), modular_lru2(config.data_cache_size);
        HeapLFUCache modular_lfu1(config.filter_cache_size, false), modular_lfu2(config.data_cache_size, false);
        HeapLFUCache modular_lfuabs1(config.filter_cache_size, true), modular_lfuabs2(config.data_cache_size, true);
        OptimalCache modular_optimal1(config.filter_cache_size, config.optimal_lookahead), modular_optimal2(config.data_cache_size, config.optimal_lookahead);

        auto lru_res = data_simulate_normal(argv[1], config, &lru1, &lru2);
        std::cout << "LRU done" << std::endl;
        auto lfu_res = data_simulate_normal(argv[1], config, &lfu1, &lfu2);
        std::cout << "LFU done" << std::endl;
        auto lfu_abs_res = data_simulate_normal(argv[1], config, &lfu_absolute1, &lfu_absolute2);
        std::cout << "LFU-ABSOLUTE done" << std::endl;
        auto optimal_res = data_simulate_optimal(argv[1], config, &optimal1, &optimal2);
        std::cout << "OPTIMAL done" << std::endl;
        auto modular_lru_res = data_simulate_modular(argv[1], config, &modular_lru1, &modular_lru2);
        std::cout << "MODULAR-LRU done" << std::endl;
        auto modular_lfu_res = data_simulate_modular(argv[1], config, &modular_lfu1, &modular_lfu2);
        std::cout << "MODULAR-LFU done" << std::endl;
        auto modular_lfuabs_res = data_simulate_modular(argv[1], config, &modular_lfuabs1, &modular_lfuabs2);
        std::cout << "MODULAR-LFUABS done" << std::endl;
        auto modular_optimal_res = data_simulate_optimal_modular(argv[1], config, &modular_optimal1, &modular_optimal2);
        std::cout << "MODULAR-OPTIMAL done" << std::endl;
        
        lru_res.generate_result_filename(config); lru_res.write_result();
        lfu_res.generate_result_filename(config); lfu_res.write_result();
        lfu_abs_res.generate_result_filename(config); lfu_abs_res.write_result();
        optimal_res.generate_result_filename(config); optimal_res.write_result();
        modular_lru_res.generate_result_filename(config); modular_lru_res.write_result();
        modular_lfu_res.generate_result_filename(config); modular_lfu_res.write_result();
        modular_lfuabs_res.generate_result_filename(config); modular_lfuabs_res.write_result();
        modular_optimal_res.generate_result_filename(config); modular_optimal_res.write_result();
    }
}
