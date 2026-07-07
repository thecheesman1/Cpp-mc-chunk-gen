// API handlers — bridges web UI to C++ backend
#include "api_handler.h"
#include "http_server.h"
#include "../generator.h"
#include "../anvil.h"
#include "../nbt.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <regex>
#include <thread>
#include <chrono>
#include <mutex>

//=============================================================================
// Global state (shared between API calls)
//=============================================================================
struct AppState {
    // Generation
    int64_t seed = 42;
    int radius = 5;
    int center_x = 0;
    int center_z = 0;
    int threads = 4;
    std::string world_path = "world";
    bool use_vulkan = false;

    // Generation progress
    std::atomic<int> chunks_done{0};
    std::atomic<int> chunks_total{0};
    std::atomic<bool> generating{false};
    std::thread gen_thread;

    // Server
    std::atomic<bool> server_running{false};
    std::string server_pid;

    // Benchmark
    std::string last_benchmark;
};

static AppState g_state;

//=============================================================================
// JSON helpers (no deps, manual builder)
//=============================================================================
static std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if ((unsigned char)c < 0x20) out += "\\u" + std::to_string((unsigned char)c);
        else out += c;
    }
    out += "\"";
    return out;
}

static std::string json_kv(const std::string& k, const std::string& v) {
    return json_str(k) + ":" + json_str(v);
}

static std::string json_kv(const std::string& k, int64_t v) {
    return json_str(k) + ":" + std::to_string(v);
}

static std::string json_kv(const std::string& k, bool v) {
    return json_str(k) + ":" + (v ? "true" : "false");
}

static std::string json_kv(const std::string& k, double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", v);
    return json_str(k) + ":" + buf;
}

//=============================================================================
// Request body extraction
//=============================================================================
static std::string body_str(const mg_http_message* hm) {
    return std::string(hm->body.ptr, hm->body.len);
}

static std::string query_param(const mg_http_message* hm, const std::string& name) {
    char buf[256];
    mg_http_get_var(&hm->query, name.c_str(), buf, sizeof(buf));
    return std::string(buf);
}

//=============================================================================
// Handlers
//=============================================================================
static std::string handle_ping(const mg_http_message*) {
    return "{\"ok\":true,\"name\":\"mcchunkgen\",\"version\":\"0.1.0\"}";
}

static std::string handle_status(const mg_http_message*) {
    std::string json = "{";
    json += json_kv("generating", g_state.generating.load()) + ",";
    json += json_kv("chunks_done", g_state.chunks_done.load()) + ",";
    json += json_kv("chunks_total", g_state.chunks_total.load()) + ",";
    json += json_kv("server_running", g_state.server_running.load());
    json += "}";
    return json;
}

static std::string handle_generate(const mg_http_message* hm) {
    if (g_state.generating.load()) {
        return "{\"error\":\"Already generating\"}";
    }

    // Parse JSON body (simple)
    std::string body = body_str(hm);
    // std::regex for extracting simple fields
    auto get_field = [&](const std::string& key) -> std::string {
        std::regex re("\"" + key + "\"\\s*:\\s*\"?([^,\"}\\n]+)\"?");
        std::smatch m;
        if (std::regex_search(body, m, re)) return m[1];
        return "";
    };
    auto get_int = [&](const std::string& key, int def) -> int {
        auto s = get_field(key);
        return s.empty() ? def : std::stoi(s);
    };

    g_state.seed = get_int("seed", g_state.seed);
    g_state.radius = get_int("radius", g_state.radius);
    g_state.center_x = get_int("center_x", g_state.center_x);
    g_state.center_z = get_int("center_z", g_state.center_z);
    g_state.threads = get_int("threads", g_state.threads);
    std::string wp = get_field("world_path");
    if (!wp.empty()) g_state.world_path = wp;

    // Start generation in background thread
    g_state.generating = true;
    g_state.chunks_done = 0;
    g_state.chunks_total = (g_state.radius * 2 + 1) * (g_state.radius * 2 + 1);

    g_state.gen_thread = std::thread([&]() {
        // This is where we call into the chunk generator
        // For now, a stub that just counts progress
        int r = g_state.radius;
        int total = (2*r+1)*(2*r+1);
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                int cx = g_state.center_x + dx;
                int cz = g_state.center_z + dz;
                
                // TODO: call actual chunk generation
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                g_state.chunks_done.fetch_add(1);
            }
        }
        g_state.generating = false;
    });
    g_state.gen_thread.detach();

    return "{\"ok\":true,\"total\":" + std::to_string(g_state.chunks_total.load()) + "}";
}

static std::string handle_inspect(const mg_http_message* hm) {
    std::string path = query_param(hm, "path");
    if (path.empty()) {
        // Try body
        std::string body = body_str(hm);
        std::regex re("\"path\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch m;
        if (std::regex_search(body, m, re)) path = m[1];
    }

    // Open and inspect region file
    std::string result = "{\"path\":" + json_str(path);

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        result += ",\"error\":\"File not found\"";
        result += "}";
        return result;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read location table
    uint8_t header[8192];
    if (fread(header, 1, 8192, f) != 8192) {
        fclose(f);
        result += ",\"error\":\"Failed to read header\"}";
        return result;
    }

    int chunk_count = 0;
    int min_sections = 999;
    int max_sections = 0;
    int dv = 0;

    for (int i = 0; i < 1024; i++) {
        int off = (header[i*4] << 16) | (header[i*4+1] << 8) | header[i*4+2];
        int cnt = header[i*4 + 3];
        if (off > 0 && cnt > 0) {
            chunk_count++;
            // Read first chunk to get DataVersion and section count
            // (only for first valid chunk to keep fast)
            if (chunk_count == 1) {
                uint8_t chdr[5];
                long pos = (long)off * 4096;
                fseek(f, pos, SEEK_SET);
                if (fread(chdr, 1, 5, f) == 5) {
                    uint32_t len = (chdr[0] << 24) | (chdr[1] << 16) | (chdr[2] << 8) | chdr[3];
                    uint8_t comp = chdr[4];
                    (void)comp; // unused for now
                    
                    std::vector<uint8_t> nbt(len > 1 ? len - 1 : 0);
                    if (!nbt.empty() && fread(nbt.data(), 1, nbt.size(), f) == nbt.size()) {
                        // Parse DataVersion from NBT
                        auto pos = nbt.find("DataVersion");
                        if (pos != std::string::npos && pos + 11 < nbt.size()) {
                            dv = (nbt[pos+8] << 24) | (nbt[pos+9] << 16) | (nbt[pos+10] << 8) | nbt[pos+11];
                        }
                        // Count sections (TAG_Byte "Y" tags)
                        int sec_count = 0;
                        size_t sp = 0;
                        while ((sp = nbt.find("\x01\x00\x01Y", sp)) != std::string::npos) {
                            sec_count++;
                            sp += 4;
                        }
                        min_sections = max_sections = sec_count;
                    }
                }
            }
        }
    }

    fclose(f);

    result += ",\"chunk_count\":" + std::to_string(chunk_count);
    result += ",\"file_size\":" + std::to_string(fsize);
    if (dv) result += ",\"data_version\":" + std::to_string(dv);
    if (min_sections <= max_sections) {
        result += ",\"sections\":" + std::to_string(max_sections);
    }
    result += "}";
    return result;
}

static std::string handle_benchmark(const mg_http_message*) {
    if (g_state.generating.load()) {
        return "{\"error\":\"Busy generating\"}";
    }

    std::string result = "{";
    result += json_kv("status", "running") + ",";
    result += json_kv("progress", 0);
    result += "}";

    // TODO: actually run benchmark in background thread
    return result;
}

static std::string handle_server_start(const mg_http_message*) {
    if (g_state.server_running.load()) {
        return "{\"error\":\"Server already running\"}";
    }

    // TODO: start MC server process
    g_state.server_running = true;
    return "{\"ok\":true}";
}

static std::string handle_server_stop(const mg_http_message*) {
    if (!g_state.server_running.load()) {
        return "{\"error\":\"Server not running\"}";
    }

    // TODO: kill MC server process
    g_state.server_running = false;
    return "{\"ok\":true}";
}

//=============================================================================
// Registration
//=============================================================================
void register_api_handlers(HttpServer& server, const std::string& webui_dir) {
    server.get("/api/ping", handle_ping);
    server.get("/api/status", handle_status);
    server.post("/api/generate", handle_generate);
    server.get("/api/inspect", handle_inspect);
    server.post("/api/inspect", handle_inspect);
    server.post("/api/benchmark", handle_benchmark);
    server.post("/api/server/start", handle_server_start);
    server.post("/api/server/stop", handle_server_stop);

    // Serve static files
    server.serve_static("/", webui_dir + "/index.html");
    server.serve_static("/style.css", webui_dir + "/style.css");
    server.serve_static("/app.js", webui_dir + "/app.js");
    server.serve_static("/tabs/generator.js", webui_dir + "/tabs/generator.js");
    server.serve_static("/tabs/inspector.js", webui_dir + "/tabs/inspector.js");
    server.serve_static("/tabs/benchmark.js", webui_dir + "/tabs/benchmark.js");
    server.serve_static("/tabs/compress.js", webui_dir + "/tabs/compress.js");
    server.serve_static("/tabs/server.js", webui_dir + "/tabs/server.js");

    printf("[API] Registered %zu endpoints\n", 9UL);
}