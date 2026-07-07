// API endpoint handlers for the web UI
#pragma once
#include "http_server.h"

// Register all API endpoints on the server
void register_api_handlers(HttpServer& server, const std::string& webui_dir);