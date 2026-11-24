cmake_minimum_required(VERSION 3.28)

project(ngba_native_gui LANGUAGES CXX ASM)

include(SymbianApp)

symbian_add_executable(ngba_native_gui src/main.cpp)

target_link_libraries(ngba_native_gui PRIVATE
    Symbian::Stackless
    Symbian::WindowServer
    Symbian::Gdi
)
