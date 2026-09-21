#pragma once
#include "config.hpp"
#include "database.hpp"
#include <filesystem>
#include <string>

class Storage {
public:
    explicit Storage(const Config& c);
    void initialize();
    std::string make_id() const;
    std::string sanitize_filename(const std::string& n) const;
    std::filesystem::path final_path(const std::string& stored) const;
    std::filesystem::path temp_path(const std::string& id) const;
    void reconcile_orphans(Database& db);
private:
    const Config& c_;
};
