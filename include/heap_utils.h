#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>
#include "cache.h"
// The percolate helpers only ever swap the mapped values of cache_map, so the
// mapped type is a template parameter: any swappable value works.

// Percolates a CacheBlock object down in the min heap. Returns the new index.
template <typename ValueType, typename FieldType>
size_t min_percolate_down(std::unordered_map<uint64_t, ValueType>& cache_map,
                      std::vector<CacheBlock>& heap, size_t index,
                      FieldType CacheBlock::* key_field) {
    size_t heap_size = heap.size();
    while (index < heap_size) {
        size_t left_child = index << 1;
        size_t right_child = left_child | 1;
        if (left_child >= heap_size) break;

        // Smaller child wins; ties go to the right child, as before.
        size_t swap_pos = left_child;
        if (right_child < heap_size && !(heap[left_child].*key_field < heap[right_child].*key_field))
            swap_pos = right_child;
        if (!(heap[swap_pos].*key_field < heap[index].*key_field)) break;

        uint64_t index_hash = heap[index].hash(), swap_hash = heap[swap_pos].hash();
        std::swap(heap[index], heap[swap_pos]);
        std::swap(cache_map[index_hash], cache_map[swap_hash]);
        index = swap_pos;
    }
    return index;
}

template <typename ValueType>
size_t min_percolate_down_absolute(std::unordered_map<uint64_t, ValueType>& cache_map,
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
template <typename ValueType, typename FieldType>
size_t min_percolate_up(std::unordered_map<uint64_t, ValueType>& cache_map,
                    std::vector<CacheBlock>& heap, size_t index,
                    FieldType CacheBlock::* key_field) {
    if (index >= heap.size()) return index;
    while (index > 1) {
        size_t parent = index >> 1;
        if (!(heap[index].*key_field < heap[parent].*key_field)) break;

        uint64_t parent_hash = heap[parent].hash(), index_hash = heap[index].hash();
        std::swap(heap[parent], heap[index]);
        std::swap(cache_map[parent_hash], cache_map[index_hash]);
        index = parent;
    }
    return index;
}

template <typename ValueType>
size_t min_percolate_up_absolute(std::unordered_map<uint64_t, ValueType>& cache_map,
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


// Percolates a CacheBlock object down in the max heap. Returns the new index.
template <typename ValueType, typename FieldType>
size_t max_percolate_down(std::unordered_map<uint64_t, ValueType>& cache_map,
                      std::vector<CacheBlock>& heap, size_t index,
                      FieldType CacheBlock::* key_field) {
    size_t heap_size = heap.size();
    while (index < heap_size) {
        size_t left_child = index << 1;
        size_t right_child = left_child | 1;
        if (left_child >= heap_size) break;

        // Larger child wins; ties go to the right child, as before.
        size_t swap_pos = left_child;
        if (right_child < heap_size && !(heap[right_child].*key_field < heap[left_child].*key_field))
            swap_pos = right_child;
        if (!(heap[index].*key_field < heap[swap_pos].*key_field)) break;

        uint64_t index_hash = heap[index].hash(), swap_hash = heap[swap_pos].hash();
        std::swap(heap[index], heap[swap_pos]);
        std::swap(cache_map[index_hash], cache_map[swap_hash]);
        index = swap_pos;
    }
    return index;
}

template <typename ValueType>
size_t max_percolate_down_absolute(std::unordered_map<uint64_t, ValueType>& cache_map,
                               std::vector<CacheBlock>& heap, size_t index,
                               std::unordered_map<uint64_t, uint64_t>& access_map) {
    size_t heap_size = heap.size();
    while (index < heap_size) {
        size_t left_child = index << 1;
        size_t right_child = left_child | 1;

        uint64_t index_access = access_map[heap[index].hash()];
        uint64_t left_access = 0, right_access = 0;
        if (left_child < heap_size) left_access = access_map[heap[left_child].hash()];
        if (right_child < heap_size) right_access = access_map[heap[right_child].hash()];

        if (index_access >= left_access && index_access >= right_access) break;

        size_t swap_pos = (left_access > right_access)? left_child : right_child;
        uint64_t index_hash = heap[index].hash(), swap_hash = heap[swap_pos].hash();
        std::swap(heap[index], heap[swap_pos]);
        std::swap(cache_map[index_hash], cache_map[swap_hash]);
        index = swap_pos;
    }
    return index;
}

// Percolates a CacheBlock object up in the max heap. Returns the new index.
template <typename ValueType, typename FieldType>
size_t max_percolate_up(std::unordered_map<uint64_t, ValueType>& cache_map,
                    std::vector<CacheBlock>& heap, size_t index,
                    FieldType CacheBlock::* key_field) {
    if (index >= heap.size()) return index;
    while (index > 1) {
        size_t parent = index >> 1;
        if (!(heap[parent].*key_field < heap[index].*key_field)) break;

        uint64_t parent_hash = heap[parent].hash(), index_hash = heap[index].hash();
        std::swap(heap[parent], heap[index]);
        std::swap(cache_map[parent_hash], cache_map[index_hash]);
        index = parent;
    }
    return index;
}

template <typename ValueType>
size_t max_percolate_up_absolute(std::unordered_map<uint64_t, ValueType>& cache_map,
                             std::vector<CacheBlock>& heap, size_t index,
                             std::unordered_map<uint64_t, uint64_t>& access_map) {
    if (index >= heap.size()) return index;
    while (index > 1) {
        size_t parent = index >> 1;
        uint64_t index_access = access_map[heap[index].hash()];
        uint64_t parent_access = access_map[heap[parent].hash()];

        if (index_access > parent_access) {
            uint64_t parent_hash = heap[parent].hash(), index_hash = heap[index].hash();
            std::swap(heap[parent], heap[index]);
            std::swap(cache_map[parent_hash], cache_map[index_hash]);
            index = parent;
        }
        else break;
    }
    return index;
}
