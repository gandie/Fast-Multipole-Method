#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "force_swap.hpp"
#include "sim_options.hpp"

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
