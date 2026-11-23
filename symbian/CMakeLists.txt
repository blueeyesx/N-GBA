# Target-specific frontend placeholder. Configure only inside a symbian-platform SDK workspace.
cmake_minimum_required(VERSION 3.16)
project(NGBA_Symbian LANGUAGES CXX)
add_executable(ngba_symbian ../src/main.cpp ../src/app.cpp ../core/gba_core.cpp ../core/adapter/mock_core.cpp)
target_include_directories(ngba_symbian PRIVATE ../include ../core ../ui)
