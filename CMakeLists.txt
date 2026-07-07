cmake_minimum_required(VERSION 3.16)
project(cpp-mc-chunk-gen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Core sources (serialization)
# ============================================================
set(CORE_SRC
    chunkgen_offline.cpp
    nbt.h
    anvil.h
    generator.h
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

# Need socket libs
if(WIN32)
    target_link_libraries(mcchunkgen-gui PRIVATE ws2_32 shell32)
else()
    target_link_libraries(mcchunkgen-gui PRIVATE m)
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