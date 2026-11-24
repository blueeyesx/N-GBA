
cmake_minimum_required(VERSION 3.28)

project(ngba_native_gui LANGUAGES CXX ASM)

include(SymbianApp)

symbian_add_executable(
    ngba_native_gui
    src/window_server.cc
    src/rom_loader.cc
)

target_link_libraries(
    ngba_native_gui
    PRIVATE
    Symbian::Stackless
    Symbian::WindowServer
    Symbian::Gdi
)
