#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "cache.h"

class HeapLFUCache: public Cache {
    std::vector<CacheBlock> cache_heap_;
    std::unordered_map<uint64_t, size_t> cache_map_;
    std::unordered_map<uint64_t, uint64_t> access_map_;
    bool is_absolute_;
public:
    HeapLFUCache(uint64_t max_size, bool is_absolute);
    using Cache::insert_block;
    using Cache::remove_block;
    using Cache::record_access;
    bool block_exists(CacheBlock block) {return cache_map_.find(block.hash()) != cache_map_.end();}
    void insert_block(CacheBlock block);    // might evict blocks to make space
    void remove_block(CacheBlock block);
    void remove_file_blocks(const FileMetadata& file);
    void evict_block();                     // removes tail
    void record_access(CacheBlock block);
    std::string get_name() {
        if (is_absolute_) return "LFUABS";
        else return "LFU";
    }
};