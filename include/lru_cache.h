#pragma once
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include "cache.h"

class LRUCache: public Cache {
    std::list<CacheBlock> lru_block_list_;
    std::unordered_map<uint64_t, std::list<CacheBlock>::iterator> cache_map_;
public:
    LRUCache(uint64_t max_size) : Cache(max_size){}
    using Cache::insert_block;
    using Cache::remove_block;
    using Cache::record_access;
    bool block_exists(CacheBlock block) {return cache_map_.find(block.hash()) != cache_map_.end();}
    void insert_block(CacheBlock block);    // might evict blocks to make space
    void remove_block(CacheBlock block);
    void evict_block();                     // removes tail
    void record_access(CacheBlock block);
    std::string get_name(){return "LRU";}
};