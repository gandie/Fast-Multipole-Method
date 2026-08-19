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
