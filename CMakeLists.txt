cmake_minimum_required(VERSION 3.16)
project(cpp-mc-chunk-gen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Treat .cu files as C++ (for CPU mock mode via cuda_mock.h)
set_source_files_properties(generator.cu PROPERTIES LANGUAGE CXX)

# ============================================================
# Threads
# ============================================================
find_package(Threads REQUIRED)

# ============================================================
# CLI target — offline chunk generator (CPU mock mode)
# ============================================================
add_executable(chunkgen_offline
    chunkgen_offline.cpp
    generator.cu
)

target_include_directories(chunkgen_offline PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(chunkgen_offline PRIVATE
    Threads::Threads
    m
)

# ============================================================
# GUI target — HTTP server + web UI backend
# ============================================================
set(GUI_SRC
    gui/gui_main.cpp
    gui/http_server.cpp
    gui/api_handler.cpp
)

add_executable(mcchunkgen-gui ${GUI_SRC})

target_include_directories(mcchunkgen-gui PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/gui
)

if(WIN32)
    target_link_libraries(mcchunkgen-gui PRIVATE ws2_32 shell32)
else()
    target_link_libraries(mcchunkgen-gui PRIVATE m Threads::Threads)
endif()

# ============================================================
# Copy webui files next to the binary for serving
# ============================================================
add_custom_command(TARGET mcchunkgen-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/gui/webui
        $<TARGET_FILE_DIR:mcchunkgen-gui>/gui/webui
    COMMENT "Copying web UI files..."
)