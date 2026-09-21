#pragma once
#include <string>
#include <cstdint>

struct Config {
    std::string bind_address;
    uint16_t port{};
    unsigned threads{};

    std::string storage_root;
    std::string files_dir;
    std::string tmp_dir;
    std::string database;

    uint64_t max_file_size{};
    uint64_t max_paste_size{};
    int64_t file_expiry_seconds{};
    int64_t paste_expiry_seconds{};

    unsigned cleanup_interval_seconds{};
    unsigned max_filename_length{};
    unsigned max_pastes_per_page{};
    unsigned max_files_per_page{};

    static Config load(const std::string& path);
};
