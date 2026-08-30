#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "zstd_reader.h"

enum RecordType: uint8_t {
    kGet,
    kIterator,
    kFileCreate,
    kFileDelete,
    kFileMove,
    kUnDefined
};

enum class FileOutcome: uint8_t {
    kNotFound,
    kFoundValue,
    kFoundTombstone,
    kFoundMergeOperand,
    kError,
    kNotApplicable
};

enum class OverallOutcome: uint8_t {
    kNotFound,
    kFound,
    kDeleted,
    kError,
    kRangeDeleted,
    kNotApplicable
};

struct Block {
    uint64_t block_id;
    uint64_t seq;
    uint64_t read_bytes;
    uint64_t uncomp_bytes;
};

struct Probe {
    FileOutcome file_outcome = FileOutcome::kNotApplicable;
    uint8_t level;
    uint64_t file_id;
    std::vector<Block> blocks;
};

struct Record {
    RecordType record_type = kUnDefined;
    
    uint64_t seq = 0;
    uint64_t timestamp = 0;
    
    uint64_t cf_id = 0;
    uint64_t lookup_id = 0;
    OverallOutcome overall_outcome = OverallOutcome::kNotApplicable;

    uint64_t touched_files = 0;
    std::vector<Probe> probes;
    uint64_t touched_blocks = 0;
    std::vector<Block> blocks;

    uint64_t file_id = 0;
    uint8_t level = 0;
    uint8_t new_level = 0;
    uint32_t entry_count = 0;
    uint32_t file_size = 0;

    uint64_t iter_id = 0;
    uint64_t caller = 0;

    bool no_insert = false;

    void reset();
};

struct FileMetadata {
    uint64_t file_id;
    uint8_t level;
    uint32_t entry_count;
    uint32_t file_size;
    bool deleted = false;

    FileMetadata(){};
    FileMetadata(const Record& record) {parse_from_record(record);}
    void parse_from_record(const Record& record);
};

class RecordParser {
    static constexpr uint8_t get_field_count = 8;
    static constexpr uint8_t iterator_field_count = 11;
    static constexpr uint8_t file_event_field_count = 9;

    bool parse_get(Record* out_record, const std::string& record_string);
    bool parse_iterator(Record* out_record, const std::string& record_string);
    bool parse_file_event(Record* out_record, const std::string& record_string);
public:
    ZSTDReader zstd_reader_;
    RecordParser(const char* filepath): zstd_reader_(filepath) {}
    bool parse_next_record(Record* out_record);     // Returns false if no more records exist
    bool parse_record(Record* out_record, const std::string& record_string);    // Returns false for malformed records
};