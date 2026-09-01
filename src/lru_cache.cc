#include <cstdlib>
#include <iostream>
#include "lru_cache.h"
#include "cache.h"
#include "record_parser.h"

void LRUCache::insert_block(CacheBlock block) {
    if (block.size_ > max_size_) {
        std::cerr << "Error: block size is bigger than cache size";
        exit(1);
    }

    if (cache_map_.find(block.hash()) != cache_map_.end()) {
        record_access(block);
        return;
    }

    block.access_count_ = 1;
    while(block.size_ > (max_size_ - curr_size_))
        evict_block();
    auto it = lru_block_list_.insert(lru_block_list_.begin(), block);
    cache_map_[block.hash()] = it;
    curr_size_ += block.size_;
    block_count_++;
}

void LRUCache::remove_block(CacheBlock block) {
    if (cache_map_.find(block.hash()) == cache_map_.end()) return;
    auto it = cache_map_[block.hash()];
    curr_size_ -= it->size_;
    block_count_--;
    lru_block_list_.erase(it);
    cache_map_.erase(block.hash());
}

void LRUCache::remove_file_blocks(const FileMetadata& file) {
    for (auto it = lru_block_list_.begin(); it != lru_block_list_.end();) {
        if (it->file_id_ != file.file_id) {
            ++it;
            continue;
        }
        curr_size_ -= it->size_;
        block_count_--;
        cache_map_.erase(it->hash());
        it = lru_block_list_.erase(it);
    }
}

void LRUCache::evict_block() {
    if (!lru_block_list_.size()) return;
    remove_block(lru_block_list_.back());
}

void LRUCache::record_access(CacheBlock block) {
    if (cache_map_.find(block.hash()) == cache_map_.end()) return;

    auto it = cache_map_[block.hash()];
    it->access_count_++;
    lru_block_list_.splice(lru_block_list_.begin(), lru_block_list_, it);
}