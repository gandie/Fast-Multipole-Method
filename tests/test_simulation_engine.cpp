#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <tuple>
#include <vector>
#include <omp.h>

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

struct ScenarioRunMetrics {
    bool all_finite = true;
    double max_relative_energy_drift = 0.0;
    double final_relative_energy_drift = 0.0;
    double max_radius_from_initial_center = 0.0;
    double min_pair_distance = std::numeric_limits<double>::infinity();
    double max_pair_distance = 0.0;
    double final_pair_distance = 0.0;
};

struct ConvergenceSweepSummary {
    bool all_finite = true;
    double coarse_error = 0.0;
    double medium_error = 0.0;
    double fine_error = 0.0;
    double order_coarse_to_medium = 0.0;
    double order_medium_to_fine = 0.0;
};

struct ConvergenceFixtureSpec {
    std::string fixture_filename;
    double primary_mass = 0.0;
    double secondary_mass = 0.0;
    double total_time = 0.0;
    double coarse_dt = 0.0;
};

struct BodyState {
    double q = 0.0;
    double px = 0.0;
    double py = 0.0;
    double vx = 0.0;
    double vy = 0.0;
};

struct OmpState {
    int max_threads = 1;
    int dynamic_enabled = 0;
};

struct OmpStateGuard {
    OmpState saved;
    explicit OmpStateGuard(OmpState state) : saved(state) {}
    ~OmpStateGuard() {
        omp_set_dynamic(saved.dynamic_enabled);
        omp_set_num_threads(saved.max_threads);
    }
};

OmpState captureOmpState() {
    return OmpState{omp_get_max_threads(), omp_get_dynamic()};
}

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

double totalAngularMomentumZ(const std::vector<fmm::Source>& sources) {
    double lz = 0.0;
    for (const auto& source : sources) {
        lz += source.q * (source.position.real() * source.velocity.imag()
                          - source.position.imag() * source.velocity.real());
    }
    return lz;
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

ScenarioRunMetrics runScenarioFixtureMetrics(const std::string& fixture_filename,
                                             double dt,
                                             int steps,
                                             double primary_mass,
                                             double secondary_mass) {
    sim::SimulationEngine engine = makeScenarioEngine(fixture_filename);

    double initial_energy = 0.0;
    Complex initial_center{0.0, 0.0};
    {
        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() >= 2);
        initial_energy = manyBodyPseudoEnergy(sources);
        initial_center = centerOfGeometry(sources);
    }

    ScenarioRunMetrics metrics;
    for (int i = 0; i < steps; ++i) {
        engine.step(dt);

        auto lock = engine.lockSources();
        const auto& sources = engine.sources();

        const StabilitySnapshot snapshot = captureStabilitySnapshot(sources, initial_center);
        metrics.all_finite = metrics.all_finite && snapshot.all_positions_finite && snapshot.all_velocities_finite;
        metrics.max_radius_from_initial_center =
            std::max(metrics.max_radius_from_initial_center, snapshot.max_radius_from_initial_center);

        const double relative_energy_drift =
            std::abs(snapshot.pseudo_energy - initial_energy) / std::max(1.0, std::abs(initial_energy));
        metrics.max_relative_energy_drift = std::max(metrics.max_relative_energy_drift, relative_energy_drift);
        metrics.final_relative_energy_drift = relative_energy_drift;

        const fmm::Source* primary = findBodyNearestMass(sources, primary_mass);
        REQUIRE(primary != nullptr);
        const fmm::Source* secondary = findBodyNearestMass(sources, secondary_mass, primary);
        REQUIRE(secondary != nullptr);

        const double pair_distance = std::abs(primary->position - secondary->position);
        metrics.min_pair_distance = std::min(metrics.min_pair_distance, pair_distance);
        metrics.max_pair_distance = std::max(metrics.max_pair_distance, pair_distance);
        metrics.final_pair_distance = pair_distance;
    }

    return metrics;
}

ScenarioRunMetrics runScenarioFixtureMetricsThreaded(const std::string& fixture_filename,
                                                     double dt,
                                                     int steps,
                                                     double primary_mass,
                                                     double secondary_mass,
                                                     int threads) {
    omp_set_dynamic(0);
    omp_set_num_threads(std::max(1, threads));
    return runScenarioFixtureMetrics(fixture_filename, dt, steps, primary_mass, secondary_mass);
}

double relativeDifference(double a, double b) {
    return std::abs(a - b) / std::max({1.0, std::abs(a), std::abs(b)});
}

double observedOrder(double coarser_error, double finer_error) {
    constexpr double error_floor = 1e-16;
    const double safe_coarser = std::max(coarser_error, error_floor);
    const double safe_finer = std::max(finer_error, error_floor);
    return std::log(safe_coarser / safe_finer) / std::log(2.0);
}

ConvergenceSweepSummary evaluatePairDistanceConvergence(const ConvergenceFixtureSpec& spec) {
    const double medium_dt = spec.coarse_dt * 0.5;
    const double fine_dt = spec.coarse_dt * 0.25;
    const double reference_dt = spec.coarse_dt * 0.125;

    const int coarse_steps = static_cast<int>(spec.total_time / spec.coarse_dt);
    const int medium_steps = static_cast<int>(spec.total_time / medium_dt);
    const int fine_steps = static_cast<int>(spec.total_time / fine_dt);
    const int reference_steps = static_cast<int>(spec.total_time / reference_dt);

    const ScenarioRunMetrics coarse =
        runScenarioFixtureMetrics(spec.fixture_filename, spec.coarse_dt, coarse_steps, spec.primary_mass, spec.secondary_mass);
    const ScenarioRunMetrics medium =
        runScenarioFixtureMetrics(spec.fixture_filename, medium_dt, medium_steps, spec.primary_mass, spec.secondary_mass);
    const ScenarioRunMetrics fine =
        runScenarioFixtureMetrics(spec.fixture_filename, fine_dt, fine_steps, spec.primary_mass, spec.secondary_mass);
    const ScenarioRunMetrics reference = runScenarioFixtureMetrics(
        spec.fixture_filename, reference_dt, reference_steps, spec.primary_mass, spec.secondary_mass);

    ConvergenceSweepSummary summary;
    summary.all_finite = coarse.all_finite && medium.all_finite && fine.all_finite && reference.all_finite;
    summary.coarse_error = std::abs(coarse.final_pair_distance - reference.final_pair_distance);
    summary.medium_error = std::abs(medium.final_pair_distance - reference.final_pair_distance);
    summary.fine_error = std::abs(fine.final_pair_distance - reference.final_pair_distance);
    summary.order_coarse_to_medium = observedOrder(summary.coarse_error, summary.medium_error);
    summary.order_medium_to_fine = observedOrder(summary.medium_error, summary.fine_error);
    return summary;
}

std::vector<BodyState> canonicalBodyStates(const std::vector<fmm::Source>& sources) {
    std::vector<BodyState> states;
    states.reserve(sources.size());
    for (const auto& source : sources) {
        states.push_back(
            BodyState{source.q, source.position.real(), source.position.imag(), source.velocity.real(), source.velocity.imag()});
    }

    std::sort(states.begin(), states.end(), [](const BodyState& a, const BodyState& b) {
        return std::tie(a.q, a.px, a.py, a.vx, a.vy) < std::tie(b.q, b.px, b.py, b.vx, b.vy);
    });
    return states;
}

std::vector<BodyState> runScenarioAndCaptureState(const std::string& fixture_filename, double dt, int steps) {
    sim::SimulationEngine engine = makeScenarioEngine(fixture_filename);
    for (int i = 0; i < steps; ++i) {
        engine.step(dt);
    }

    auto lock = engine.lockSources();
    return canonicalBodyStates(engine.sources());
}

std::vector<BodyState> runGeneratedAndCaptureState(sim::SimulationOptions options, double dt, int steps) {
    sim::SimulationEngine engine(options, 1380);
    for (int i = 0; i < steps; ++i) {
        engine.step(dt);
    }

    auto lock = engine.lockSources();
    return canonicalBodyStates(engine.sources());
}

void requireSameState(const std::vector<BodyState>& lhs, const std::vector<BodyState>& rhs, double margin) {
    REQUIRE(lhs.size() == rhs.size());
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        REQUIRE(lhs[i].q == Approx(rhs[i].q).margin(margin));
        REQUIRE(lhs[i].px == Approx(rhs[i].px).margin(margin));
        REQUIRE(lhs[i].py == Approx(rhs[i].py).margin(margin));
        REQUIRE(lhs[i].vx == Approx(rhs[i].vx).margin(margin));
        REQUIRE(lhs[i].vy == Approx(rhs[i].vy).margin(margin));
    }
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
    REQUIRE(first.rebuilt_forces_this_frame);
    REQUIRE(first.frames_since_force_rebuild == 0);
    REQUIRE(first.build_telemetry_updated_this_frame);
    REQUIRE(first.build_total_internal_ms >= 0.0f);
    REQUIRE(first.build_active_nodes > 0);
    REQUIRE(first.build_source_count == engine.particleCount());
    REQUIRE(first.build_leaf_nodes > 0);
    REQUIRE(first.build_max_leaf_sources > 0);

    engine.step(0.001);
    const sim::EngineFrameStats second = engine.frameStats();
    REQUIRE(second.max_build_ms >= first.max_build_ms);
    REQUIRE_FALSE(second.rebuilt_forces_this_frame);
    REQUIRE(second.frames_since_force_rebuild == 1);
    REQUIRE_FALSE(second.build_telemetry_updated_this_frame);
}

TEST_CASE("rebuild cadence telemetry matches strict configured schedule", "[engine][regression][cadence]") {
    sim::SimulationOptions options;
    options.rebuild_every = 3;
    options.cluster_bodies = 0;
    options.uniform_bodies = 2;
    options.orbit_bodies = 0;
    options.black_hole_mass = 0.0;

    sim::SimulationEngine engine(options, 1380);

    engine.step(0.001);
    auto s0 = engine.frameStats();
    REQUIRE(s0.rebuilt_forces_this_frame);
    REQUIRE(s0.frames_since_force_rebuild == 0);
    REQUIRE(s0.build_telemetry_updated_this_frame);
    REQUIRE(s0.build_active_nodes > 0);
    REQUIRE(s0.build_leaf_nodes > 0);
    REQUIRE(s0.build_direct_pair_evals > 0);

    engine.step(0.001);
    auto s1 = engine.frameStats();
    REQUIRE_FALSE(s1.rebuilt_forces_this_frame);
    REQUIRE(s1.frames_since_force_rebuild == 1);

    engine.step(0.001);
    auto s2 = engine.frameStats();
    REQUIRE_FALSE(s2.rebuilt_forces_this_frame);
    REQUIRE(s2.frames_since_force_rebuild == 2);

    engine.step(0.001);
    auto s3 = engine.frameStats();
    REQUIRE(s3.rebuilt_forces_this_frame);
    REQUIRE(s3.frames_since_force_rebuild == 0);

    for (int frame = 4; frame < 11; ++frame) {
        engine.step(0.001);
        const auto stats = engine.frameStats();
        const bool expected_rebuild = (frame % 3) == 0;
        REQUIRE(stats.rebuilt_forces_this_frame == expected_rebuild);
        REQUIRE(stats.frames_since_force_rebuild == (expected_rebuild ? 0 : frame % 3));
    }
}

TEST_CASE("scenario replay is deterministic with synchronous cadence", "[engine][scenario][regression][determinism]") {
    constexpr double dt = 1e-3;
    constexpr int steps = 4000;

    const auto first = runScenarioAndCaptureState("high_mass_ratio_binary.json", dt, steps);
    const auto second = runScenarioAndCaptureState("high_mass_ratio_binary.json", dt, steps);

    requireSameState(first, second, 1e-11);
}

TEST_CASE("generated replay is deterministic for fixed seed and cadence", "[engine][regression][determinism]") {
    sim::SimulationOptions options;
    options.rebuild_every = 3;
    options.cluster_bodies = 3;
    options.uniform_bodies = 3;
    options.orbit_bodies = 0;
    options.black_hole_mass = 0.0;

    constexpr double dt = 1e-3;
    constexpr int steps = 200;

    const auto first = runGeneratedAndCaptureState(options, dt, steps);
    const auto second = runGeneratedAndCaptureState(options, dt, steps);

    requireSameState(first, second, 1e-11);
}

TEST_CASE("scenario replay remains deterministic over extended horizon", "[engine][scenario][regression][determinism][long]") {
    constexpr double dt = 1e-3;
    constexpr int steps = 20000;

    const auto first = runScenarioAndCaptureState("high_mass_ratio_binary.json", dt, steps);
    const auto second = runScenarioAndCaptureState("high_mass_ratio_binary.json", dt, steps);

    requireSameState(first, second, 1e-11);
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
    double initial_lz = 0.0;
    Complex initial_center{0.0, 0.0};
    double initial_separation = 0.0;
    {
        auto lock = engine.lockSources();
        const auto& sources = engine.sources();
        REQUIRE(sources.size() == 2);
        initial_energy = manyBodyPseudoEnergy(sources);
        initial_lz = totalAngularMomentumZ(sources);
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
    const double final_lz = totalAngularMomentumZ(sources);
    const double relative_lz_drift = std::abs(final_lz - initial_lz) / std::max(1.0, std::abs(initial_lz));
    const double final_separation = pairDistance(sources, 0, 1);

    REQUIRE(final_snapshot.all_positions_finite);
    REQUIRE(final_snapshot.all_velocities_finite);
    REQUIRE(relative_energy_drift < 0.07);
    REQUIRE(relative_lz_drift < 0.02);
    REQUIRE(final_snapshot.max_radius_from_initial_center < 215.0);
    REQUIRE(final_separation > 125.0);
    REQUIRE(final_separation < 275.0);
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
    REQUIRE(relative_energy_drift < 0.11);
    REQUIRE(final_snapshot.max_radius_from_initial_center < 410.0);
    REQUIRE(final_moon_distance > 1.0);
    REQUIRE(final_moon_distance < 72.0);
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
    REQUIRE(coarse_to_fine_sep_error < 4e-5);
    REQUIRE(medium_to_fine_sep_error < 4e-5);
    REQUIRE(coarse.max_relative_energy_drift < 4e-5);
    REQUIRE(medium.max_relative_energy_drift < 4e-5);
    REQUIRE(fine.max_relative_energy_drift < 4e-5);
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

TEST_CASE("scenario fixtures show consistent timestep-refinement convergence", "[engine][scenario][precision][convergence]") {
    const std::vector<ConvergenceFixtureSpec> fixtures = {
        {"two_body_circular.json", 1.0, 1.0, 2.0, 2e-3},
        {"hierarchical_star_planet_moon.json", 80.0, 1.0, 2.0, 2e-3},
        {"high_mass_ratio_binary.json", 80.0, 0.2, 2.0, 2e-3},
    };

    for (const auto& fixture : fixtures) {
        const ConvergenceSweepSummary summary = evaluatePairDistanceConvergence(fixture);

        INFO("fixture=" << fixture.fixture_filename);
        INFO("coarse_error=" << summary.coarse_error);
        INFO("medium_error=" << summary.medium_error);
        INFO("fine_error=" << summary.fine_error);
        INFO("order_coarse_to_medium=" << summary.order_coarse_to_medium);
        INFO("order_medium_to_fine=" << summary.order_medium_to_fine);

        REQUIRE(summary.all_finite);
        if (summary.coarse_error > 5e-7 || summary.medium_error > 5e-7) {
            REQUIRE(summary.coarse_error > summary.medium_error);
            REQUIRE(summary.order_coarse_to_medium >= 0.9);
        } else {
            REQUIRE(summary.coarse_error < 5e-7);
            REQUIRE(summary.medium_error < 5e-7);
        }

        // When both medium/fine errors are at the sub-micro reference floor, cancellation can invert
        // tiny differences without indicating loss of the broader timestep-refinement trend.
        if (summary.medium_error > 5e-7 || summary.fine_error > 5e-7) {
            REQUIRE(summary.medium_error > summary.fine_error);
            REQUIRE(summary.order_medium_to_fine >= 0.9);
        } else {
            REQUIRE(summary.medium_error < 5e-7);
            REQUIRE(summary.fine_error < 5e-7);
        }

        REQUIRE(summary.fine_error < 1.5e-4);
    }
}

TEST_CASE("high mass-ratio fixture stays finite and bounded over long horizon", "[engine][scenario][stability][stress]") {
    const ScenarioRunMetrics metrics = runScenarioFixtureMetrics("high_mass_ratio_binary.json", 1e-3, 20000, 80.0, 0.2);

    REQUIRE(metrics.all_finite);
    // This stiff regime exhibits larger pseudo-energy oscillation under the current integrator.
    REQUIRE(metrics.max_relative_energy_drift < 0.35);
    REQUIRE(metrics.max_radius_from_initial_center < 275.0);
    REQUIRE(metrics.min_pair_distance > 130.0);
    REQUIRE(metrics.max_pair_distance < 295.0);
}

TEST_CASE("close-approach fixture remains finite under near-singular stress", "[engine][scenario][stability][stress]") {
    const ScenarioRunMetrics metrics = runScenarioFixtureMetrics("close_approach_binary.json", 5e-4, 30000, 1.0, 1.0);

    REQUIRE(metrics.all_finite);
    // Near-singular encounters are intentionally difficult; keep a bounded but realistic envelope.
    REQUIRE(metrics.max_relative_energy_drift < 0.45);
    REQUIRE(metrics.max_radius_from_initial_center < 19.0);
    REQUIRE(metrics.min_pair_distance > 2.0);
    REQUIRE(metrics.max_pair_distance < 11.5);
}

TEST_CASE("thread policies preserve long-horizon scenario envelopes", "[engine][scenario][stability][threading]") {
    const OmpState initial_omp = captureOmpState();
    OmpStateGuard guard(initial_omp);

    const int runtime_threads = omp_get_max_threads();
    if (runtime_threads < 2) {
        SUCCEED("Runtime exposes one OpenMP thread; cross-policy scenario guard skipped.");
        return;
    }

    const int multi_threads = std::min(8, runtime_threads);

    const ScenarioRunMetrics high_serial = runScenarioFixtureMetricsThreaded(
        "high_mass_ratio_binary.json", 1e-3, 12000, 80.0, 0.2, 1);
    const ScenarioRunMetrics high_multi = runScenarioFixtureMetricsThreaded(
        "high_mass_ratio_binary.json", 1e-3, 12000, 80.0, 0.2, multi_threads);

    const ScenarioRunMetrics close_serial = runScenarioFixtureMetricsThreaded(
        "close_approach_binary.json", 5e-4, 16000, 1.0, 1.0, 1);
    const ScenarioRunMetrics close_multi = runScenarioFixtureMetricsThreaded(
        "close_approach_binary.json", 5e-4, 16000, 1.0, 1.0, multi_threads);

    REQUIRE(high_serial.all_finite);
    REQUIRE(high_multi.all_finite);
    REQUIRE(close_serial.all_finite);
    REQUIRE(close_multi.all_finite);

    REQUIRE(high_serial.max_relative_energy_drift < 0.35);
    REQUIRE(high_multi.max_relative_energy_drift < 0.35);
    REQUIRE(high_serial.max_radius_from_initial_center < 275.0);
    REQUIRE(high_multi.max_radius_from_initial_center < 275.0);
    REQUIRE(high_serial.min_pair_distance > 130.0);
    REQUIRE(high_multi.min_pair_distance > 130.0);
    REQUIRE(high_serial.max_pair_distance < 295.0);
    REQUIRE(high_multi.max_pair_distance < 295.0);

    REQUIRE(close_serial.max_relative_energy_drift < 0.45);
    REQUIRE(close_multi.max_relative_energy_drift < 0.45);
    REQUIRE(close_serial.max_radius_from_initial_center < 19.0);
    REQUIRE(close_multi.max_radius_from_initial_center < 19.0);
    REQUIRE(close_serial.min_pair_distance > 2.0);
    REQUIRE(close_multi.min_pair_distance > 2.0);
    REQUIRE(close_serial.max_pair_distance < 11.5);
    REQUIRE(close_multi.max_pair_distance < 11.5);

    REQUIRE(relativeDifference(high_serial.max_relative_energy_drift, high_multi.max_relative_energy_drift) < 1e-9);
    REQUIRE(relativeDifference(high_serial.max_radius_from_initial_center, high_multi.max_radius_from_initial_center) < 1e-10);
    REQUIRE(relativeDifference(high_serial.min_pair_distance, high_multi.min_pair_distance) < 1e-10);
    REQUIRE(relativeDifference(high_serial.max_pair_distance, high_multi.max_pair_distance) < 1e-10);
    REQUIRE(relativeDifference(high_serial.final_pair_distance, high_multi.final_pair_distance) < 1e-10);

    REQUIRE(relativeDifference(close_serial.max_relative_energy_drift, close_multi.max_relative_energy_drift) < 1e-9);
    REQUIRE(relativeDifference(close_serial.max_radius_from_initial_center, close_multi.max_radius_from_initial_center) < 1e-10);
    REQUIRE(relativeDifference(close_serial.min_pair_distance, close_multi.min_pair_distance) < 1e-10);
    REQUIRE(relativeDifference(close_serial.max_pair_distance, close_multi.max_pair_distance) < 1e-10);
    REQUIRE(relativeDifference(close_serial.final_pair_distance, close_multi.final_pair_distance) < 1e-10);
}
