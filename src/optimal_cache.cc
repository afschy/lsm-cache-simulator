#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <utility>
#include "heap_utils.h"
#include "optimal_cache.h"

void OptimalCache::add_lookahead(CacheBlock block) {
    lookahead_counter_++;
    block.insert_time_ = lookahead_counter_;
    
    uint64_t hash = block.hash();
    std::deque<uint64_t>& access_list = access_distance_map_[hash];
    access_list.push_back(lookahead_counter_);
    lookahead_.push_back(block);

    if (cache_map_.find(hash) == cache_map_.end()) return;
    if (access_list.size() > 1) return;

    size_t found_block_index = cache_map_[hash];
    cache_heap_[found_block_index].nearest_access_ = lookahead_counter_;

    found_block_index = max_percolate_down(cache_map_, cache_heap_, found_block_index, &CacheBlock::nearest_access_);
    found_block_index = max_percolate_up(cache_map_, cache_heap_, found_block_index, &CacheBlock::nearest_access_);
    cache_map_[hash] = found_block_index;
}

void OptimalCache::pop_lookahead_front() {
    CacheBlock block = lookahead_.front();
    lookahead_.pop_front();

    uint64_t hash = block.hash();
    auto access_it = access_distance_map_.find(hash);
    if (access_it == access_distance_map_.end()) return;

    std::deque<uint64_t>& access_list = access_it->second;
    if (access_list.front() != block.insert_time_) return;
    access_list.pop_front();

    // erasing invalidates access_list, so the new nearest access has to be read out first
    uint64_t next_access = access_list.empty()? UINT64_MAX : access_list.front();
    if (access_list.empty()) access_distance_map_.erase(access_it);

    if (cache_map_.find(hash) == cache_map_.end()) return;
    size_t index = cache_map_[hash];
    cache_heap_[index].nearest_access_ = next_access;

    index = max_percolate_down(cache_map_, cache_heap_, index, &CacheBlock::nearest_access_);
    index = max_percolate_up(cache_map_, cache_heap_, index, &CacheBlock::nearest_access_);
    cache_map_[hash] = index;
}

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

    pop_lookahead_front();
    while(block.size_ > (max_size_ - curr_size_))
        evict_block();

    uint64_t block_hash = block.hash();
    // a present entry is never empty, so its front is the next access
    auto access_it = access_distance_map_.find(block_hash);
    if (access_it == access_distance_map_.end()) block.nearest_access_ = UINT64_MAX;
    else block.nearest_access_ = access_it->second.front();

    block.access_count_ = 1;
    cache_heap_.push_back(block);
    // the percolate helpers swap through cache_map_, so the new block needs an
    // entry before it moves, otherwise the block it displaces is mapped to 0
    cache_map_[block_hash] = cache_heap_.size()-1;
    cache_map_[block_hash] = max_percolate_up(cache_map_, cache_heap_, cache_heap_.size()-1, &CacheBlock::nearest_access_);
    curr_size_ += block.size_;
    block_count_++;
}

void OptimalCache::remove_block(CacheBlock block) {
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
    cache_map_[block_hash] = max_percolate_down(cache_map_, cache_heap_, pos, &CacheBlock::nearest_access_);

    if (cache_map_[block_hash] == pos)
        cache_map_[block_hash] = max_percolate_up(cache_map_, cache_heap_, pos, &CacheBlock::nearest_access_);
}

void OptimalCache::remove_file_blocks(const FileMetadata& file) {
    std::vector<CacheBlock> matches;
    for (size_t i = 1; i < cache_heap_.size(); i++)
        if (cache_heap_[i].file_id_ == file.file_id)
            matches.push_back(cache_heap_[i]);
    for (const CacheBlock& block : matches)
        remove_block(block);
}

void OptimalCache::evict_block() {
    if (cache_heap_.size() > 1)
        remove_block(cache_heap_[1]);
}

void OptimalCache::record_access(CacheBlock block) {
    CacheBlock next_block = lookahead_.front();
    if (block.hash() != next_block.hash()) {
        std::cerr << "Error: insertion order doesn't match lookahead\n";
        exit(1);
    }

    pop_lookahead_front();
    if (cache_map_.find(block.hash()) == cache_map_.end()) return;

    size_t index = cache_map_[block.hash()];
    cache_heap_[index].access_count_++;
}
