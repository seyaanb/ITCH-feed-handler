cmake_minimum_required(VERSION 3.15)
project(ZeroCopyFeedHandler VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -fno-strict-aliasing -Wall -Wextra -Werror")

# 1. Zero-Copy Engine
add_executable(feed_handler_bench
    benchmarks/FeedHandlerBench.cpp
    src/network/PcapReader.cpp
    src/itch/Parser.cpp
)
target_link_libraries(feed_handler_bench pthread)

# 2. Naive Baseline Engine
add_executable(naive_bench
    benchmarks/NaiveFeedHandlerBench.cpp
)
target_link_libraries(naive_bench pthread)