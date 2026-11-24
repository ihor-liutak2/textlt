cmake_minimum_required(VERSION 3.14)
project(textlt_connect LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(Boost 1.74 CONFIG QUIET)
if(NOT Boost_FOUND)
  if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
  endif()
  if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
  endif()
  find_package(Boost 1.74 REQUIRED MODULE)
endif()
find_package(OpenSSL 3.0 REQUIRED COMPONENTS SSL Crypto)

add_library(textlt_connect STATIC
  src/identity.cpp
  src/service.cpp
  src/providers/localsend/https.cpp
  src/providers/localsend/localsend_provider.cpp
  src/providers/textlt/discovery.cpp
  src/providers/textlt/protocol.cpp
  src/providers/textlt/socket.cpp
  src/providers/textlt/tls.cpp
  src/providers/textlt/textlt_provider.cpp
)
add_library(textlt::connect ALIAS textlt_connect)

target_compile_features(textlt_connect PUBLIC cxx_std_17)
target_include_directories(textlt_connect PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_include_directories(textlt_connect PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")
set_target_properties(textlt_connect PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(textlt_connect PRIVATE Threads::Threads Boost::boost OpenSSL::SSL OpenSSL::Crypto)
if(WIN32)
  target_link_libraries(textlt_connect PRIVATE ws2_32)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(textlt_connect_loopback examples/textlt_loopback.cpp)
  target_link_libraries(textlt_connect_loopback PRIVATE textlt::connect)
  set_target_properties(textlt_connect_loopback PROPERTIES CXX_EXTENSIONS OFF)
endif()
