set(BAREMETAL_TESTS_SRCS
    test_architecture_tlbs.cpp
    test_keraunos_address_map.cpp
    test_att_window.cpp
    test_att_resolver.cpp
    test_grendel_att_map.cpp
    test_cluster_descriptor_offline.cpp
    test_cluster_descriptor_cluster_id.cpp
    test_core_coord_translation_wh.cpp
    test_core_coord_translation_bh.cpp
    test_core_coord_translation_quasar.cpp
    test_soc_arch_descriptor.cpp
    test_soc_descriptor.cpp
    test_long_jump.cpp
    test_notification_mechanism.cpp
    test_cluster.cpp
    test_blackhole_arc_apb.cpp
    test_wormhole_arc_window.cpp
    test_blackhole_arc_message_queue.cpp
    # SimulationServerSocket and SimulationClient have no simulation deps (always compiled), so
    # their tests need no card and no simulation build -- they run in the no-card baremetal CI lane.
    test_simulation_server_socket.cpp
    test_simulation_client.cpp
    test_simulation_server_transport.cpp
)

add_executable(baremetal_tests ${BAREMETAL_TESTS_SRCS})
target_link_libraries(baremetal_tests PRIVATE test_common)

# yaml-cpp is private to tt-umd, so its headers do not reach here through test_common, and the
# ATT map fidelity test parses the vendored tables itself. Take the include path alone: linking
# the library would place a second copy of it in this binary beside the one already inside
# libtt-umd.so, which ASan reports as an ODR violation on YAML::Exception's typeinfo.
target_include_directories(baremetal_tests PRIVATE $<TARGET_PROPERTY:yaml-cpp::yaml-cpp,INTERFACE_INCLUDE_DIRECTORIES>)
if(TT_UMD_BUILD_EMULE)
    target_sources(baremetal_tests PRIVATE test_sw_emule_chip.cpp)
endif()
if(TT_UMD_BUILD_SIMULATION)
    target_sources(baremetal_tests PRIVATE test_simulation_tlb_allocator.cpp)
endif()
set_target_properties(
    baremetal_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/baremetal
        OUTPUT_NAME
            baremetal_tests
)
