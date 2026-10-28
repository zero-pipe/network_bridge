# Test taxonomy (also used by tools/gate.py):
#   unit = pure module / codec / config logic, no sockets
#   e2e  = end-to-end loopback / streaming with real threads and sockets
#
# Run one category:
#   ctest --test-dir build -L unit
#   ctest --test-dir build -L e2e
# Or use the unified gate:
#   python3 tools/gate.py unit
#   python3 tools/gate.py e2e

add_executable(test_framing test_framing.c)
target_include_directories(test_framing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/transport/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/message)
target_link_libraries(test_framing PRIVATE rpc)
add_test(NAME unit.framing COMMAND test_framing)
set_tests_properties(unit.framing PROPERTIES LABELS "unit;framing" TIMEOUT 120)

add_executable(test_loopback test_loopback.c)
target_link_libraries(test_loopback PRIVATE rpc)
add_test(NAME e2e.loopback COMMAND test_loopback)
set_tests_properties(e2e.loopback PROPERTIES LABELS "e2e;loopback" TIMEOUT 300)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE rpc)
add_test(NAME unit.config COMMAND test_config)
set_tests_properties(unit.config PROPERTIES LABELS "unit;config" TIMEOUT 120)

add_executable(test_metrics test_metrics.c)
target_include_directories(test_metrics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils)
target_link_libraries(test_metrics PRIVATE rpc)
add_test(NAME unit.metrics COMMAND test_metrics)
set_tests_properties(unit.metrics PROPERTIES LABELS "unit;metrics" TIMEOUT 120)

add_executable(test_registry test_registry.c)
target_include_directories(test_registry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/registry)
target_link_libraries(test_registry PRIVATE rpc)
add_test(NAME unit.registry COMMAND test_registry)
set_tests_properties(unit.registry PROPERTIES LABELS "unit;registry" TIMEOUT 120)

add_executable(test_reliable test_reliable.c)
target_include_directories(test_reliable PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/transport/common)
target_link_libraries(test_reliable PRIVATE rpc)
add_test(NAME unit.reliable COMMAND test_reliable)
set_tests_properties(unit.reliable PROPERTIES LABELS "unit;reliable" TIMEOUT 120)

add_executable(test_stream test_stream.c)
target_link_libraries(test_stream PRIVATE rpc)
add_test(NAME e2e.stream COMMAND test_stream)
set_tests_properties(e2e.stream PROPERTIES LABELS "e2e;stream" TIMEOUT 300)

add_executable(test_poller_alloc test_poller_alloc.c)
target_include_directories(test_poller_alloc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/discovery
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/message
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/registry
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/router
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/transport/common
    ${CMAKE_SOURCE_DIR}/3rdparty/uthash)
target_link_libraries(test_poller_alloc PRIVATE rpc)
add_test(NAME unit.poller_alloc COMMAND test_poller_alloc)
set_tests_properties(unit.poller_alloc PROPERTIES LABELS "unit;poller_alloc" TIMEOUT 120)

add_executable(test_iobuf test_iobuf.c)
target_include_directories(test_iobuf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils)
target_link_libraries(test_iobuf PRIVATE rpc)
add_test(NAME unit.iobuf COMMAND test_iobuf)
set_tests_properties(unit.iobuf PROPERTIES LABELS "unit;iobuf" TIMEOUT 120)

add_executable(test_buffer test_buffer.c)
target_link_libraries(test_buffer PRIVATE nb_base)
add_test(NAME unit.buffer COMMAND test_buffer)
set_tests_properties(unit.buffer PROPERTIES LABELS "unit;buffer" TIMEOUT 120)

add_executable(test_buf_concurrency test_buf_concurrency.c)
target_link_libraries(test_buf_concurrency PRIVATE ntk nb_base)
add_test(NAME unit.buf_concurrency COMMAND test_buf_concurrency)
set_tests_properties(unit.buf_concurrency PROPERTIES LABELS "unit;concurrency" TIMEOUT 300)

add_executable(test_slotpool test_slotpool.c)
target_link_libraries(test_slotpool PRIVATE nb_base)
add_test(NAME unit.slotpool COMMAND test_slotpool)
set_tests_properties(unit.slotpool PROPERTIES LABELS "unit;slotpool" TIMEOUT 120)

add_executable(test_poolset test_poolset.c)
target_link_libraries(test_poolset PRIVATE rpc)
add_test(NAME unit.poolset COMMAND test_poolset)
set_tests_properties(unit.poolset PROPERTIES LABELS "unit;poolset" TIMEOUT 60)

add_executable(test_api test_api.c)
target_link_libraries(test_api PRIVATE rpc)
add_test(NAME e2e.api COMMAND test_api)
set_tests_properties(e2e.api PROPERTIES LABELS "e2e;api" TIMEOUT 120)

add_executable(test_move test_move.c)
target_link_libraries(test_move PRIVATE rpc)
add_test(NAME e2e.move COMMAND test_move)
set_tests_properties(e2e.move PROPERTIES LABELS "e2e;move" TIMEOUT 120)

add_executable(test_worker test_worker.c)
target_link_libraries(test_worker PRIVATE rpc)
add_test(NAME e2e.worker COMMAND test_worker)
set_tests_properties(e2e.worker PROPERTIES LABELS "e2e;worker" TIMEOUT 120)

add_executable(test_semantics test_semantics.c)
target_link_libraries(test_semantics PRIVATE rpc)
add_test(NAME e2e.semantics COMMAND test_semantics)
set_tests_properties(e2e.semantics PROPERTIES LABELS "e2e;semantics" TIMEOUT 120)

add_executable(test_headers test_headers.c)
target_link_libraries(test_headers PRIVATE rpc)
add_test(NAME unit.headers COMMAND test_headers)
set_tests_properties(unit.headers PROPERTIES LABELS "unit;headers" TIMEOUT 30)

add_executable(test_contracts test_contracts.c)
target_link_libraries(test_contracts PRIVATE rpc)
add_test(NAME unit.contracts COMMAND test_contracts)
set_tests_properties(unit.contracts PROPERTIES LABELS "unit;contracts" TIMEOUT 120)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_shm test_shm.c)
    target_link_libraries(test_shm PRIVATE rpc)
    add_test(NAME e2e.shm COMMAND test_shm)
    set_tests_properties(e2e.shm PROPERTIES LABELS "e2e;shm" TIMEOUT 120)

    add_executable(test_shmseg test_shmseg.c)
    target_link_libraries(test_shmseg PRIVATE rpc)
    target_include_directories(test_shmseg PRIVATE ${CMAKE_SOURCE_DIR}/src/transport/shm)
    add_test(NAME unit.shmseg COMMAND test_shmseg)
    set_tests_properties(unit.shmseg PROPERTIES LABELS "unit;shm" TIMEOUT 120)

    add_executable(test_shm_mesh test_shm_mesh.c)
    target_link_libraries(test_shm_mesh PRIVATE rpc)
    add_test(NAME e2e.shm_mesh COMMAND test_shm_mesh)
    set_tests_properties(e2e.shm_mesh PROPERTIES LABELS "e2e;shm" TIMEOUT 120)
endif()

if(NB_ENABLE_DDS)
    add_executable(test_dds_loan test_dds_loan.c)
    target_link_libraries(test_dds_loan PRIVATE rpc)
    add_test(NAME e2e.dds_loan COMMAND test_dds_loan)
    set_tests_properties(e2e.dds_loan PROPERTIES LABELS "e2e;dds" TIMEOUT 120)
endif()
