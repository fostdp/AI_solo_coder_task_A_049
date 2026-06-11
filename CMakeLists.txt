cmake_minimum_required(VERSION 3.14)
project(tcm_acupuncture_system VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_BACKEND "Build C++ backend server" ON)
option(BUILD_BLE_SIMULATOR "Build BLE data simulator" ON)

find_package(Threads REQUIRED)

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

include_directories(${CMAKE_SOURCE_DIR}/backend/include)

if(BUILD_BACKEND)
    add_subdirectory(backend)
endif()

if(BUILD_BLE_SIMULATOR)
    add_subdirectory(ble_simulator)
endif()
