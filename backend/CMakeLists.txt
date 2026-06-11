cmake_minimum_required(VERSION 3.14)
project(tcm_backend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(BACKEND_SOURCES
    src/main_crow.cpp
    src/http_server.cpp
    src/mongodb_manager.cpp
    src/acupoint_data.cpp
    src/ble_data_receiver.cpp
    src/random_forest_model.cpp
    src/meridian_network_analyzer.cpp
    src/anomaly_detector.cpp
    src/dingtalk_notifier.cpp
    src/websocket_manager.cpp
    src/data_processor.cpp
)

add_executable(tcm_backend ${BACKEND_SOURCES})

target_include_directories(tcm_backend PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/crow/include
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/shared/include
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(tcm_backend PRIVATE
    Threads::Threads
    ${OPENSSL_LIBRARIES}
)

if(WIN32)
    target_link_libraries(tcm_backend PRIVATE ws2_32 mswsock)
endif()

add_subdirectory(services)
