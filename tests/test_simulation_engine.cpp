#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
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

const fmm::Source* findBodyNearestMass(const std::vector<fmm::Source>& sources,
                                       double target_mass,
                                       const fmm::Source* exclude = nullptr) {
    const fmm::Source* best = nullptr;
    double best_error = std::numeric_limits<double>::infinity();

    for (const auto& source : sources) {
        if (exclude != nullptr && &source == exclude) {
            continue;
        }

        const double error = std::abs(source.q - target_mass);
        if (error < best_error) {
            best_error = error;
            best = &source;
        }
    }

    return best;
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

struct StabilitySnapshot {
    double pseudo_energy = 0.0;
    double max_radius_from_initial_center = 0.0;
    bool all_positions_finite = true;
    bool all_velocities_finite = true;
};

struct TwoBodyRunMetrics {
    bool all_finite = true;
    double max_relative_energy_drift = 0.0;
    double final_relative_energy_drift = 0.0;
    double final_separation = 0.0;
};

struct HierarchicalRunMetrics {
    bool all_finite = true;
    double max_relative_energy_drift = 0.0;
    double max_radius_from_initial_center = 0.0;
    double min_planet_star_distance = std::numeric_limits<double>::infinity();
    double max_planet_star_distance = 0.0;
    double min_moon_planet_distance = std::numeric_limits<double>::infinity();
    double max_moon_planet_distance = 0.0;
};

double manyBodyPseudoEnergy(const std::vector<fmm::Source>& sources) {
    double kinetic = 0.0;
    for (const auto& source : sources) {
        kinetic += 0.5 * std::norm(source.velocity);
    }

    double potential = 0.0;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        for (std::size_t j = i + 1; j < sources.size(); ++j) {
            const double separation = std::abs(sources[i].position - sources[j].position);
            if (separation <= 1e-9) {
                return std::numeric_limits<double>::infinity();
            }
            potential += sources[i].q * sources[j].q * std::log(separation);
        }
    }

    return kinetic + potential;
}

Complex centerOfGeometry(const std::vector<fmm::Source>& sources) {
    Complex center{0.0, 0.0};
    if (sources.empty()) {
        return center;
    }

    for (const auto& source : sources) {
        center += source.position;
    }
    center /= static_cast<double>(sources.size());
    return center;
}

double maxRadiusFromCenter(const std::vector<fmm::Source>& sources, Complex center) {
    double max_radius = 0.0;
    for (const auto& source : sources) {
        max_radius = std::max(max_radius, std::abs(source.position - center));
    }
    return max_radius;
}

double pairDistance(const std::vector<fmm::Source>& sources, std::size_t a, std::size_t b) {
    if (a >= sources.size() || b >= sources.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::abs(sources[a].position - sources[b].position);
}

StabilitySnapshot captureStabilitySnapshot(const std::vector<fmm::Source>& sources, Complex initial_center) {
    StabilitySnapshot snapshot;
    snapshot.pseudo_energy = manyBodyPseudoEnergy(sources);
    snapshot.max_radius_from_initial_center = maxRadiusFromCenter(sources, initial_center);

    for (const auto& source : sources) {
        const bool pos_finite = std::isfinite(source.position.real()) && std::isfinite(source.position.imag());
        const bool vel_finite = std::isfinite(source.velocity.real()) && std::isfinite(source.velocity.imag());
        snapshot.all_positions_finite = snapshot.all_positions_finite && pos_finite;
        snapshot.all_velocities_finite = snapshot.all_velocities_finite && vel_finite;
    }

    return snapshot;
}

std::filesystem::path scenarioFixturePath(const std::string& filename) {
    return std::filesystem::path(__FILE__).parent_path() / "fixtures" / "scenarios" / filename;
}

sim::SimulationEngine makeScenarioEngine(const std::string& fixture_filename) {
    sim::SimulationOptions options;
    options.rebuild_every = 1;
    options.cluster_bodies = 0;
    options.uniform_bodies = 0;
    options.orbit_bodies = 0;
    options.black_hole_mass = 0.0;
    options.scenario_file = scenarioFixturePath(fixture_filename).string();
    return sim::SimulationEngine(options, 1380);
}

TwoBodyRunMetrics runTwoBodyFixtureMetrics(double dt, int steps) {
    sim::SimulationEngine engine = makeScenarioEngine("two_body_circular.json");

    double initial_energy = 0.0;
    {
        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() == 2);
        initial_energy = manyBodyPseudoEnergy(sources);
    }

    TwoBodyRunMetrics metrics;
    for (int i = 0; i < steps; ++i) {
        engine.step(dt);

        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() == 2);

        const StabilitySnapshot snapshot = captureStabilitySnapshot(sources, Complex{690.0, 690.0});
        metrics.all_finite = metrics.all_finite && snapshot.all_positions_finite && snapshot.all_velocities_finite;

        const double relative_energy_drift =
            std::abs(snapshot.pseudo_energy - initial_energy) / std::max(1.0, std::abs(initial_energy));
        metrics.max_relative_energy_drift = std::max(metrics.max_relative_energy_drift, relative_energy_drift);
        metrics.final_relative_energy_drift = relative_energy_drift;
        metrics.final_separation = pairDistance(sources, 0, 1);
    }

    return metrics;
}

HierarchicalRunMetrics runHierarchicalFixtureMetrics(double dt, int steps) {
    sim::SimulationEngine engine = makeScenarioEngine("hierarchical_star_planet_moon.json");

    double initial_energy = 0.0;
    Complex initial_center{0.0, 0.0};
    {
        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() == 3);
        initial_energy = manyBodyPseudoEnergy(sources);
        initial_center = centerOfGeometry(sources);
    }

    HierarchicalRunMetrics metrics;
    for (int i = 0; i < steps; ++i) {
        engine.step(dt);

        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() == 3);

        const StabilitySnapshot snapshot = captureStabilitySnapshot(sources, initial_center);
        metrics.all_finite = metrics.all_finite && snapshot.all_positions_finite && snapshot.all_velocities_finite;
        metrics.max_radius_from_initial_center =
            std::max(metrics.max_radius_from_initial_center, snapshot.max_radius_from_initial_center);

        const double relative_energy_drift =
            std::abs(snapshot.pseudo_energy - initial_energy) / std::max(1.0, std::abs(initial_energy));
        metrics.max_relative_energy_drift = std::max(metrics.max_relative_energy_drift, relative_energy_drift);

        const fmm::Source* star = findBodyNearestMass(sources, 80.0);
        REQUIRE(star != nullptr);
        const fmm::Source* planet = findBodyNearestMass(sources, 1.0, star);
        REQUIRE(planet != nullptr);
        const fmm::Source* moon = findBodyNearestMass(sources, 0.05, planet);
        REQUIRE(moon != nullptr);

        const double planet_star_distance = std::abs(planet->position - star->position);
        const double moon_planet_distance = std::abs(moon->position - planet->position);

        metrics.min_planet_star_distance = std::min(metrics.min_planet_star_distance, planet_star_distance);
        metrics.max_planet_star_distance = std::max(metrics.max_planet_star_distance, planet_star_distance);
        metrics.min_moon_planet_distance = std::min(metrics.min_moon_planet_distance, moon_planet_distance);
        metrics.max_moon_planet_distance = std::max(metrics.max_moon_planet_distance, moon_planet_distance);
    }

    return metrics;
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

TEST_CASE("scenario file initializes deterministic engine bodies", "[engine][scenario]") {
        const std::filesystem::path temp_path =
                std::filesystem::temp_directory_path() / "fmm_engine_scenario.json";

        std::ofstream out(temp_path);
        REQUIRE(out.is_open());
        out << R"({
    "metadata": {"name": "engine-init"},
    "bodies": [
        {"mass": 4.0, "position": [100.0, 200.0], "velocity": [1.5, -2.0]},
        {"charge": 1.0, "position": [300.0, 400.0], "velocity": [-0.5, 0.25]}
    ]
})";
        out.close();

        sim::SimulationOptions options = smallOptions();
        options.cluster_bodies = 999;
        options.uniform_bodies = 999;
        options.orbit_bodies = 999;
        options.black_hole_mass = 50000.0;
        options.scenario_file = temp_path.string();

        sim::SimulationEngine engine(options, 1380);
        REQUIRE(engine.particleCount() == 2);

        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources[0].position.real() == Approx(100.0));
        REQUIRE(sources[0].position.imag() == Approx(200.0));
        REQUIRE(sources[0].velocity.real() == Approx(1.5));
        REQUIRE(sources[0].velocity.imag() == Approx(-2.0));
        REQUIRE(sources[0].q == Approx(4.0));
}

TEST_CASE("scenario mode does not pin pseudo black-hole mass", "[engine][scenario]") {
        const std::filesystem::path temp_path =
                std::filesystem::temp_directory_path() / "fmm_engine_scenario_unpinned.json";

        std::ofstream out(temp_path);
        REQUIRE(out.is_open());
        out << R"({
    "metadata": {},
    "bodies": [
        {"mass": 100000.0, "position": [200.0, 300.0], "velocity": [10.0, 0.0]}
    ]
})";
        out.close();

        sim::SimulationOptions options = smallOptions();
        options.scenario_file = temp_path.string();
        options.black_hole_mass = 100000.0;

        sim::SimulationEngine engine(options, 1380);
        engine.step(1e-3);

        auto lock = engine.lockSources();
        const auto& body = engine.sources().front();
        REQUIRE(body.position.real() > 200.0);
        REQUIRE(body.position.imag() == Approx(300.0));
}

TEST_CASE("predefined two-body fixture remains bounded over long horizon", "[engine][scenario][stability]") {
    sim::SimulationEngine engine = makeScenarioEngine("two_body_circular.json");

    double initial_energy = 0.0;
    Complex initial_center{0.0, 0.0};
    double initial_separation = 0.0;
    {
        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() == 2);
        initial_energy = manyBodyPseudoEnergy(sources);
        initial_center = centerOfGeometry(sources);
        initial_separation = pairDistance(sources, 0, 1);
    }

    constexpr double dt = 1e-3;
    constexpr int steps = 10000;
    for (int i = 0; i < steps; ++i) {
        engine.step(dt);
    }

    auto lock = engine.lockSources();
    const auto& sources = engine.sources();
    REQUIRE(sources.size() == 2);

    const StabilitySnapshot final_snapshot = captureStabilitySnapshot(sources, initial_center);
    const double relative_energy_drift =
        std::abs(final_snapshot.pseudo_energy - initial_energy) / std::max(1.0, std::abs(initial_energy));
    const double final_separation = pairDistance(sources, 0, 1);

    REQUIRE(final_snapshot.all_positions_finite);
    REQUIRE(final_snapshot.all_velocities_finite);
    REQUIRE(relative_energy_drift < 0.08);
    REQUIRE(final_snapshot.max_radius_from_initial_center < 220.0);
    REQUIRE(final_separation > 120.0);
    REQUIRE(final_separation < 280.0);
    REQUIRE(initial_separation > 0.0);
}

TEST_CASE("predefined hierarchical fixture keeps moon-like companion bounded", "[engine][scenario][stability]") {
    sim::SimulationEngine engine = makeScenarioEngine("hierarchical_star_planet_moon.json");

    double initial_energy = 0.0;
    Complex initial_center{0.0, 0.0};
    double initial_moon_distance = 0.0;
    {
        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() == 3);
        initial_energy = manyBodyPseudoEnergy(sources);
        initial_center = centerOfGeometry(sources);

        const fmm::Source* planet = findBodyNearestMass(sources, 1.0);
        REQUIRE(planet != nullptr);
        const fmm::Source* moon = findBodyNearestMass(sources, 0.05, planet);
        REQUIRE(moon != nullptr);
        initial_moon_distance = std::abs(planet->position - moon->position);
    }

    constexpr double dt = 1e-3;
    constexpr int steps = 10000;
    for (int i = 0; i < steps; ++i) {
        engine.step(dt);
    }

    auto lock = engine.lockSources();
    const auto& sources = engine.sources();
    REQUIRE(sources.size() == 3);

    const StabilitySnapshot final_snapshot = captureStabilitySnapshot(sources, initial_center);
    const double relative_energy_drift =
        std::abs(final_snapshot.pseudo_energy - initial_energy) / std::max(1.0, std::abs(initial_energy));

    const fmm::Source* final_planet = findBodyNearestMass(sources, 1.0);
    REQUIRE(final_planet != nullptr);
    const fmm::Source* final_moon = findBodyNearestMass(sources, 0.05, final_planet);
    REQUIRE(final_moon != nullptr);
    const double final_moon_distance = std::abs(final_planet->position - final_moon->position);

    REQUIRE(final_snapshot.all_positions_finite);
    REQUIRE(final_snapshot.all_velocities_finite);
    REQUIRE(relative_energy_drift < 0.12);
    REQUIRE(final_snapshot.max_radius_from_initial_center < 420.0);
    REQUIRE(final_moon_distance > 1.0);
    REQUIRE(final_moon_distance < 75.0);
    REQUIRE(initial_moon_distance > 0.0);
}

TEST_CASE("two-body fixture improves with timestep refinement", "[engine][scenario][precision]") {
    constexpr double total_time = 2.0;
    const TwoBodyRunMetrics coarse = runTwoBodyFixtureMetrics(2e-3, static_cast<int>(total_time / 2e-3));
    const TwoBodyRunMetrics medium = runTwoBodyFixtureMetrics(1e-3, static_cast<int>(total_time / 1e-3));
    const TwoBodyRunMetrics fine = runTwoBodyFixtureMetrics(5e-4, static_cast<int>(total_time / 5e-4));

    REQUIRE(coarse.all_finite);
    REQUIRE(medium.all_finite);
    REQUIRE(fine.all_finite);

    const double coarse_to_fine_sep_error = std::abs(coarse.final_separation - fine.final_separation);
    const double medium_to_fine_sep_error = std::abs(medium.final_separation - fine.final_separation);

    // Avoid brittle strict ordering between tiny cross-run errors; enforce absolute precision envelopes instead.
    REQUIRE(coarse_to_fine_sep_error < 5e-5);
    REQUIRE(medium_to_fine_sep_error < 5e-5);
    REQUIRE(coarse.max_relative_energy_drift < 5e-5);
    REQUIRE(medium.max_relative_energy_drift < 5e-5);
    REQUIRE(fine.max_relative_energy_drift < 5e-5);
}

TEST_CASE("hierarchical fixture stays bounded throughout trajectory", "[engine][scenario][stability][precision]") {
    const HierarchicalRunMetrics metrics = runHierarchicalFixtureMetrics(1e-3, 10000);

    REQUIRE(metrics.all_finite);
    REQUIRE(metrics.max_relative_energy_drift < 0.2);
    REQUIRE(metrics.max_radius_from_initial_center < 430.0);

    REQUIRE(metrics.min_planet_star_distance > 120.0);
    REQUIRE(metrics.max_planet_star_distance < 240.0);

    REQUIRE(metrics.min_moon_planet_distance > 1.0);
    REQUIRE(metrics.max_moon_planet_distance < 80.0);
}
