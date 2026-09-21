#pragma once
#include "config.hpp"
#include "database.hpp"
#include "storage.hpp"
#include <atomic>
#include <thread>

class CleanupWorker {
public:
    CleanupWorker(const Config&, Database&, Storage&);
    ~CleanupWorker();
    void start();
    void stop();
private:
    const Config& c_;
    Database& db_;
    Storage& storage_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
    void loop();
};
