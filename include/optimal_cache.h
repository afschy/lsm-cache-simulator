#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "cache.h"

class OptimalCache: public Cache {    
    std::vector<CacheBlock> cache_heap_;                // max heap on the distance of first access
    std::unordered_map<uint64_t, size_t> cache_map_;    // block hash to heap index
    // block hash to sorted list of all access distances in lookahead.
    // an entry is erased once its list empties, so a present entry is always non-empty
    std::unordered_map<uint64_t, std::deque<uint64_t>> access_distance_map_;
public:
    uint32_t max_lookahead_;
    std::deque<CacheBlock> lookahead_;
    uint64_t lookahead_counter_ = 0;

    OptimalCache(uint64_t max_size, uint32_t max_lookahead)
        :Cache(max_size), max_lookahead_(max_lookahead) {cache_heap_.resize(1);}

    using Cache::insert_block;
    using Cache::remove_block;
    using Cache::record_access;
    
    bool advance_lookahead();   // Returns true if hit, false if miss
    bool block_exists(CacheBlock block) {return cache_map_.find(block.hash()) != cache_map_.end();}
    void insert_block(CacheBlock block) override;   // might evict blocks to make space
    void remove_block(CacheBlock block) override;
    void evict_block() override;
    void record_access(CacheBlock block) override;  // has to be the first element of the lookahead, otherwise will crash

    void set_max_lookahead(uint32_t max_lookahead) {max_lookahead_ = max_lookahead;}
    uint32_t get_max_lookahead() {return max_lookahead_;}
    
    void add_lookahead(CacheBlock block);
    void pop_lookahead_front();
    void clear_lookahead() {lookahead_.clear();}
    size_t get_lookahead_size() {return lookahead_.size();}
    std::string get_name(){return "OPTIMAL";}
};