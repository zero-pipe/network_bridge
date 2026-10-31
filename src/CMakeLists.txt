set(NB_BASE_SOURCES
    base/alloc.c
    base/buf.c
    base/poolset.c
    base/slotpool.c
    base/crc32.c
)

add_library(nb_base STATIC ${NB_BASE_SOURCES})
add_library(nb::base ALIAS nb_base)

target_include_directories(nb_base
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(nb_base PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    OUTPUT_NAME nb_base
)

install(TARGETS nb_base
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(NB_SOURCES
    core/node.c
    core/node_access.c
    core/transport_factory.c
    core/dispatch.c
    core/service.c
    core/reply.c
    core/proxy.c
    core/loan.c
    core/stub.c
    core/stream.c
    core/message_view.c
    core/metadata.c
    core/service_table.c
    core/sync.c
    core/pending.c
    core/dedup.c
    core/codec.c
    core/payload.c
    utils/log.c
    utils/error.c
    utils/metrics.c
    utils/poller_alloc.c
    utils/util.c
    utils/iobuf.c
    utils/endpoint.c
    utils/qos.c
    utils/json/codec_json.c
    message/frame.c
    message/envelope.c
    router/router.c
    registry/registry.c
    discovery/discovery.c
    config/config.c
    transport/common/framing.c
    transport/common/transport.c
    transport/common/provider.c
    transport/common/rx_window.c
    transport/common/reasm.c
    transport/common/tx_ring.c
    transport/udp/udp.c
    transport/tcp/tcp.c
    transport/shm/shm.c
    transport/shm/shm_segment.c
    ${CMAKE_SOURCE_DIR}/3rdparty/cjson/cJSON.c
)

if(NB_ENABLE_DDS)
    list(APPEND NB_SOURCES
        transport/dds/cyclone_dds.c
        transport/dds/cy_dds_message.c)
endif()

add_library(nb STATIC ${NB_SOURCES})
add_library(nb::nb ALIAS nb)
# Backward-compatible target name for downstreams that still link `rpc`.
add_library(rpc ALIAS nb)

target_include_directories(nb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/base
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/json
        ${CMAKE_CURRENT_SOURCE_DIR}/message
        ${CMAKE_CURRENT_SOURCE_DIR}/transport/common
        ${CMAKE_CURRENT_SOURCE_DIR}/transport/dds
        ${CMAKE_CURRENT_SOURCE_DIR}/router
        ${CMAKE_CURRENT_SOURCE_DIR}/registry
        ${CMAKE_CURRENT_SOURCE_DIR}/discovery
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        ${CMAKE_SOURCE_DIR}/3rdparty/uthash
        ${CMAKE_SOURCE_DIR}/3rdparty/cjson
)

install(TARGETS nb
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# elog 只是 log.c 的实现细节：静态库最终仍需链接，但不向使用者传播头文件路径。
target_link_libraries(nb PUBLIC ntk nb_base PRIVATE elog)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # shm_open()/sem_timedwait() live in librt on older glibc.
    target_link_libraries(nb PUBLIC rt)
endif()

if(NB_ENABLE_DDS)
    target_link_libraries(nb PUBLIC nb_cyclonedds)
endif()

set_target_properties(nb PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    OUTPUT_NAME nb
)

if(MSVC)
    target_compile_definitions(nb PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
