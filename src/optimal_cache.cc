#include <cstdlib>
#include <iostream>
#include <map>
#include "optimal_cache.h"

bool OptimalCache::advance_lookahead() {
    auto block = lookahead_.front();
    if (cache_map_.find(block.hash()) != cache_map_.end()) {
        record_access(block);
        return true;
    }
    insert_block(block);
    return false;
}

void OptimalCache::insert_block(CacheBlock block) {
    if (block.size_ > max_size_) {
        std::cerr << "Error: block size is bigger than cache size\n";
        exit(1);
    }

    CacheBlock next_block = lookahead_.front();
    if (block.hash() != next_block.hash()) {
        std::cerr << "Error: insertion order doesn't match lookahead\n";
        exit(1);
    }

    if (cache_map_.find(block.hash()) != cache_map_.end()) {
        record_access(block);
        return;
    }

    lookahead_.pop_front();
    while(block.size_ > (max_size_ - curr_size_))
        evict_block();
    
    block.access_count = 1;
    auto it = block_list_.insert(block_list_.begin(), block);
    cache_map_[block.hash()] = it;
    curr_size_ += block.size_;
    block_count_++;
}

void OptimalCache::remove_block(CacheBlock block) {
    if (cache_map_.find(block.hash()) == cache_map_.end()) return;
    auto it = cache_map_[block.hash()];
    curr_size_ -= it->size_;
    block_count_--;
    block_list_.erase(it);
    cache_map_.erase(block.hash());
}

void OptimalCache::evict_block() {
    std::map<uint64_t, bool> distance_map;
    
    size_t distance = 0;
    auto it = lookahead_.begin();
    uint64_t max_hash = 0;
    bool max_hash_found = false;

    while (it != lookahead_.end() && distance < max_lookahead_) {
        uint64_t curr_hash = it->hash();
        ++distance; ++it;
        if (cache_map_.find(curr_hash) == cache_map_.end()) continue;
        if (distance_map.find(curr_hash) != distance_map.end()) continue;
        distance_map[curr_hash] = true;
        max_hash = curr_hash;
        max_hash_found = true;
    }

    for (auto cache_entry : cache_map_) {
        uint64_t curr_hash = cache_entry.first;
        if (distance_map.find(curr_hash) != distance_map.end()) continue;
        // this element never recurs, so its distance is infinity
        max_hash = curr_hash;
        max_hash_found = true;
        break;
    }

    if (!max_hash_found) {
        std::cerr << "Failed to evict\n";
        exit(1);
    }

    auto it2 = cache_map_[max_hash];
    curr_size_ -= it2->size_;
    block_count_--;
    block_list_.erase(it2);
    cache_map_.erase(max_hash);
}

void OptimalCache::record_access(CacheBlock block) {
    CacheBlock next_block = lookahead_.front();
    if (block.hash() != next_block.hash()) {
        std::cerr << "Error: insertion order doesn't match lookahead\n";
        exit(1);
    }

    lookahead_.pop_front();
    if (cache_map_.find(block.hash()) == cache_map_.end()) return;

    auto it = cache_map_[block.hash()];
    it->access_count++;
}