// HTTP server using Mongoose (single-header C lib)
#include "http_server.h"
#define MONGOOSE_IMPLEMENTATION
#include "mongoose.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#endif

//=============================================================================
// HttpServer impl
//=============================================================================
HttpServer::HttpServer() : mgr_(nullptr) {}
HttpServer::~HttpServer() { stop(); }

struct ServerContext {
    HttpServer* server;
    std::map<std::string, HttpHandler> get_handlers;
    std::map<std::string, HttpHandler> post_handlers;
    std::map<std::string, std::string> static_files;
};

uint16_t HttpServer::start(uint16_t port) {
    if (port == 0) port = platform::find_free_port();
    if (port == 0) port = 18000; // fallback

    mgr_ = (mg_mgr*)calloc(1, sizeof(mg_mgr));
    mg_mgr_init(mgr_);

    // Bind
    char addr[64];
    snprintf(addr, sizeof(addr), "http://0.0.0.0:%u", port);

    auto* ctx = new ServerContext;
    ctx->server = this;

    mg_connection* nc = mg_http_listen(mgr_, addr, event_handler, ctx);
    if (!nc) {
        fprintf(stderr, "[HTTP] Failed to bind %s\n", addr);
        free(mgr_);
        mgr_ = nullptr;
        delete ctx;
        return 0;
    }

    port_ = port;
    running_ = true;
    printf("[HTTP] Server on http://localhost:%u\n", port);
    return port;
}

void HttpServer::stop() {
    running_ = false;
    if (mgr_) {
        mg_mgr_free(mgr_);
        free(mgr_);
        mgr_ = nullptr;
    }
}

void HttpServer::get(const std::string& uri_prefix, HttpHandler handler) {
    auto* ctx = (ServerContext*)(mgr_ ? mgr_->user_data : nullptr);
    if (!ctx) return;
    ctx->get_handlers[uri_prefix] = std::move(handler);
}

void HttpServer::post(const std::string& uri_prefix, HttpHandler handler) {
    auto* ctx = (ServerContext*)(mgr_ ? mgr_->user_data : nullptr);
    if (!ctx) return;
    ctx->post_handlers[uri_prefix] = std::move(handler);
}

void HttpServer::serve_static(const std::string& uri_prefix, const std::string& file_path) {
    auto* ctx = (ServerContext*)(mgr_ ? mgr_->user_data : nullptr);
    if (!ctx) return;
    ctx->static_files[uri_prefix] = file_path;
}

void HttpServer::poll(int ms) {
    if (mgr_) mg_mgr_poll(mgr_, ms);
}

//=============================================================================
// Static file helpers
//=============================================================================
static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string mime_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html; charset=utf-8";
    if (path.ends_with(".css"))  return "text/css; charset=utf-8";
    if (path.ends_with(".js"))   return "application/javascript; charset=utf-8";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".ico"))  return "image/x-icon";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    if (path.ends_with(".woff2"))return "font/woff2";
    return "text/plain; charset=utf-8";
}

// Extract URI path from mg_http_message
static std::string get_uri(const mg_http_message* hm) {
    return std::string(hm->uri.ptr, hm->uri.len);
}

//=============================================================================
// Event handler (dispatch)
//=============================================================================
void HttpServer::event_handler(mg_connection* nc, int ev, void* ev_data, void* fn_data) {
    auto* ctx = (ServerContext*)fn_data;
    if (!ctx) return;

    if (ev == MG_EV_HTTP_MSG) {
        auto* hm = (mg_http_message*)ev_data;
        std::string uri = get_uri(hm);

        // Determine method
        bool is_get = mg_vcmp(&hm->method, "GET") == 0;
        bool is_post = mg_vcmp(&hm->method, "POST") == 0;

        // Try API handlers
        if (is_post) {
            for (auto& [prefix, handler] : ctx->post_handlers) {
                if (uri == prefix || uri.find(prefix) == 0) {
                    try {
                        std::string json = handler(hm);
                        mg_http_reply(nc, 200, "Content-Type: application/json\r\n", "%s", json.c_str());
                    } catch (const std::exception& e) {
                        mg_http_reply(nc, 500, "Content-Type: application/json\r\n",
                                      "{\"error\":\"%s\"}", e.what());
                    }
                    return;
                }
            }
        }

        if (is_get) {
            // API handlers first
            for (auto& [prefix, handler] : ctx->get_handlers) {
                if (uri == prefix || uri.find(prefix) == 0) {
                    try {
                        std::string json = handler(hm);
                        mg_http_reply(nc, 200, "Content-Type: application/json\r\n", "%s", json.c_str());
                    } catch (const std::exception& e) {
                        mg_http_reply(nc, 500, "Content-Type: application/json\r\n",
                                      "{\"error\":\"%s\"}", e.what());
                    }
                    return;
                }
            }

            // Static files
            for (auto& [prefix, file_path] : ctx->static_files) {
                if (uri == prefix) {
                    std::string content = read_file(file_path);
                    if (!content.empty()) {
                        std::string mime = mime_type(file_path);
                        char hdr[128];
                        snprintf(hdr, sizeof(hdr), "Content-Type: %s\r\n", mime.c_str());
                        mg_http_reply(nc, 200, hdr, "%s", content.c_str());
                    } else {
                        mg_http_reply(nc, 404, "", "Not Found");
                    }
                    return;
                }
            }

            // If URI is /, serve index.html
            if (uri == "/" || uri.empty()) {
                auto it = ctx->static_files.find("/");
                if (it != ctx->static_files.end()) {
                    std::string content = read_file(it->second);
                    if (!content.empty()) {
                        mg_http_reply(nc, 200, "Content-Type: text/html; charset=utf-8\r\n",
                                      "%s", content.c_str());
                        return;
                    }
                }
            }
        }

        mg_http_reply(nc, 404, "", "Not Found");
    }
}

//=============================================================================
// Platform helpers
//=============================================================================
uint16_t platform::find_free_port() {
    int fd;
    struct sockaddr_in addr;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return 0;
#else
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr*)&addr, &len) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return 0;
    }

    uint16_t port = ntohs(addr.sin_port);

#ifdef _WIN32
    closesocket(fd);
    WSACleanup();
#else
    close(fd);
#endif

    return port;
}

void platform::open_browser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(0, "open", url.c_str(), 0, 0, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "open '%s'", url.c_str());
    system(cmd);
#else
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' 2>/dev/null || sensible-browser '%s' 2>/dev/null || firefox '%s' 2>/dev/null &",
             url.c_str(), url.c_str(), url.c_str());
    // Fork to avoid blocking
    if (fork() == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char*)nullptr);
        _exit(0);
    }
#endif
}