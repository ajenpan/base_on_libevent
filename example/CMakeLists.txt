cmake_minimum_required(VERSION 3.16)

project(event-example)

# Set default build to release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose Release or Debug" FORCE)
endif()

IF(CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-DDEBUG)
ELSEIF()
    add_definitions(-Wno-dev)
ENDIF()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" ${CMAKE_MODULE_PATH})

message("CMAKE_MODULE_PATH:${CMAKE_MODULE_PATH}")

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC_FILES)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX)
    add_compile_options(-fPIC)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()

set_property(TARGET PROPERTY POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    if((MSVC) AND(MSVC_VERSION GREATER_EQUAL 1914))
        add_compile_options("/Zc:__cplusplus")
    endif()

    add_definitions(-DWIN_EXPORT)

    add_compile_options("/EHsc")
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Libevent CONFIG REQUIRED)
link_libraries(libevent::core libevent::extra)

find_package(fmt CONFIG REQUIRED)
link_libraries(fmt::fmt-header-only)

add_executable(ws_example ws_example.cpp)

#add_executable(tcp_example tcp_example.cpp)

# add_executable(example curl_example.cpp)
# add_executable(example httpserver_example.cpp)

# else()
# set(third_party_dir $ENV{HOME}/third_party)

# if(NOT EXISTS ${third_party_dir})
# set(third_party_dir ${CMAKE_SOURCE_DIR}/../../third_party)
# endif()

# message("define third_party_dir:${third_party_dir}")

# target_include_directories(${target} PRIVATE ${third_party_dir}/fmt-9.1.0/include)
# target_include_directories(${target} PRIVATE ${third_party_dir}/spdlog-1.11.0/include)

# target_include_directories(${target} PRIVATE ${third_party_dir}/libevent-release-2.1.12-stable/prebuild/include)
# target_link_libraries(${target} PRIVATE
# ${third_party_dir}/libevent-release-2.1.12-stable/prebuild/lib/libevent_core.a
# ${third_party_dir}/libevent-release-2.1.12-stable/prebuild/lib/libevent_extra.a
# ${third_party_dir}/libevent-release-2.1.12-stable/prebuild/lib/libevent_pthreads.a
# )
# endif()
