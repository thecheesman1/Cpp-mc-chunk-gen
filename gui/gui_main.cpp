// mcchunkgen-gui — Desktop tool for Cpp-mc-chunk-gen
// Starts HTTP server, opens browser, lets the user control everything
// from a clean web UI.
//
// One exe. Zero install. Open browser, done.
#include "http_server.h"
#include "api_handler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static HttpServer* g_server = nullptr;

//=============================================================================
// Signal handler for clean shutdown
//=============================================================================
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[GUI] Shutting down...\n");
    if (g_server) g_server->stop();
    exit(0);
}

//=============================================================================
// Main
//=============================================================================
int main(int argc, char** argv) {
    // Parse args
    int port = 0;
    bool no_browser = false;
    std::string webui_dir = "gui/webui";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-browser") == 0) {
            no_browser = true;
        } else if (strcmp(argv[i], "--webui") == 0 && i + 1 < argc) {
            webui_dir = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("mcchunkgen-gui — chunk generator control panel\n");
            printf("Usage: mcchunkgen-gui [options]\n");
            printf("  --port <n>       HTTP port (default: auto)\n");
            printf("  --no-browser     Don't open browser on start\n");
            printf("  --webui <dir>    Web UI directory (default: gui/webui)\n");
            printf("  --help, -h       Show this\n");
            return 0;
        }
    }

    printf("╔══════════════════════════════════════╗\n");
    printf("║       mcchunkgen-gui  v0.1.0         ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Start HTTP server
    HttpServer server;
    g_server = &server;

    uint16_t actual_port = server.start(port);
    if (actual_port == 0) {
        fprintf(stderr, "[GUI] Failed to start HTTP server\n");
        return 1;
    }

    // Register API endpoints
    register_api_handlers(server, webui_dir);

    // Open browser
    if (!no_browser) {
        std::string url = "http://localhost:" + std::to_string(actual_port);
        printf("[GUI] Opening %s\n", url.c_str());
        platform::open_browser(url);
    }

    printf("[GUI] Running. Press Ctrl+C to stop.\n\n");

    // Main loop
    while (server.is_running()) {
        server.poll(100);
    }

    printf("[GUI] Goodbye.\n");
    return 0;
}