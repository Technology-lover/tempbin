#include "config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

Config Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open config: " + path);
    json j; in >> j;

    Config c;
    c.bind_address = j.at("server").value("bind_address", "127.0.0.1");
    c.port = j.at("server").at("port").get<uint16_t>();
    c.threads = j.at("server").value("threads", 4u);

    auto s = j.at("storage");
    c.storage_root = s.at("root").get<std::string>();
    c.files_dir = s.at("files_dir").get<std::string>();
    c.tmp_dir = s.at("tmp_dir").get<std::string>();
    c.database = s.at("database").get<std::string>();

    auto l = j.at("limits");
    const auto mb = l.value("max_file_size_mb", 20ULL);
    const auto kb = l.value("max_paste_size_kb", 512ULL);
    c.max_file_size = mb * 1024ULL * 1024ULL;
    c.max_paste_size = kb * 1024ULL;
    c.file_expiry_seconds = l.value("file_expiry_hours", 24LL) * 3600LL;
    c.paste_expiry_seconds = l.value("paste_expiry_hours", 24LL) * 3600LL;

    auto cl = j.at("cleanup");
    c.cleanup_interval_seconds = cl.value("interval_seconds", 60u);

    auto sec = j.at("security");
    c.max_filename_length = sec.value("max_filename_length", 180u);
    c.max_pastes_per_page = sec.value("max_pastes_per_page", 50u);
    c.max_files_per_page = sec.value("max_files_per_page", 50u);

    if (c.threads == 0 || c.cleanup_interval_seconds == 0)
        throw std::runtime_error("Invalid zero-valued runtime configuration");
    if (c.max_file_size == 0 || c.max_paste_size == 0)
        throw std::runtime_error("Upload/paste limits must be greater than zero");
    return c;
}
