#pragma once
#include "record_parser.h"
#include <cstdint>
#include <string>

enum class BlockType: uint8_t {
    kFilter,
    kIndex,
    kData,
    kUndefined
};

struct CacheBlock {
    BlockType block_type_ = BlockType::kUndefined;
    uint64_t file_id_ = UINT64_MAX;
    uint64_t block_id_ = UINT64_MAX;
    uint8_t level_ = UINT8_MAX;
    uint64_t size_ = 0;
    uint64_t lookup_id_ = UINT64_MAX;
    uint32_t access_count_ = 0;
    uint64_t nearest_access_ = UINT64_MAX;
    uint64_t insert_time_ = 0;

    CacheBlock(BlockType block_type, uint64_t file_id, uint64_t block_id, uint8_t level, uint64_t size, uint64_t lookup_id=UINT64_MAX) {
        block_type_ = block_type;
        file_id_ = file_id;
        block_id_ = block_id;
        level_ = level;
        size_ = size;
        lookup_id_ = lookup_id;
    }
    CacheBlock(){}

    bool operator==(const CacheBlock& other) const {
        return block_type_ == other.block_type_ && file_id_ == other.file_id_ && block_id_ == other.block_id_;
    }
    static uint64_t hash(BlockType block_type, uint64_t file_id, uint64_t block_id) {
        return (uint64_t(block_type)<<61) | (file_id<<32) | block_id;
    }
    uint64_t hash() const {
        return hash(block_type_, file_id_, block_id_);
    }
};

class Cache {
public:
    uint64_t max_size_;
    uint64_t curr_size_;
    uint64_t block_count_;
    Cache(uint64_t max_size) {
        max_size_ = max_size;
        curr_size_ = 0;
        block_count_ = 0;
    }
    virtual ~Cache() = default;
    virtual bool block_exists(CacheBlock block)=0;
    virtual bool block_exists(BlockType block_type, uint64_t file_id, uint64_t block_id, uint8_t level, uint64_t size) {
        CacheBlock block(block_type, file_id, block_id, level, size);
        return block_exists(block);
    }
    virtual void insert_block(CacheBlock block)=0;    // might evict blocks to make space
    virtual void insert_block(BlockType block_type, uint64_t file_id, uint64_t block_id, uint8_t level, uint64_t size) {
        CacheBlock block(block_type, file_id, block_id, level, size);
        insert_block(block);
    }
    virtual void remove_block(CacheBlock block)=0;
    virtual void remove_block(BlockType block_type, uint64_t file_id, uint64_t block_id) {
        CacheBlock block(block_type, file_id, block_id, 0, 0);
        remove_block(block);
    }
    virtual void evict_block()=0;                     // removes tail
    virtual void remove_file_blocks(const FileMetadata& file)=0;
    virtual void record_access(CacheBlock block)=0;
    virtual void record_access(BlockType block_type, uint64_t file_id, uint64_t block_id) {
        CacheBlock block(block_type, file_id, block_id, 0, 0);
        record_access(block);
    }
    virtual std::string get_name()=0;
};
