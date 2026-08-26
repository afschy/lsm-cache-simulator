#include <iostream>
#include "simulators.h"
#include "lru_cache.h"
#include "lfu_cache.h"
#include "optimal_cache.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout<< "Usage: bin/lsm-sim filename\n";
        exit(0);
    }

    SimulationConfig config;
    config.read_from_file("config");

    LRUCache lru(config.cache_size);
    HeapLFUCache lfu(config.cache_size, false);
    HeapLFUCache lfu_absolute(config.cache_size, true);
    OptimalCache optimal(config.cache_size, config.optimal_lookahead);
    OptimalCache optimal_modular(config.cache_size, config.optimal_lookahead);

    auto lru_res = filter_simulate_normal(argv[1], config, &lru);
    std::cout << "LRU done" << std::endl;
    auto lfu_res = filter_simulate_normal(argv[1], config, &lfu);
    std::cout << "LFU done" << std::endl;
    auto lfu_abs_res = filter_simulate_normal(argv[1], config, &lfu_absolute);
    std::cout << "LFU-ABSOLUTE done" << std::endl;
    auto optimal_res = filter_simulate_optimal(argv[1], config, &optimal);
    std::cout << "OPTIMAL done" << std::endl;
    auto optimal_modular_res = filter_simulate_optimal_modular(argv[1], config, &optimal_modular);
    std::cout << "OPTIMAL-MODULAR done" << std::endl;
    
    lru_res.generate_result_filename(config); lru_res.write_result();
    lfu_res.generate_result_filename(config); lfu_res.write_result();
    lfu_abs_res.generate_result_filename(config); lfu_abs_res.write_result();
    optimal_res.generate_result_filename(config); optimal_res.write_result();
    optimal_modular_res.generate_result_filename(config); optimal_modular_res.write_result();
}
