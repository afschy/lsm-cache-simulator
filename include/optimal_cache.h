#pragma once
#include <list>
#include <queue>
#include <deque>
#include <unordered_map>
#include "cache.h"

class OptimalCache: public Cache {
    std::deque<CacheBlock> lookahead_;
    uint32_t max_lookahead_;

    std::list<CacheBlock> block_list_;
    std::unordered_map<uint64_t, std::list<CacheBlock>::iterator> cache_map_;
public:
    OptimalCache(uint64_t max_size, uint32_t max_lookahead)
        :Cache(max_size), max_lookahead_(max_lookahead) {}

    using Cache::insert_block;
    using Cache::remove_block;
    using Cache::record_access;
    
    void advance_lookahead();
    void insert_block(CacheBlock block) override;   // might evict blocks to make space
    void remove_block(CacheBlock block) override;
    void evict_block() override;
    void record_access(CacheBlock block) override;  // has to be the first element of the lookahead, otherwise will crash

    void set_max_lookahead(uint32_t max_lookahead) {max_lookahead_ = max_lookahead;}
    uint32_t get_max_lookahead() {return max_lookahead_;}
    
    void add_lookahead(CacheBlock block) {lookahead_.push_back(block);}
    void clear_lookahead() {lookahead_.clear();}
    size_t get_lookahead_size() {return lookahead_.size();}
};