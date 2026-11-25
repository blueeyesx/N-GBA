
cmake_minimum_required(VERSION 3.28)

project(ngba_native_gui LANGUAGES CXX ASM)

include(SymbianApp)

symbian_add_executable(
    ngba_native_gui
    src/window_server.cc
    src/ngba_paths.cc
    src/rom_loader.cc
    src/arm7tdmi.cc
    src/gba_memory.cc
    src/cpu_selftest.cc
)

target_link_libraries(
    ngba_native_gui
    PRIVATE
    Symbian::Stackless
    Symbian::WindowServer
    Symbian::Gdi
    Symbian::FileServer
)
