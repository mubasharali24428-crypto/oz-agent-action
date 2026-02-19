cmake_minimum_required(VERSION 3.14)
project(huffman_compressor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(huffman main.cpp)
