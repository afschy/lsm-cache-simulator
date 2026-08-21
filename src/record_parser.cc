#include "record_parser.h"
#include <charconv>
#include <string>
#include <string_view>

void split_string(std::vector<std::string_view>& output, std::string_view input, char delimiter) {
    output.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t delim_pos = input.find(delimiter, start);
        if (delim_pos == std::string_view::npos) {
            output.push_back(input.substr(start));
            return;
        }
        output.push_back(input.substr(start, delim_pos - start));
        start = delim_pos + 1;
    }
}

uint64_t string_view_to_number(std::string_view str, bool& success) {
    uint64_t number = 0;
    auto result = std::from_chars(str.data(), str.data() + str.size(), number);
    if (result.ec != std::errc{} || result.ptr != str.data() + str.size()) success = false;
    return number;
}

void Record::reset() {
    record_type = kUnDefined;
    seq = 0;
    timestamp = 0;
    cf_id = 0;
    lookup_id = 0;
    overall_outcome = OverallOutcome::kNotApplicable;

    touched_files = 0;
    probes.clear();
    touched_blocks = 0;
    blocks.clear();

    file_id = 0;
    level = 0;
    iter_id = 0;
    caller = 0;

    no_insert = false;
    new_level = 0;
}

bool RecordParser::parse_next_record(Record* out_record) {
    std::string read_line;
    while (!zstd_reader_.reached_file_end()) {
        zstd_reader_.get_next_line(&read_line);
        if (parse_record(out_record, read_line))
            return true;
    }
    return false;
}

bool RecordParser::parse_record(Record* out_record, const std::string& read_line) {
    out_record->reset();
    char first_char = read_line[0];
    switch (first_char)
    {
    case 'G':
        return parse_get(out_record, read_line);
    case 'A':
        return parse_iterator(out_record, read_line);
    case 'F':
        return parse_file_event(out_record, read_line);    
    default:
        return false;
    }
}

bool RecordParser::parse_get(Record* out_record, const std::string& read_line) {
    static std::vector<std::string_view> fields;
    split_string(fields, read_line, ',');
    if (fields.size() < get_field_count) return false;
    
    bool success = true;
    out_record->record_type = RecordType::kGet;
    out_record->seq = string_view_to_number(fields[1], success);
    out_record->timestamp = string_view_to_number(fields[2], success);
    out_record->lookup_id = string_view_to_number(fields[3], success);
    out_record->cf_id = string_view_to_number(fields[4], success);
    out_record->overall_outcome = static_cast<OverallOutcome>(string_view_to_number(fields[5], success));
    out_record->touched_files = string_view_to_number(fields[6], success);
    out_record->touched_blocks = 0;
    if (!success) return false;

    if (out_record->touched_files == 0) return fields[7].empty();

    static std::vector<std::string_view> probes;
    split_string(probes, fields[7], '|');
    if (probes.size() != out_record->touched_files) return false;

    for (std::string_view curr_probe : probes) {
        static std::vector<std::string_view> probe_fields;
        split_string(probe_fields, curr_probe, ':');
        if (probe_fields.size() < 3) return false;

        uint64_t level = string_view_to_number(probe_fields[0], success);
        uint64_t file_number = string_view_to_number(probe_fields[1], success);
        FileOutcome outcome = static_cast<FileOutcome>(string_view_to_number(probe_fields[2], success));

        Probe probe;
        probe.level = level;
        probe.file_id = file_number;
        probe.file_outcome = outcome;
        
        for (size_t i=3; i<probe_fields.size(); i++) {
            Block block;

            static std::vector<std::string_view> block_fields;
            split_string(block_fields, probe_fields[i], '/');
            if (block_fields.size() < 4) return false;

            block.block_id = string_view_to_number(block_fields[0], success);
            block.seq = string_view_to_number(block_fields[1], success);
            block.read_bytes = string_view_to_number(block_fields[2], success);
            block.uncomp_bytes = string_view_to_number(block_fields[3], success);

            probe.blocks.push_back(block);
            out_record->blocks.push_back(block);
            out_record->touched_blocks++;
        }
        out_record->probes.push_back(probe);
    }
    
    if (!success) return false;
    return true;
}

bool RecordParser::parse_iterator(Record* out_record, const std::string& read_line) {
    static std::vector<std::string_view> fields;
    split_string(fields, read_line, ',');
    if (fields.size() < iterator_field_count) return false;

    bool success = true;
    out_record->record_type = RecordType::kIterator;
    out_record->seq = string_view_to_number(fields[1], success);
    out_record->timestamp = string_view_to_number(fields[2], success);
    out_record->cf_id = string_view_to_number(fields[3], success);
    out_record->caller = string_view_to_number(fields[4], success);
    out_record->iter_id = string_view_to_number(fields[5], success);
    out_record->level = string_view_to_number(fields[6], success);
    out_record->file_id = string_view_to_number(fields[7], success);
    out_record->no_insert = string_view_to_number(fields[8], success);
    out_record->touched_blocks = string_view_to_number(fields[9], success);
    if (!success) return false;

    if (out_record->touched_blocks == 0) return fields[10].empty();

    static std::vector<std::string_view> block_list;
    split_string(block_list, fields[10], ':');
    if (block_list.size() != out_record->touched_blocks) return false;

    for (auto block_str : block_list) {
        static std::vector<std::string_view> block_fields;
        split_string(block_fields, block_str, '/');
        if (block_fields.size() < 4) return false;

        Block block;
        block.block_id = string_view_to_number(block_fields[0], success);
        block.seq = string_view_to_number(block_fields[1], success);
        block.read_bytes = string_view_to_number(block_fields[2], success);
        block.uncomp_bytes = string_view_to_number(block_fields[3], success);

        out_record->blocks.push_back(block);
    }

    if (!success) return false;
    return true;
}

bool RecordParser::parse_file_event(Record* out_record, const std::string& read_line) {
    static std::vector<std::string_view> fields;
    split_string(fields, read_line, ',');
    if (fields.size() < file_event_field_count) return false;

    bool success = true;
    out_record->seq = string_view_to_number(fields[1], success);
    out_record->timestamp = string_view_to_number(fields[2], success);
    out_record->cf_id = string_view_to_number(fields[4], success);
    out_record->file_id = string_view_to_number(fields[5], success);
    out_record->entry_count = string_view_to_number(fields[6], success);
    out_record->file_size = string_view_to_number(fields[7], success);
    out_record->level = string_view_to_number(fields[8], success);
    if (fields.size() > 9)
        out_record->new_level = string_view_to_number(fields[9], success);
    if (!success) return false;

    if (fields[3] == "create") out_record->record_type = kFileCreate;
    else if (fields[3] == "delete") out_record->record_type = kFileDelete;
    else if (fields[3] == "move") out_record->record_type = kFileMove;
    else return false;
    return true;
}

void FileMetadata::parse_from_record(const Record& record) {
    file_id = record.file_id;
    level = record.level;
    entry_count = record.entry_count;
    file_size = record.file_size;
}