#pragma once
#include "cache.h"
#include "optimal_cache.h"
#include "simulation_config_result.h"

// cache only filters
// SimulationResult filter_simulate_normal(const char* trace_file_name, const SimulationConfig& config, Cache* cache);
// SimulationResult filter_simulate_optimal(const char* trace_file_name, const SimulationConfig& config, OptimalCache* cache);
// SimulationResult filter_simulate_optimal_modular(const char* trace_file_name, const SimulationConfig& config, OptimalCache* cache);
// SimulationResult filter_simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* cache);

// cache both filter and data blocks
SimulationResult simulate_normal(const char* trace_file_name, const SimulationConfig& config, Cache* filter_cache, Cache* data_cache);
SimulationResult simulate_optimal(const char* trace_file_name, const SimulationConfig& config, OptimalCache* filter_cache, OptimalCache* data_cache);
SimulationResult simulate_optimal_modular(const char* trace_file_name, const SimulationConfig& config, OptimalCache* filter_cache, OptimalCache* data_cache);
SimulationResult simulate_modular(const char* trace_file_name, const SimulationConfig& config, Cache* filter_cache, Cache* data_cache);
