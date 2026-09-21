#pragma once
#include "config.hpp"
#include "database.hpp"
#include "storage.hpp"
#include <crow.h>

void register_routes(crow::SimpleApp& app, const Config& c, Database& db, Storage& storage);
