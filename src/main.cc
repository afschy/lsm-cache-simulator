#include <cstdlib>
#include <iostream>
#include "lfu_cache.h"
#include "lru_cache.h"
#include "optimal_cache.h"
#include "simulators.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout<< "Usage: bin/lsm-sim filename\n";
        exit(1);
    }

    SimulationConfig config;
    config.read_from_file("config");

    // for normal bloom filter
    LRUCache lru1(config.filter_cache_size), lru2(config.data_cache_size);
    HeapLFUCache lfu1(config.filter_cache_size, false), lfu2(config.data_cache_size, false);
    HeapLFUCache lfu_absolute1(config.filter_cache_size, true), lfu_absolute2(config.data_cache_size, true);
    OptimalCache optimal1(config.filter_cache_size, config.optimal_lookahead), optimal2(config.data_cache_size, config.optimal_lookahead);

    // for modular bloom filter without our optimization
    LRUCache modular_lru1(config.filter_cache_size), modular_lru2(config.data_cache_size);
    HeapLFUCache modular_lfu1(config.filter_cache_size, false), modular_lfu2(config.data_cache_size, false);
    HeapLFUCache modular_lfuabs1(config.filter_cache_size, true), modular_lfuabs2(config.data_cache_size, true);
    OptimalCache modular_optimal1(config.filter_cache_size, config.optimal_lookahead), modular_optimal2(config.data_cache_size, config.optimal_lookahead);

    // for optimized modular bloom filter
    LRUCache optimized_modular_lru1(config.filter_cache_size), optimized_modular_lru2(config.data_cache_size);
    HeapLFUCache optimized_modular_lfu1(config.filter_cache_size, false), optimized_modular_lfu2(config.data_cache_size, false);
    HeapLFUCache optimized_modular_lfuabs1(config.filter_cache_size, true), optimized_modular_lfuabs2(config.data_cache_size, true);
    OptimalCache optimized_modular_optimal1(config.filter_cache_size, config.optimal_lookahead), optimized_modular_optimal2(config.data_cache_size, config.optimal_lookahead);

    // normal bloom filter
    auto lru_res = simulate_normal(argv[1], config, &lru1, &lru2);
    std::cout << "LRU done" << std::endl;
    lru_res.generate_result_filename(config); lru_res.write_result();

    auto lfu_res = simulate_normal(argv[1], config, &lfu1, &lfu2);
    std::cout << "LFU done" << std::endl;
    lfu_res.generate_result_filename(config); lfu_res.write_result();

    auto lfu_abs_res = simulate_normal(argv[1], config, &lfu_absolute1, &lfu_absolute2);
    std::cout << "LFU-ABSOLUTE done" << std::endl;
    lfu_abs_res.generate_result_filename(config); lfu_abs_res.write_result();

    auto optimal_res = simulate_optimal(argv[1], config, &optimal1, &optimal2);
    std::cout << "OPTIMAL done" << std::endl;
    optimal_res.generate_result_filename(config); optimal_res.write_result();

    // modular bloom filter
    config.module_limit_optimized = 0;

    auto modular_lru_res = simulate_modular(argv[1], config, &modular_lru1, &modular_lru2);
    std::cout << "MODULAR-LRU done" << std::endl;
    modular_lru_res.generate_result_filename(config); modular_lru_res.write_result();

    auto modular_lfu_res = simulate_modular(argv[1], config, &modular_lfu1, &modular_lfu2);
    std::cout << "MODULAR-LFU done" << std::endl;
    modular_lfu_res.generate_result_filename(config); modular_lfu_res.write_result();

    auto modular_lfuabs_res = simulate_modular(argv[1], config, &modular_lfuabs1, &modular_lfuabs2);
    std::cout << "MODULAR-LFUABS done" << std::endl;
    modular_lfuabs_res.generate_result_filename(config); modular_lfuabs_res.write_result();

    auto modular_optimal_res = simulate_optimal_modular(argv[1], config, &modular_optimal1, &modular_optimal2);
    std::cout << "MODULAR-OPTIMAL done" << std::endl;
    modular_optimal_res.generate_result_filename(config); modular_optimal_res.write_result();

    // our optimized version of modular bloom filter
    config.module_limit_optimized = 1;

    auto optimized_modular_lru_res = simulate_modular(argv[1], config, &optimized_modular_lru1, &optimized_modular_lru2);
    std::cout << "MODULAR-LRU done" << std::endl;
    optimized_modular_lru_res.generate_result_filename(config); optimized_modular_lru_res.write_result();

    auto optimized_modular_lfu_res = simulate_modular(argv[1], config, &optimized_modular_lfu1, &optimized_modular_lfu2);
    std::cout << "MODULAR-LFU done" << std::endl;
    optimized_modular_lfu_res.generate_result_filename(config); optimized_modular_lfu_res.write_result();

    auto optimized_modular_lfuabs_res = simulate_modular(argv[1], config, &optimized_modular_lfuabs1, &optimized_modular_lfuabs2);
    std::cout << "MODULAR-LFUABS done" << std::endl;
    optimized_modular_lfuabs_res.generate_result_filename(config); optimized_modular_lfuabs_res.write_result();

    auto optimized_modular_optimal_res = simulate_optimal_modular(argv[1], config, &optimized_modular_optimal1, &optimized_modular_optimal2);
    std::cout << "MODULAR-OPTIMAL done" << std::endl;
    optimized_modular_optimal_res.generate_result_filename(config); optimized_modular_optimal_res.write_result();
}
