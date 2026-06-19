cmake_minimum_required(VERSION 3.20)
project(LexerFixtures VERSION 1.0 LANGUAGES CXX)

set(SAMPLE_FLAG ON CACHE BOOL "Enable sample")
add_executable(sample main.cpp)
target_link_libraries(sample PRIVATE ftxui::dom)
