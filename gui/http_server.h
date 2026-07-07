// Minimal HTTP server wrapping Mongoose + API handlers
#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <atomic>

// Forward declare mg_mgr and mg_connection (opaque)
struct mg_mgr;
struct mg_connection;
struct mg_http_message;

// Callback: return JSON response body
using HttpHandler = std::function<std::string(const mg_http_message*)>;

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    // Start server on given port. Returns actual port (0 on failure).
    uint16_t start(uint16_t port = 0);
    void stop();

    bool is_running() const { return running_; }
    uint16_t port() const { return port_; }

    // Register a handler for a URI prefix
    void get(const std::string& uri_prefix, HttpHandler handler);
    void post(const std::string& uri_prefix, HttpHandler handler);

    // Register a static file path (for frontend)
    void serve_static(const std::string& uri_prefix, const std::string& file_path);

    // Poll once (call in a loop)
    void poll(int ms = 200);

private:
    static void event_handler(struct mg_connection* nc, int ev, void* ev_data, void* fn_data);

    mg_mgr* mgr_;
    std::atomic<bool> running_{false};
    uint16_t port_{0};
};

// Platform helpers
namespace platform {
    // Find a free TCP port (bind to 0, read port, close)
    uint16_t find_free_port();

    // Open URL in default browser
    void open_browser(const std::string& url);
} // namespace platform