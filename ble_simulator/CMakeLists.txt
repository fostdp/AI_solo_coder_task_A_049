cmake_minimum_required(VERSION 3.14)

set(SIMULATOR_SOURCES
    src/main.cpp
    src/ble_simulator.cpp
)

add_executable(ble_simulator ${SIMULATOR_SOURCES})

target_include_directories(ble_simulator PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/include
)

target_link_libraries(ble_simulator PRIVATE
    Threads::Threads
)

if(WIN32)
    target_link_libraries(ble_simulator PRIVATE ws2_32)
endif()
