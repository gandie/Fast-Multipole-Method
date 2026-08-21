cmake_minimum_required(VERSION 3.28)
project(CMakeSFMLProject LANGUAGES CXX)

include(CTest)
option(ENABLE_COVERAGE "Enable coverage flags for GCC/Clang" OFF)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-O0 -g --coverage)
        add_link_options(--coverage)
    else()
        message(WARNING "ENABLE_COVERAGE is ON but compiler does not support GNU/Clang coverage flags")
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_STANDARD 20)

add_executable(sim
    src/main.cpp
    src/simulation_engine.cpp
)

include(FetchContent)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.1.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(SFML)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(OpenMP REQUIRED)

if(BUILD_TESTING)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Catch2)
    add_subdirectory(tests)
endif()

target_compile_features(sim PRIVATE cxx_std_20)
target_link_libraries(sim PRIVATE sfml-graphics sfml-window sfml-system)
target_link_libraries(sim PRIVATE nlohmann_json::nlohmann_json)

target_link_libraries(sim PRIVATE OpenMP::OpenMP_CXX)

# Keep only if needed on Apple
if(APPLE)
    target_include_directories(sim PRIVATE /opt/homebrew/opt/libomp/include)
    target_link_directories(sim PRIVATE /opt/homebrew/opt/libomp/lib)
endif()

# Better than overwriting global CMAKE_CXX_FLAGS_RELEASE
target_compile_options(sim PRIVATE
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# Optional: enable IPO/LTO in Release
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sim)
