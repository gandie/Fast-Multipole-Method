#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>

#include "simulation_engine.hpp"

using Catch::Approx;

namespace {

sim::SimulationOptions smallOptions() {
    sim::SimulationOptions options;
    options.rebuild_every = 2;
    options.cluster_bodies = 6;
    options.uniform_bodies = 4;
    options.orbit_bodies = 0;
    options.black_hole_mass = 0.0;
    return options;
}

const fmm::Source* findBlackHole(const std::vector<fmm::Source>& sources, double mass) {
    auto it = std::find_if(sources.begin(), sources.end(), [mass](const fmm::Source& source) {
        return source.q >= mass * 0.9;
    });
    return (it == sources.end()) ? nullptr : &(*it);
}

}  // namespace

TEST_CASE("engine initializes sources and tree geometry", "[engine][regression]") {
    sim::SimulationOptions options = smallOptions();
    sim::SimulationEngine engine(options, 1380);

    REQUIRE(engine.rebuildEvery() == 2);
    REQUIRE(engine.particleCount() == 10);

    const auto geometries = engine.boxGeometries();
    REQUIRE_FALSE(geometries.empty());

    const auto& sources = engine.sources();
    REQUIRE(sources.size() == 10);
}

TEST_CASE("interaction radius is clamped to supported range", "[engine][regression]") {
    sim::SimulationEngine engine(smallOptions(), 1380);

    REQUIRE(engine.interactionRadius() == Approx(50.0));

    engine.increaseInteractionRadius(10000.0);
    REQUIRE(engine.interactionRadius() == Approx(300.0));

    engine.decreaseInteractionRadius(10000.0);
    REQUIRE(engine.interactionRadius() == Approx(10.0));
}

TEST_CASE("particle add and remove APIs update particle count", "[engine][regression]") {
    sim::SimulationEngine engine(smallOptions(), 1380);

    const std::size_t before = engine.particleCount();

    engine.addParticlesAt(0.0, 0.0, 25);
    REQUIRE(engine.particleCount() == before + 25);

    engine.removeParticlesAt(100.0, 100.0);
    REQUIRE(engine.particleCount() == before);

    engine.addParticlesAt(100.0, 100.0, 0);
    REQUIRE(engine.particleCount() == before);

    engine.removeParticlesAt(1300.0, 1300.0);
    REQUIRE(engine.particleCount() == before);
}

TEST_CASE("step updates frame stats and rebuild cadence path", "[engine][regression]") {
    sim::SimulationEngine engine(smallOptions(), 1380);

    engine.step(0.001);
    const sim::EngineFrameStats first = engine.frameStats();

    REQUIRE(first.frame_ms >= 0.0f);
    REQUIRE(first.integrate_ms >= 0.0f);
    REQUIRE(first.phase3_ms >= 0.0f);
    REQUIRE(first.ema_build_ms >= 0.0f);

    engine.step(0.001);
    const sim::EngineFrameStats second = engine.frameStats();
    REQUIRE(second.max_build_ms >= first.max_build_ms);
}

TEST_CASE("black hole is pinned to center each step", "[engine][regression]") {
    sim::SimulationOptions options = smallOptions();
    options.black_hole_mass = 100000.0;

    sim::SimulationEngine engine(options, 1380);

    engine.step(0.001);

    auto lock = engine.lockSources();
    const fmm::Source* black_hole = findBlackHole(engine.sources(), options.black_hole_mass);
    REQUIRE(black_hole != nullptr);
    REQUIRE(black_hole->position.real() == Approx(690.0));
    REQUIRE(black_hole->position.imag() == Approx(690.0));
    REQUIRE(std::abs(black_hole->velocity.real()) < 1e-12);
    REQUIRE(std::abs(black_hole->velocity.imag()) < 1e-12);
}

TEST_CASE("orbit bodies are initialized when black hole is enabled", "[engine][regression]") {
    sim::SimulationOptions options = smallOptions();
    options.black_hole_mass = 40000.0;
    options.orbit_bodies = 3;

    sim::SimulationEngine engine(options, 1380);

    REQUIRE(engine.particleCount() == 14);

    auto lock = engine.lockSources();
    const auto& sources = engine.sources();

    int moving_orbit_candidates = 0;
    for (const auto& source : sources) {
        if (source.q <= 1.0) {
            const double speed = std::abs(source.velocity);
            if (speed > 0.0) {
                ++moving_orbit_candidates;
            }
        }
    }

    REQUIRE(moving_orbit_candidates >= 3);
}
