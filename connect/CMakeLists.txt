cmake_minimum_required(VERSION 3.14)
project(textlt_connect LANGUAGES CXX)

option(TEXTLT_FETCH_BOOST "Download Boost headers when they are unavailable locally" ON)

find_package(Threads REQUIRED)
# CMake can keep an include directory from a previous build after it disappears.
if(Boost_INCLUDE_DIR AND NOT EXISTS "${Boost_INCLUDE_DIR}/boost/version.hpp")
  unset(Boost_INCLUDE_DIR CACHE)
endif()
find_package(Boost 1.74 CONFIG QUIET COMPONENTS asio beast)
if(TARGET Boost::asio AND TARGET Boost::beast)
  set(TEXTLT_CONNECT_BOOST_TARGETS Boost::asio Boost::beast)
else()
  # Older distributions ship Asio and Beast as part of a single header package.
  find_package(Boost 1.74 CONFIG QUIET)
  if(NOT Boost_FOUND)
    if(POLICY CMP0144)
      cmake_policy(SET CMP0144 NEW)
    endif()
    if(POLICY CMP0167)
      cmake_policy(SET CMP0167 OLD)
    endif()
    find_package(Boost 1.74 QUIET MODULE)
  endif()
  foreach(include_dir IN LISTS Boost_INCLUDE_DIRS)
    if(Boost_FOUND AND TARGET Boost::boost AND
       EXISTS "${include_dir}/boost/asio.hpp" AND EXISTS "${include_dir}/boost/beast.hpp")
      set(TEXTLT_CONNECT_BOOST_TARGETS Boost::boost)
      break()
    endif()
  endforeach()
endif()

if(NOT TEXTLT_CONNECT_BOOST_TARGETS)
  if(NOT TEXTLT_FETCH_BOOST)
    message(FATAL_ERROR "TextLT needs Boost 1.74 or newer with Asio and Beast. Install the headers or enable TEXTLT_FETCH_BOOST.")
  endif()
  include(FetchContent)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  set(textlt_boost_fetch_options)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
    list(APPEND textlt_boost_fetch_options SOURCE_SUBDIR textlt-populate-only)
  endif()
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
    list(APPEND textlt_boost_fetch_options INACTIVITY_TIMEOUT 30)
  endif()
  FetchContent_Declare(textlt_boost
    URL https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2
    URL_HASH SHA256=49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305
    TLS_VERIFY ON
    ${textlt_boost_fetch_options}
  )
  if(FETCHCONTENT_FULLY_DISCONNECTED AND NOT FETCHCONTENT_SOURCE_DIR_TEXTLT_BOOST AND
     NOT EXISTS "${FETCHCONTENT_BASE_DIR}/textlt_boost-src/boost/asio.hpp")
    message(FATAL_ERROR "Boost is missing from the offline build. Configure once with FETCHCONTENT_FULLY_DISCONNECTED=OFF or set FETCHCONTENT_SOURCE_DIR_TEXTLT_BOOST to an extracted Boost release.")
  endif()

  # Only the headers are needed. Do not add Boost's projects or install rules.
  if(CMAKE_VERSION VERSION_LESS 3.18)
    FetchContent_GetProperties(textlt_boost)
    if(NOT textlt_boost_POPULATED)
      FetchContent_Populate(textlt_boost)
    endif()
  else()
    FetchContent_MakeAvailable(textlt_boost)
  endif()
  if(NOT EXISTS "${textlt_boost_SOURCE_DIR}/boost/asio.hpp" OR
     NOT EXISTS "${textlt_boost_SOURCE_DIR}/boost/beast.hpp")
    message(FATAL_ERROR "The Boost source directory does not contain Asio and Beast: ${textlt_boost_SOURCE_DIR}")
  endif()
  add_library(textlt_boost_headers INTERFACE)
  target_include_directories(textlt_boost_headers SYSTEM INTERFACE "${textlt_boost_SOURCE_DIR}")
  set(TEXTLT_CONNECT_BOOST_TARGETS textlt_boost_headers)
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

target_link_libraries(textlt_connect PRIVATE Threads::Threads ${TEXTLT_CONNECT_BOOST_TARGETS} OpenSSL::SSL OpenSSL::Crypto)
if(WIN32)
  target_compile_definitions(textlt_connect PRIVATE _WIN32_WINNT=0x0601)
  target_link_libraries(textlt_connect PRIVATE ws2_32)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(textlt_connect_loopback examples/textlt_loopback.cpp)
  target_link_libraries(textlt_connect_loopback PRIVATE textlt::connect)
  set_target_properties(textlt_connect_loopback PROPERTIES CXX_EXTENSIONS OFF)
endif()
