#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

struct FileRecord {
    std::string id, original_name, stored_name;
    uint64_t size{};
    int64_t created_at{}, expires_at{};
};

struct PasteRecord {
    std::string id, content;
    int64_t created_at{}, expires_at{};
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void initialize();
    bool insert_file(const FileRecord& r);
    bool insert_paste(const PasteRecord& r);
    bool get_file_by_stored_name(const std::string& stored, FileRecord& out);
    bool get_paste(const std::string& id, PasteRecord& out);
    std::vector<FileRecord> list_files(int64_t now, unsigned limit);
    std::vector<PasteRecord> list_pastes(int64_t now, unsigned limit);
    std::vector<FileRecord> expired_files(int64_t now);
    std::vector<PasteRecord> expired_pastes(int64_t now);
    bool delete_file(const std::string& id);
    bool delete_paste(const std::string& id);
    bool file_exists_by_stored_name(const std::string& stored);
    bool paste_exists(const std::string& id);

private:
    sqlite3* db_{};
    std::mutex mutex_;
    void exec(const char* sql);
};
