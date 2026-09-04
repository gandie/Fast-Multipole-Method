add_executable(unit_tests
    test_math_utils.cpp
    test_expansions.cpp
    test_quadtree_invariants.cpp
    test_barnes_hut_accuracy.cpp
    test_fmm_accuracy.cpp
    test_regressions.cpp
    test_simulation_engine.cpp
    ../src/simulation_engine.cpp
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(unit_tests PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
    OpenMP::OpenMP_CXX
)

include(Catch)
catch_discover_tests(unit_tests)
