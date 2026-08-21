#include <cstdint>
#include <cstdlib>
#include "lfu_cache.h"

// Percolates a CacheBlock object down in the min heap. Returns the new index.
size_t percolate_down(std::unordered_map<uint64_t, size_t>& cache_map,
                      std::vector<CacheBlock>& heap, size_t index) {
    size_t heap_size = heap.size();
    while (index < heap_size) {
        size_t left_child = index << 1;
        size_t right_child = left_child | 1;

        uint64_t index_access = heap[index].access_count;
        uint64_t left_access = UINT64_MAX, right_access = UINT64_MAX;
        if (left_child < heap_size) left_access = heap[left_child].access_count;
        if (right_child < heap_size) right_access = heap[right_child].access_count;

        if (index_access <= left_access && index_access <= right_access) break;

        size_t swap_pos = (left_access < right_access)? left_child : right_child;
        uint64_t index_hash = heap[index].hash(), swap_hash = heap[swap_pos].hash();
        std::swap(heap[index], heap[swap_pos]);
        std::swap(cache_map[index_hash], cache_map[swap_hash]);
        index = swap_pos;
    }
    return index;
}

size_t percolate_down_absolute(std::unordered_map<uint64_t, size_t>& cache_map,
                               std::vector<CacheBlock>& heap, size_t index,
                               std::unordered_map<uint64_t, uint64_t>& access_map) {
    size_t heap_size = heap.size();
    while (index < heap_size) {
        size_t left_child = index << 1;
        size_t right_child = left_child | 1;

        uint64_t index_access = access_map[heap[index].hash()];
        uint64_t left_access = UINT64_MAX, right_access = UINT64_MAX;
        if (left_child < heap_size) left_access = access_map[heap[left_child].hash()];
        if (right_child < heap_size) right_access = access_map[heap[right_child].hash()];

        if (index_access <= left_access && index_access <= right_access) break;

        size_t swap_pos = (left_access < right_access)? left_child : right_child;
        uint64_t index_hash = heap[index].hash(), swap_hash = heap[swap_pos].hash();
        std::swap(heap[index], heap[swap_pos]);
        std::swap(cache_map[index_hash], cache_map[swap_hash]);
        index = swap_pos;
    }
    return index;
}

// Percolates a CacheBlock object up in the min heap. Returns the new index.
size_t percolate_up(std::unordered_map<uint64_t, size_t>& cache_map,
                    std::vector<CacheBlock>& heap, size_t index) {
    if (index >= heap.size()) return index;
    while (index > 1) {
        size_t parent = index >> 1;
        uint64_t index_access = heap[index].access_count;
        uint64_t parent_access = heap[parent].access_count;

        if (index_access < parent_access) {
            uint64_t parent_hash = heap[parent].hash(), index_hash = heap[index].hash();
            std::swap(heap[parent], heap[index]);
            std::swap(cache_map[parent_hash], cache_map[index_hash]);
            index = parent;
        }
        else break;
    }
    return index;
}

size_t percolate_up_absolute(std::unordered_map<uint64_t, size_t>& cache_map,
                             std::vector<CacheBlock>& heap, size_t index,
                             std::unordered_map<uint64_t, uint64_t>& access_map) {
    if (index >= heap.size()) return index;
    while (index > 1) {
        size_t parent = index >> 1;
        uint64_t index_access = access_map[heap[index].hash()];
        uint64_t parent_access = access_map[heap[parent].hash()];

        if (index_access < parent_access) {
            uint64_t parent_hash = heap[parent].hash(), index_hash = heap[index].hash();
            std::swap(heap[parent], heap[index]);
            std::swap(cache_map[parent_hash], cache_map[index_hash]);
            index = parent;
        }
        else break;
    }
    return index;
}

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

    block.access_count = 1;
    access_map_[block_hash]++;
    while(block.size_ > (max_size_ - curr_size_))
        evict_block();
    cache_heap_.push_back(block);
    cache_map_[block_hash] = cache_heap_.size()-1;
    if (is_absolute_)
        cache_map_[block_hash] = percolate_up_absolute(cache_map_, cache_heap_, cache_heap_.size()-1, access_map_);
    else
        cache_map_[block_hash] = percolate_up(cache_map_, cache_heap_, cache_heap_.size()-1);
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
        cache_map_[block_hash] = percolate_down_absolute(cache_map_, cache_heap_, pos, access_map_);
    else
        cache_map_[block_hash] = percolate_down(cache_map_, cache_heap_, pos);

    if (cache_map_[block_hash] == pos) {
        if (is_absolute_)
            cache_map_[block_hash] = percolate_up_absolute(cache_map_, cache_heap_, pos, access_map_);
        else
            cache_map_[block_hash] = percolate_up(cache_map_, cache_heap_, pos);
    }
}

void HeapLFUCache::evict_block() {
    if (cache_heap_.size() <= 1) return;
    remove_block(cache_heap_[1]);
}

void HeapLFUCache::record_access(CacheBlock block) {
    uint64_t block_hash = block.hash();
    if (cache_map_.find(block_hash) == cache_map_.end()) return;
 
    size_t pos = cache_map_[block_hash];
    cache_heap_[pos].access_count++;
    access_map_[block_hash]++;
    if (is_absolute_)
        cache_map_[block_hash] = percolate_down_absolute(cache_map_, cache_heap_, pos, access_map_);
    else
        cache_map_[block_hash] = percolate_down(cache_map_, cache_heap_, pos);
}