cmake_minimum_required(VERSION 3.14)
project(tcm_services CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(CROW_INCLUDE ${CMAKE_SOURCE_DIR}/third_party/crow/include)
set(SHARED_INCLUDE ${CMAKE_SOURCE_DIR}/backend/include ${CMAKE_SOURCE_DIR}/backend/shared/include)
set(SHARED_DIR ${CMAKE_SOURCE_DIR}/backend/src)

function(add_tcm_service NAME MAIN_SRC EXTRA_SRCS)
    add_executable(${NAME} ${MAIN_SRC} ${EXTRA_SRCS})
    target_include_directories(${NAME} PRIVATE
        ${CROW_INCLUDE}
        ${SHARED_INCLUDE}
        ${OPENSSL_INCLUDE_DIR}
    )
    target_link_libraries(${NAME} PRIVATE
        Threads::Threads
        ${OPENSSL_LIBRARIES}
    )
    if(WIN32)
        target_link_libraries(${NAME} PRIVATE ws2_32 mswsock)
    endif()
endfunction()

set(INGEST_SRCS
    ${SHARED_DIR}/mongodb_manager.cpp
    ${SHARED_DIR}/ble_data_receiver.cpp
    ${SHARED_DIR}/data_processor.cpp
    ${SHARED_DIR}/websocket_manager.cpp
    ${SHARED_DIR}/acupoint_data.cpp
)
add_tcm_service(tcm_ingest
    ${CMAKE_CURRENT_SOURCE_DIR}/ingest/main.cpp
    "${INGEST_SRCS}"
)

set(PREDICTOR_SRCS
    ${SHARED_DIR}/random_forest_model.cpp
    ${SHARED_DIR}/acupoint_data.cpp
)
add_tcm_service(tcm_predictor
    ${CMAKE_CURRENT_SOURCE_DIR}/predictor/main.cpp
    "${PREDICTOR_SRCS}"
)

set(TOPOLOGY_SRCS
    ${SHARED_DIR}/meridian_network_analyzer.cpp
    ${SHARED_DIR}/mongodb_manager.cpp
    ${SHARED_DIR}/acupoint_data.cpp
)
add_tcm_service(tcm_topology
    ${CMAKE_CURRENT_SOURCE_DIR}/topology/main.cpp
    "${TOPOLOGY_SRCS}"
)

set(ALERTER_SRCS
    ${SHARED_DIR}/anomaly_detector.cpp
    ${SHARED_DIR}/dingtalk_notifier.cpp
    ${SHARED_DIR}/mongodb_manager.cpp
    ${SHARED_DIR}/acupoint_data.cpp
)
add_tcm_service(tcm_alerter
    ${CMAKE_CURRENT_SOURCE_DIR}/alerter/main.cpp
    "${ALERTER_SRCS}"
)
