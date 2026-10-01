#include <cstdlib>
#include <iostream>
#include "lfu_cache.h"
#include "lru_cache.h"
#include "optimal_cache.h"
#include "simulation_config_result.h"
#include "simulators.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout<< "Usage: bin/lsm-sim filename\n";
        exit(1);
    }

    SimulationConfig config;
    config.read_from_file("config");

    // for normal bloom filter
    // LRUCache lru1(config.filter_cache_size), lru2(config.data_cache_size);
    // HeapLFUCache lfu1(config.filter_cache_size, false), lfu2(config.data_cache_size, false);
    // HeapLFUCache lfu_absolute1(config.filter_cache_size, true), lfu_absolute2(config.data_cache_size, true);
    // OptimalCache optimal1(config.filter_cache_size, config.optimal_lookahead), optimal2(config.data_cache_size, config.optimal_lookahead);

    // vanilla modular bloom filter (no module count optimization)
    LRUCache vanilla_modular_lru1(config.filter_cache_size), vanilla_modular_lru2(config.data_cache_size);
    HeapLFUCache vanilla_modular_lfu1(config.filter_cache_size, false), vanilla_modular_lfu2(config.data_cache_size, false);
    HeapLFUCache vanilla_modular_lfuabs1(config.filter_cache_size, true), vanilla_modular_lfuabs2(config.data_cache_size, true);
    OptimalCache vanilla_modular_optimal1(config.filter_cache_size, config.optimal_lookahead), vanilla_modular_optimal2(config.data_cache_size, config.optimal_lookahead);

    // cost-model modular bloom filter
    LRUCache costmodel_modular_lru1(config.filter_cache_size), costmodel_modular_lru2(config.data_cache_size);
    HeapLFUCache costmodel_modular_lfu1(config.filter_cache_size, false), costmodel_modular_lfu2(config.data_cache_size, false);
    HeapLFUCache costmodel_modular_lfuabs1(config.filter_cache_size, true), costmodel_modular_lfuabs2(config.data_cache_size, true);
    OptimalCache costmodel_modular_optimal1(config.filter_cache_size, config.optimal_lookahead), costmodel_modular_optimal2(config.data_cache_size, config.optimal_lookahead);

    // file-info modular bloom filter
    LRUCache fileinfo_modular_lru1(config.filter_cache_size), fileinfo_modular_lru2(config.data_cache_size);
    HeapLFUCache fileinfo_modular_lfu1(config.filter_cache_size, false), fileinfo_modular_lfu2(config.data_cache_size, false);
    HeapLFUCache fileinfo_modular_lfuabs1(config.filter_cache_size, true), fileinfo_modular_lfuabs2(config.data_cache_size, true);
    OptimalCache fileinfo_modular_optimal1(config.filter_cache_size, config.optimal_lookahead), fileinfo_modular_optimal2(config.data_cache_size, config.optimal_lookahead);

    // workload-info modular bloom filter
    LRUCache workinfo_modular_lru1(config.filter_cache_size), workinfo_modular_lru2(config.data_cache_size);
    HeapLFUCache workinfo_modular_lfu1(config.filter_cache_size, false), workinfo_modular_lfu2(config.data_cache_size, false);
    HeapLFUCache workinfo_modular_lfuabs1(config.filter_cache_size, true), workinfo_modular_lfuabs2(config.data_cache_size, true);
    OptimalCache workinfo_modular_optimal1(config.filter_cache_size, config.optimal_lookahead), workinfo_modular_optimal2(config.data_cache_size, config.optimal_lookahead);

    // normal bloom filter
    // auto lru_res = simulate_normal(argv[1], config, &lru1, &lru2);
    // std::cout << "LRU done" << std::endl;
    // lru_res.generate_result_filename(config); lru_res.write_result();
    //
    // auto lfu_res = simulate_normal(argv[1], config, &lfu1, &lfu2);
    // std::cout << "LFU done" << std::endl;
    // lfu_res.generate_result_filename(config); lfu_res.write_result();
    //
    // auto lfu_abs_res = simulate_normal(argv[1], config, &lfu_absolute1, &lfu_absolute2);
    // std::cout << "LFU-ABSOLUTE done" << std::endl;
    // lfu_abs_res.generate_result_filename(config); lfu_abs_res.write_result();
    //
    // auto optimal_res = simulate_optimal(argv[1], config, &optimal1, &optimal2);
    // std::cout << "OPTIMAL done" << std::endl;
    // optimal_res.generate_result_filename(config); optimal_res.write_result();

    // vanilla modular bloom filter (no module count optimization)
    config.module_limit_optimized = kVanilla;

    auto vanilla_modular_lru_res = simulate_modular(argv[1], config, &vanilla_modular_lru1, &vanilla_modular_lru2);
    std::cout << "VANILLA-MODULAR-LRU done" << std::endl;
    vanilla_modular_lru_res.generate_result_filename(config); vanilla_modular_lru_res.write_result();

    auto vanilla_modular_lfu_res = simulate_modular(argv[1], config, &vanilla_modular_lfu1, &vanilla_modular_lfu2);
    std::cout << "VANILLA-MODULAR-LFU done" << std::endl;
    vanilla_modular_lfu_res.generate_result_filename(config); vanilla_modular_lfu_res.write_result();

    auto vanilla_modular_lfuabs_res = simulate_modular(argv[1], config, &vanilla_modular_lfuabs1, &vanilla_modular_lfuabs2);
    std::cout << "VANILLA-MODULAR-LFUABS done" << std::endl;
    vanilla_modular_lfuabs_res.generate_result_filename(config); vanilla_modular_lfuabs_res.write_result();

    auto vanilla_modular_optimal_res = simulate_optimal_modular(argv[1], config, &vanilla_modular_optimal1, &vanilla_modular_optimal2);
    std::cout << "VANILLA-MODULAR-OPTIMAL done" << std::endl;
    vanilla_modular_optimal_res.generate_result_filename(config); vanilla_modular_optimal_res.write_result();

    // cost-model modular bloom filter
    config.module_limit_optimized = kCostModel;

    auto costmodel_modular_lru_res = simulate_modular(argv[1], config, &costmodel_modular_lru1, &costmodel_modular_lru2);
    std::cout << "COSTMODEL-MODULAR-LRU done" << std::endl;
    costmodel_modular_lru_res.generate_result_filename(config); costmodel_modular_lru_res.write_result();

    auto costmodel_modular_lfu_res = simulate_modular(argv[1], config, &costmodel_modular_lfu1, &costmodel_modular_lfu2);
    std::cout << "COSTMODEL-MODULAR-LFU done" << std::endl;
    costmodel_modular_lfu_res.generate_result_filename(config); costmodel_modular_lfu_res.write_result();

    auto costmodel_modular_lfuabs_res = simulate_modular(argv[1], config, &costmodel_modular_lfuabs1, &costmodel_modular_lfuabs2);
    std::cout << "COSTMODEL-MODULAR-LFUABS done" << std::endl;
    costmodel_modular_lfuabs_res.generate_result_filename(config); costmodel_modular_lfuabs_res.write_result();

    auto costmodel_modular_optimal_res = simulate_optimal_modular(argv[1], config, &costmodel_modular_optimal1, &costmodel_modular_optimal2);
    std::cout << "COSTMODEL-MODULAR-OPTIMAL done" << std::endl;
    costmodel_modular_optimal_res.generate_result_filename(config); costmodel_modular_optimal_res.write_result();

    // file-info modular bloom filter
    config.module_limit_optimized = kFileInfo;

    auto fileinfo_modular_lru_res = simulate_modular(argv[1], config, &fileinfo_modular_lru1, &fileinfo_modular_lru2);
    std::cout << "FILEINFO-MODULAR-LRU done" << std::endl;
    fileinfo_modular_lru_res.generate_result_filename(config); fileinfo_modular_lru_res.write_result();

    auto fileinfo_modular_lfu_res = simulate_modular(argv[1], config, &fileinfo_modular_lfu1, &fileinfo_modular_lfu2);
    std::cout << "FILEINFO-MODULAR-LFU done" << std::endl;
    fileinfo_modular_lfu_res.generate_result_filename(config); fileinfo_modular_lfu_res.write_result();

    auto fileinfo_modular_lfuabs_res = simulate_modular(argv[1], config, &fileinfo_modular_lfuabs1, &fileinfo_modular_lfuabs2);
    std::cout << "FILEINFO-MODULAR-LFUABS done" << std::endl;
    fileinfo_modular_lfuabs_res.generate_result_filename(config); fileinfo_modular_lfuabs_res.write_result();

    auto fileinfo_modular_optimal_res = simulate_optimal_modular(argv[1], config, &fileinfo_modular_optimal1, &fileinfo_modular_optimal2);
    std::cout << "FILEINFO-MODULAR-OPTIMAL done" << std::endl;
    fileinfo_modular_optimal_res.generate_result_filename(config); fileinfo_modular_optimal_res.write_result();

    // workload-info modular bloom filter
    config.module_limit_optimized = kWorkInfo;

    auto workinfo_modular_lru_res = simulate_modular(argv[1], config, &workinfo_modular_lru1, &workinfo_modular_lru2);
    std::cout << "WORKINFO-MODULAR-LRU done" << std::endl;
    workinfo_modular_lru_res.generate_result_filename(config); workinfo_modular_lru_res.write_result();

    auto workinfo_modular_lfu_res = simulate_modular(argv[1], config, &workinfo_modular_lfu1, &workinfo_modular_lfu2);
    std::cout << "WORKINFO-MODULAR-LFU done" << std::endl;
    workinfo_modular_lfu_res.generate_result_filename(config); workinfo_modular_lfu_res.write_result();

    auto workinfo_modular_lfuabs_res = simulate_modular(argv[1], config, &workinfo_modular_lfuabs1, &workinfo_modular_lfuabs2);
    std::cout << "WORKINFO-MODULAR-LFUABS done" << std::endl;
    workinfo_modular_lfuabs_res.generate_result_filename(config); workinfo_modular_lfuabs_res.write_result();

    auto workinfo_modular_optimal_res = simulate_optimal_modular(argv[1], config, &workinfo_modular_optimal1, &workinfo_modular_optimal2);
    std::cout << "WORKINFO-MODULAR-OPTIMAL done" << std::endl;
    workinfo_modular_optimal_res.generate_result_filename(config); workinfo_modular_optimal_res.write_result();
}
