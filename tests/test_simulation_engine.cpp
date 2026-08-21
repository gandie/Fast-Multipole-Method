#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

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

Complex pairwiseForce(const fmm::Source& target, const fmm::Source& source) {
    const double dx = target.position.real() - source.position.real();
    const double dy = target.position.imag() - source.position.imag();
    const double r2 = dx * dx + dy * dy;
    return Complex{-source.q * dx / r2, -source.q * dy / r2};
}

double twoBodyPseudoEnergy(const std::vector<fmm::Source>& sources) {
    if (sources.size() != 2) return 0.0;

    const auto& a = sources[0];
    const auto& b = sources[1];

    const double kinetic = 0.5 * (std::norm(a.velocity) + std::norm(b.velocity));
    const double separation = std::abs(a.position - b.position);
    const double potential = a.q * b.q * std::log(separation);
    return kinetic + potential;
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

TEST_CASE("two-body first step matches analytic update", "[engine][physics]") {
    sim::SimulationOptions options;
    options.rebuild_every = 1000;
    options.cluster_bodies = 0;
    options.uniform_bodies = 2;
    options.orbit_bodies = 0;
    options.black_hole_mass = 0.0;

    sim::SimulationEngine engine(options, 1380);

    std::vector<fmm::Source> initial;
    {
        auto lock = engine.lockSources();
        auto& sources = const_cast<std::vector<fmm::Source>&>(engine.sources());
        REQUIRE(sources.size() == 2);
        sources[0].velocity = Complex{0.0, 0.0};
        sources[1].velocity = Complex{0.0, 0.0};
        initial = sources;
    }

    const double dt = 1e-3;
    const Complex f0 = pairwiseForce(initial[0], initial[1]);
    const Complex f1 = pairwiseForce(initial[1], initial[0]);

    const Complex expected_v0 = initial[0].velocity + f0 * dt;
    const Complex expected_v1 = initial[1].velocity + f1 * dt;

    const Complex expected_x0 = initial[0].position + (initial[0].velocity + 0.5 * f0 * dt) * dt;
    const Complex expected_x1 = initial[1].position + (initial[1].velocity + 0.5 * f1 * dt) * dt;

    engine.step(dt);

    auto lock = engine.lockSources();
    const auto& sources = engine.sources();
    REQUIRE(sources.size() == 2);

    REQUIRE(sources[0].position.real() == Approx(expected_x0.real()).margin(1e-10));
    REQUIRE(sources[0].position.imag() == Approx(expected_x0.imag()).margin(1e-10));
    REQUIRE(sources[1].position.real() == Approx(expected_x1.real()).margin(1e-10));
    REQUIRE(sources[1].position.imag() == Approx(expected_x1.imag()).margin(1e-10));

    REQUIRE(sources[0].velocity.real() == Approx(expected_v0.real()).margin(1e-10));
    REQUIRE(sources[0].velocity.imag() == Approx(expected_v0.imag()).margin(1e-10));
    REQUIRE(sources[1].velocity.real() == Approx(expected_v1.real()).margin(1e-10));
    REQUIRE(sources[1].velocity.imag() == Approx(expected_v1.imag()).margin(1e-10));
}

TEST_CASE("two-body pseudo energy stays bounded over short run", "[engine][physics]") {
    sim::SimulationOptions options;
    options.rebuild_every = 1;
    options.cluster_bodies = 0;
    options.uniform_bodies = 2;
    options.orbit_bodies = 0;
    options.black_hole_mass = 0.0;

    sim::SimulationEngine engine(options, 1380);

    double initial_energy = 0.0;
    {
        auto lock = engine.lockSources();
        auto& sources = const_cast<std::vector<fmm::Source>&>(engine.sources());
        REQUIRE(sources.size() == 2);

        const Complex r_vec = sources[1].position - sources[0].position;
        const double r = std::abs(r_vec);
        REQUIRE(r > 1e-6);

        Complex tangent{-r_vec.imag(), r_vec.real()};
        tangent /= r;

        const double speed = std::sqrt(0.5);
        sources[0].velocity = tangent * speed;
        sources[1].velocity = -tangent * speed;

        initial_energy = twoBodyPseudoEnergy(sources);
    }

    constexpr double dt = 1e-3;
    for (int i = 0; i < 4000; ++i) {
        engine.step(dt);
    }

    auto lock = engine.lockSources();
    const auto& sources = engine.sources();
    REQUIRE(sources.size() == 2);

    const double final_energy = twoBodyPseudoEnergy(sources);
    const double relative_drift = std::abs(final_energy - initial_energy) / std::max(1.0, std::abs(initial_energy));

    REQUIRE(relative_drift < 0.02);
}

TEST_CASE("black hole cannot be removed via interaction radius", "[engine][regression]") {
    sim::SimulationOptions options;
    options.rebuild_every = 1;
    options.cluster_bodies = 0;
    options.uniform_bodies = 0;
    options.orbit_bodies = 0;
    options.black_hole_mass = 100000.0;

    sim::SimulationEngine engine(options, 1380);
    REQUIRE(engine.particleCount() == 1);

    engine.removeParticlesAt(690.0, 690.0);
    REQUIRE(engine.particleCount() == 1);

    engine.step(1e-3);

    auto lock = engine.lockSources();
    const auto* black_hole = findBlackHole(engine.sources(), options.black_hole_mass);
    REQUIRE(black_hole != nullptr);
    REQUIRE(black_hole->position.real() == Approx(690.0));
    REQUIRE(black_hole->position.imag() == Approx(690.0));
}
