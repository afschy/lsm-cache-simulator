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
    OptimalCache optimal(config.cache_size, config.max_lookahead);
    LRUCache modular(config.cache_size);

    auto lru_res = filter_simulate_normal(argv[1], config, &lru);
    auto lfu_res = filter_simulate_normal(argv[1], config, &lfu);
    auto lfu_abs_res = filter_simulate_normal(argv[1], config, &lfu_absolute);
    auto optimal_res = filter_simulate_optimal(argv[1], config, &optimal);
    auto modular_res = filter_simulate_modular(argv[1], config, &modular);
    
    lru_res.generate_result_filename(config); lru_res.write_result();
    lfu_res.generate_result_filename(config); lfu_res.write_result();
    lfu_abs_res.generate_result_filename(config); lfu_abs_res.write_result();
    optimal_res.generate_result_filename(config); optimal_res.write_result();
    modular_res.generate_result_filename(config); modular_res.write_result();
}
