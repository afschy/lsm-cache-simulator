#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include "heap_utils.h"
#include "lfu_cache.h"

HeapLFUCache::HeapLFUCache(uint64_t max_size, bool is_absolute): Cache(max_size) {
    cache_heap_.resize(1);
    is_absolute_ = is_absolute;
}

void HeapLFUCache::insert_block(CacheBlock block) {
    if (block.size_ > max_size_) {
        std::cerr << "Error: block size is bigger than cache size";
        exit(1);
    }

    uint64_t block_hash = block.hash();
    if (cache_map_.find(block_hash) != cache_map_.end())
        return record_access(block);

    block.access_count_ = 1;
    access_map_[block_hash]++;
    while(block.size_ > (max_size_ - curr_size_))
        evict_block();
    cache_heap_.push_back(block);
    cache_map_[block_hash] = cache_heap_.size()-1;
    if (is_absolute_)
        cache_map_[block_hash] = min_percolate_up_absolute(cache_map_, cache_heap_, cache_heap_.size()-1, access_map_);
    else
        cache_map_[block_hash] = min_percolate_up(cache_map_, cache_heap_, cache_heap_.size()-1, &CacheBlock::access_count_);
    curr_size_ += block.size_;
    block_count_++;
}

void HeapLFUCache::remove_block(CacheBlock block) {
    uint64_t block_hash = block.hash();
    if (cache_map_.find(block_hash) == cache_map_.end()) return;

    size_t pos = cache_map_[block_hash];
    uint64_t block_size = cache_heap_[pos].size_;

    CacheBlock swap_block = cache_heap_[cache_heap_.size()-1];
    std::swap(cache_map_[block_hash], cache_map_[swap_block.hash()]);
    std::swap(cache_heap_[pos], cache_heap_[cache_heap_.size()-1]);
    
    cache_heap_.pop_back();
    cache_map_.erase(block_hash);

    curr_size_ -= block_size;
    block_count_--;
    if (swap_block == block || pos == cache_heap_.size()) return;
    
    block_hash = cache_heap_[pos].hash();
    if (is_absolute_)
        cache_map_[block_hash] = min_percolate_down_absolute(cache_map_, cache_heap_, pos, access_map_);
    else
        cache_map_[block_hash] = min_percolate_down(cache_map_, cache_heap_, pos, &CacheBlock::access_count_);

    if (cache_map_[block_hash] == pos) {
        if (is_absolute_)
            cache_map_[block_hash] = min_percolate_up_absolute(cache_map_, cache_heap_, pos, access_map_);
        else
            cache_map_[block_hash] = min_percolate_up(cache_map_, cache_heap_, pos, &CacheBlock::access_count_);
    }
}

void HeapLFUCache::remove_file_blocks(const FileMetadata& file) {
    std::vector<CacheBlock> matches;
    for (size_t i = 1; i < cache_heap_.size(); i++)
        if (cache_heap_[i].file_id_ == file.file_id)
            matches.push_back(cache_heap_[i]);
    for (const CacheBlock& block : matches)
        remove_block(block);
}

void HeapLFUCache::evict_block() {
    if (cache_heap_.size() <= 1) return;
    remove_block(cache_heap_[1]);
}

void HeapLFUCache::record_access(CacheBlock block) {
    uint64_t block_hash = block.hash();
    if (cache_map_.find(block_hash) == cache_map_.end()) return;
 
    size_t pos = cache_map_[block_hash];
    cache_heap_[pos].access_count_++;
    access_map_[block_hash]++;
    if (is_absolute_)
        cache_map_[block_hash] = min_percolate_down_absolute(cache_map_, cache_heap_, pos, access_map_);
    else
        cache_map_[block_hash] = min_percolate_down(cache_map_, cache_heap_, pos, &CacheBlock::access_count_);
}