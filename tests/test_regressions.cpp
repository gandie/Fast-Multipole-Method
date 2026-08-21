#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include "force_swap.hpp"
#include "sim_options.hpp"
#include "simulation_engine.hpp"
#include "spawn_utils.hpp"

TEST_CASE("orbit bodies require positive black hole mass", "[regression][cli]") {
    const std::vector<std::string> args{"--orbit", "10"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: orbit bodies require --black-hole with positive mass");
}

TEST_CASE("help flag short-circuits parsing", "[regression][cli]") {
    const std::vector<std::string> args{"--help", "--orbit", "10"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE(result.ok);
    REQUIRE(result.options.help_requested);
}

TEST_CASE("missing value for option is rejected", "[regression][cli]") {
    const std::vector<std::string> args{"--orbit"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: argument requires a value: --orbit");
}

TEST_CASE("invalid integer values are rejected", "[regression][cli]") {
    const std::vector<std::string> args{"--uniform-bodies", "12abc"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: invalid integer value: 12abc");
}

TEST_CASE("unknown arguments are rejected", "[regression][cli]") {
    const std::vector<std::string> args{"--bogus-flag"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: unknown argument: --bogus-flag");
}

TEST_CASE("rebuild-every lower bound is enforced", "[regression][cli]") {
    const std::vector<std::string> args{"--rebuild-every", "0"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: rebuild-every must be >= 1");
}

TEST_CASE("zero total bodies is rejected", "[regression][cli]") {
    const std::vector<std::string> args{
        "--cluster-bodies", "0",
        "--uniform-bodies", "0",
        "--orbit", "0"
    };
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: total body count must be > 0");
}

TEST_CASE("negative black hole mass is rejected", "[regression][cli]") {
    const std::vector<std::string> args{"--black-hole", "-5"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: black hole mass cannot be negative");
}

TEST_CASE("valid mixed options parse successfully", "[regression][cli]") {
    const std::vector<std::string> args{
        "--rebuild-every", "3",
        "--cluster-bodies", "10",
        "--uniform-bodies", "5",
        "--black-hole", "100",
        "--orbit", "2"
    };
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE(result.ok);
    REQUIRE_FALSE(result.options.help_requested);
    REQUIRE(result.options.rebuild_every == 3);
    REQUIRE(result.options.cluster_bodies == 10);
    REQUIRE(result.options.uniform_bodies == 5);
    REQUIRE(result.options.orbit_bodies == 2);
    REQUIRE(result.options.black_hole_mass == 100.0);
}

TEST_CASE("negative body counts are rejected", "[regression][cli]") {
    const std::vector<std::string> args{"--cluster-bodies", "-1"};
    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message == "Error: body counts cannot be negative");
}

TEST_CASE("scenario option parses and overrides generation validation", "[regression][cli]") {
    const std::vector<std::string> args{
        "--scenario", "fixtures/scenario.json",
        "--cluster-bodies", "0",
        "--uniform-bodies", "0",
        "--orbit", "10"
    };

    const sim::ParseResult result = sim::parseSimulationArgs(args);

    REQUIRE(result.ok);
    REQUIRE(result.options.scenario_file == "fixtures/scenario.json");
}

TEST_CASE("scenario loader accepts mass and charge aliases", "[regression][scenario]") {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "fmm_scenario_valid.json";

    std::ofstream out(temp_path);
    REQUIRE(out.is_open());
    out << R"({
  "metadata": {"name": "alias-check"},
  "bodies": [
    {"mass": 3.0, "charge": 5.0, "position": [10.0, 20.0], "velocity": [1.0, 2.0]},
    {"charge": 2.5, "position": [30.0, 40.0], "velocity": [0.0, -1.0]}
  ]
})";
    out.close();

    const sim::ScenarioLoadResult result = sim::loadScenarioFromFile(temp_path.string());
    REQUIRE(result.ok);
    REQUIRE(result.sources.size() == 2);

    // Mass takes precedence when both fields are present.
    REQUIRE(result.sources[0].q == 3.0);
    REQUIRE(result.sources[1].q == 2.5);
}

TEST_CASE("scenario loader rejects incompatible shape", "[regression][scenario]") {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "fmm_scenario_invalid.json";

    std::ofstream out(temp_path);
    REQUIRE(out.is_open());
    out << R"({"metadata": {}, "bodies": [{"mass": 1.0, "position": [1.0]}]})";
    out.close();

    const sim::ScenarioLoadResult result = sim::loadScenarioFromFile(temp_path.string());
    REQUIRE_FALSE(result.ok);
    const bool mentions_missing_required_fields =
        result.error_message.find("requires 'position' and 'velocity'") != std::string::npos;
    const bool mentions_bad_vector_shape =
        result.error_message.find("must be an array of 2 numbers") != std::string::npos;
    const bool has_expected_error = mentions_missing_required_fields || mentions_bad_vector_shape;
    REQUIRE(has_expected_error);
}

TEST_CASE("force swap ignores and clears stale pending data", "[regression][async]") {
    std::vector<Complex> current_forces(5, Complex{1.0, 1.0});
    std::vector<Complex> pending_forces(3, Complex{9.0, 9.0});

    const bool swapped = sim::trySwapPendingForces(current_forces, pending_forces);

    REQUIRE_FALSE(swapped);
    REQUIRE(pending_forces.empty());
    REQUIRE(current_forces.size() == 5);
}

TEST_CASE("force swap succeeds when vector sizes match", "[regression][async]") {
    std::vector<Complex> current_forces(2, Complex{1.0, 1.0});
    std::vector<Complex> pending_forces{Complex{4.0, 5.0}, Complex{6.0, 7.0}};

    const bool swapped = sim::trySwapPendingForces(current_forces, pending_forces);

    REQUIRE(swapped);
    REQUIRE(pending_forces.empty());
    REQUIRE(current_forces[0] == Complex{4.0, 5.0});
    REQUIRE(current_forces[1] == Complex{6.0, 7.0});
}

TEST_CASE("border-adjacent spawn points stay within simulation bounds", "[regression][spawn]") {
    std::mt19937 gen(42);
    constexpr int screen_size = 1380;
    constexpr double radius = 80.0;

    for (int i = 0; i < 500; ++i) {
        const auto [x, y] = sim::sampleSpawnPointWithinBounds(20.0, 15.0, radius, screen_size, gen);
        REQUIRE(x >= 100.0);
        REQUIRE(x <= static_cast<double>(screen_size - 100));
        REQUIRE(y >= 100.0);
        REQUIRE(y <= static_cast<double>(screen_size - 100));
    }
}

TEST_CASE("corner clicks do not collapse spawn to one point", "[regression][spawn]") {
    std::mt19937 gen(7);
    constexpr int screen_size = 1380;
    constexpr double radius = 50.0;

    double min_x = 1e9;
    double max_x = -1e9;
    double min_y = 1e9;
    double max_y = -1e9;

    for (int i = 0; i < 400; ++i) {
        const auto [x, y] = sim::sampleSpawnPointWithinBounds(0.0, 0.0, radius, screen_size, gen);
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }

    REQUIRE((max_x - min_x) > 1.0);
    REQUIRE((max_y - min_y) > 1.0);
}
