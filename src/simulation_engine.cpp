#include "simulation_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <omp.h>

#include "spawn_utils.hpp"

namespace sim {
namespace {
using nlohmann::json;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadiusStepMin = 10.0;
constexpr double kRadiusStepMax = 300.0;
constexpr float kEmaAlpha = 0.10f;

bool parseFiniteNumber(const json& value, const char* field_name, double& out, std::string& error_message) {
    if (!value.is_number()) {
        error_message = std::string("Error: scenario field '") + field_name + "' must be a number";
        return false;
    }

    out = value.get<double>();
    if (!std::isfinite(out)) {
        error_message = std::string("Error: scenario field '") + field_name + "' must be finite";
        return false;
    }

    return true;
}

bool parseVector2Array(const json& value, const char* field_name, Complex& out, std::string& error_message) {
    if (!value.is_array() || value.size() != 2) {
        error_message = std::string("Error: scenario field '") + field_name + "' must be an array of 2 numbers";
        return false;
    }

    double x = 0.0;
    double y = 0.0;
    if (!parseFiniteNumber(value[0], field_name, x, error_message)) return false;
    if (!parseFiniteNumber(value[1], field_name, y, error_message)) return false;

    out = Complex{x, y};
    return true;
}

bool parseBodyCharge(const json& body, double& out_q, std::string& error_message) {
    if (body.contains("mass")) {
        if (!parseFiniteNumber(body.at("mass"), "mass", out_q, error_message)) return false;
        return true;
    }

    if (body.contains("charge")) {
        if (!parseFiniteNumber(body.at("charge"), "charge", out_q, error_message)) return false;
        return true;
    }

    error_message = "Error: each scenario body requires either 'mass' or 'charge'";
    return false;
}
}

ScenarioLoadResult loadScenarioFromFile(const std::string& file_path) {
    ScenarioLoadResult result;

    std::ifstream input(file_path);
    if (!input.is_open()) {
        result.error_message = "Error: failed to open scenario file: " + file_path;
        return result;
    }

    json root;
    try {
        input >> root;
    } catch (const std::exception& ex) {
        result.error_message = "Error: malformed scenario JSON in " + file_path + ": " + ex.what();
        return result;
    }

    if (!root.is_object()) {
        result.error_message = "Error: scenario root must be a JSON object";
        return result;
    }

    if (root.contains("metadata") && !root.at("metadata").is_object()) {
        result.error_message = "Error: scenario 'metadata' must be an object when provided";
        return result;
    }

    if (!root.contains("bodies") || !root.at("bodies").is_array()) {
        result.error_message = "Error: scenario must contain a 'bodies' array";
        return result;
    }

    const json& bodies = root.at("bodies");
    if (bodies.empty()) {
        result.error_message = "Error: scenario 'bodies' array must not be empty";
        return result;
    }

    result.sources.reserve(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const json& body = bodies[i];
        if (!body.is_object()) {
            result.error_message = "Error: scenario body at index " + std::to_string(i) + " must be an object";
            return result;
        }

        if (!body.contains("position") || !body.contains("velocity")) {
            result.error_message = "Error: scenario body at index " + std::to_string(i)
                                 + " requires 'position' and 'velocity'";
            return result;
        }

        Complex position{0.0, 0.0};
        Complex velocity{0.0, 0.0};
        double q = 0.0;

        if (!parseVector2Array(body.at("position"), "position", position, result.error_message)) return result;
        if (!parseVector2Array(body.at("velocity"), "velocity", velocity, result.error_message)) return result;
        if (!parseBodyCharge(body, q, result.error_message)) return result;

        if (q < 0.0) {
            result.error_message = "Error: scenario body at index " + std::to_string(i)
                                 + " has negative mass/charge";
            return result;
        }

        fmm::Source source;
        source.position = position;
        source.velocity = velocity;
        source.q = q;
        result.sources.push_back(source);
    }

    result.ok = true;
    return result;
}

std::unique_lock<std::mutex> SimulationEngine::lockSourcesScoped() {
    return std::unique_lock<std::mutex>(sources_mutex_);
}

SimulationEngine::SimulationEngine(const SimulationOptions& options, int screen_size)
    : options_(options),
      screen_size_(screen_size),
      center_(static_cast<double>(screen_size_) / 2.0, static_cast<double>(screen_size_) / 2.0),
      cluster_dist_(static_cast<double>(screen_size_) / 2.0, static_cast<double>(screen_size_) / 10.0),
      uniform_dist_(100.0, static_cast<double>(screen_size_) - 100.0),
      sources_(),
    tree_(sources_, 60, 10) {
    initializeSources();

    {
        std::lock_guard<std::mutex> tree_lock(tree_mutex_);
        tree_.buildTree();
        current_forces_ = tree_.forces;
    }
}

SimulationEngine::~SimulationEngine() = default;

void SimulationEngine::addParticles(std::vector<fmm::Source>& sources,
                                    double x,
                                    double y,
                                    double radius,
                                    std::mt19937& gen,
                                    int screen_size,
                                    int count) {
    std::uniform_real_distribution<double> angle_dist(0.0, 2.0 * kPi);
    std::uniform_real_distribution<double> speed_dist(50.0, 200.0);

    sources.reserve(sources.size() + static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        const auto [px, py] = sampleSpawnPointWithinBounds(x, y, radius, screen_size, gen);

        const double vel_theta = angle_dist(gen);
        const double vel_mag = speed_dist(gen);

        sources.emplace_back(px, py, 1.0);
        sources.back().velocity = Complex{vel_mag * std::cos(vel_theta), vel_mag * std::sin(vel_theta)};
    }
}

void SimulationEngine::removeParticles(std::vector<fmm::Source>& sources,
                                       double x,
                                       double y,
                                       double radius,
                                       double protected_mass) {
    auto it = sources.begin();
    while (it != sources.end()) {
        if (protected_mass > 0.0 && it->q >= protected_mass * 0.9) {
            ++it;
            continue;
        }

        const double dx = it->position.real() - x;
        const double dy = it->position.imag() - y;
        const double dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= radius) it = sources.erase(it);
        else ++it;
    }
}

void SimulationEngine::initializeSources() {
    sources_.clear();

    if (!options_.scenario_file.empty()) {
        ScenarioLoadResult loaded = loadScenarioFromFile(options_.scenario_file);
        if (!loaded.ok) {
            throw std::runtime_error(loaded.error_message);
        }
        sources_ = std::move(loaded.sources);
        return;
    }

    int total_bodies = options_.cluster_bodies + options_.uniform_bodies + options_.orbit_bodies;
    if (options_.black_hole_mass > 0.0) total_bodies += 1;

    sources_.reserve(static_cast<std::size_t>(total_bodies));

    if (options_.black_hole_mass > 0.0) {
        sources_.emplace_back(center_.real(), center_.imag(), options_.black_hole_mass);
        sources_.back().velocity = Complex{0.0, 0.0};
    }

    for (int i = 0; i < options_.cluster_bodies; ++i) {
        double px = cluster_dist_(gen_);
        double py = cluster_dist_(gen_);
        while (px < 100.0 || px > static_cast<double>(screen_size_ - 100)) px = cluster_dist_(gen_);
        while (py < 100.0 || py > static_cast<double>(screen_size_ - 100)) py = cluster_dist_(gen_);
        sources_.emplace_back(px, py, 10.0);
    }

    for (int i = 0; i < options_.uniform_bodies; ++i) {
        sources_.emplace_back(uniform_dist_(gen_), uniform_dist_(gen_), 1.0);
    }

    const double orbital_speed = 200.0;
    for (auto& source : sources_) {
        const Complex displacement = source.position - center_;
        const double radius = std::abs(displacement);
        if (radius > 1.0) {
            Complex tangent{-displacement.imag(), displacement.real()};
            tangent /= radius;
            source.velocity = tangent * orbital_speed;
        }
    }

    if (options_.orbit_bodies <= 0) return;

    std::uniform_real_distribution<double> orbit_angle(0.0, 2.0 * kPi);
    std::uniform_real_distribution<double> orbit_radius(120.0, static_cast<double>(screen_size_) * 0.5 - 120.0);
    std::uniform_real_distribution<double> speed_jitter(0.9, 1.1);

    constexpr double orbit_direction = 1.0;
    const double base_orbit_speed = std::sqrt(options_.black_hole_mass);

    for (int i = 0; i < options_.orbit_bodies; ++i) {
        const double theta = orbit_angle(gen_);
        const double radius = orbit_radius(gen_);

        const double px = center_.real() + radius * std::cos(theta);
        const double py = center_.imag() + radius * std::sin(theta);

        sources_.emplace_back(px, py, 1.0);

        Complex radial = sources_.back().position - center_;
        const double radial_norm = std::abs(radial);
        if (radial_norm > 1e-9) {
            Complex tangent{-radial.imag(), radial.real()};
            tangent /= radial_norm;
            sources_.back().velocity = tangent * (orbit_direction * base_orbit_speed * speed_jitter(gen_));
        }
    }
}

void SimulationEngine::pinBlackHoleLocked() {
    if (!options_.scenario_file.empty()) return;
    if (options_.black_hole_mass <= 0.0) return;

    for (auto& source : sources_) {
        if (source.q >= options_.black_hole_mass * 0.9) {
            source.position = center_;
            source.velocity = Complex{0.0, 0.0};
            break;
        }
    }
}

void SimulationEngine::rebuildTreeSynchronously() {
    auto sources_lock = lockSourcesScoped();
    std::lock_guard<std::mutex> tree_lock(tree_mutex_);
    tree_.buildTree();
    current_forces_ = tree_.forces;
}

void SimulationEngine::step(double dt) {
    using Clock = std::chrono::high_resolution_clock;
    const auto frame_start = Clock::now();

    auto phase_start = Clock::now();
    {
        auto lock = lockSourcesScoped();
        if (current_forces_.size() != sources_.size()) {
            current_forces_.assign(sources_.size(), Complex{0.0, 0.0});
        }

        const int start_idx = (options_.scenario_file.empty() && options_.black_hole_mass > 0.0) ? 1 : 0;
        #pragma omp parallel for schedule(static)
        for (int i = start_idx; i < static_cast<int>(sources_.size()); ++i) {
            sources_[i].velocity += 0.5 * current_forces_[i] * dt;
            sources_[i].position += sources_[i].velocity * dt;
        }

        pinBlackHoleLocked();
    }

    stats_.integrate_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

    phase_start = Clock::now();
    const bool should_rebuild_now =
        (options_.rebuild_every <= 1) || ((total_frames_ % options_.rebuild_every) == 0);
    stats_.rebuilt_forces_this_frame = should_rebuild_now;
    stats_.build_telemetry_updated_this_frame = false;
    if (should_rebuild_now) {
        // Enforce deterministic force freshness at configured cadence.
        auto lock = lockSourcesScoped();
        std::lock_guard<std::mutex> tree_lock(tree_mutex_);
        tree_.buildTree();
        current_forces_ = tree_.forces;

        const auto& build = tree_.lastBuildTelemetry();
        stats_.build_telemetry_updated_this_frame = true;
        stats_.build_sort_ms = build.sort_ms;
        stats_.build_node_lists_ms = build.node_lists_ms;
        stats_.build_upward_ms = build.upward_ms;
        stats_.build_downward_ms = build.downward_ms;
        stats_.build_forces_ms = build.forces_ms;
        stats_.build_total_internal_ms = build.total_ms;
        stats_.build_source_count = build.source_count;
        stats_.build_active_nodes = build.active_nodes;
        stats_.build_tree_height = build.tree_height;
        stats_.build_leaf_nodes = build.leaf_nodes;
        stats_.build_max_leaf_sources = build.max_leaf_sources;
        stats_.build_total_near_neighbors = build.total_near_neighbors;
        stats_.build_total_interaction_list = build.total_interaction_list;
        stats_.build_total_list_w = build.total_list_w;
        stats_.build_total_list_x = build.total_list_x;
        stats_.build_list_w_force_evals = build.list_w_force_evals;
        stats_.build_direct_pair_evals = build.direct_pair_evals;
        stats_.build_omp_max_threads = build.omp_max_threads;
        stats_.build_omp_dynamic_enabled = build.omp_dynamic_enabled;
        stats_.build_omp_threads_node_lists = build.omp_threads_node_lists;
        stats_.build_omp_threads_upward = build.omp_threads_upward;
        stats_.build_omp_threads_downward = build.omp_threads_downward;
        stats_.build_omp_threads_forces = build.omp_threads_forces;

        stats_.frames_since_force_rebuild = 0;
    } else {
        ++stats_.frames_since_force_rebuild;
    }

    stats_.build_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

    if (total_frames_ == 0) stats_.ema_build_ms = stats_.build_ms;
    else stats_.ema_build_ms = (1.0f - kEmaAlpha) * stats_.ema_build_ms + kEmaAlpha * stats_.build_ms;
    stats_.max_build_ms = std::max(stats_.max_build_ms, stats_.build_ms);

    phase_start = Clock::now();
    {
        auto lock = lockSourcesScoped();
        const int start_idx = (options_.scenario_file.empty() && options_.black_hole_mass > 0.0) ? 1 : 0;

        #pragma omp parallel for schedule(static)
        for (int i = start_idx; i < static_cast<int>(sources_.size()); ++i) {
            sources_[i].velocity += 0.5 * current_forces_[i] * dt;
        }
    }

    stats_.phase3_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

    stats_.frame_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - frame_start).count()) / 1000.0f;

    ++total_frames_;
}

void SimulationEngine::addParticlesAt(double x, double y, int count) {
    bool size_changed = false;
    {
        auto lock = lockSourcesScoped();
        const std::size_t old_size = sources_.size();

        addParticles(sources_, x, y, interaction_radius_, gen_, screen_size_, count);
        size_changed = (sources_.size() != old_size);
    }

    if (size_changed) {
        rebuildTreeSynchronously();
    }
}

void SimulationEngine::removeParticlesAt(double x, double y) {
    bool size_changed = false;
    {
        auto lock = lockSourcesScoped();
        const std::size_t old_size = sources_.size();

        const double protected_mass = options_.scenario_file.empty() ? options_.black_hole_mass : 0.0;
        removeParticles(sources_, x, y, interaction_radius_, protected_mass);
        size_changed = (sources_.size() != old_size);
    }

    if (size_changed) {
        rebuildTreeSynchronously();
    }
}

void SimulationEngine::increaseInteractionRadius(double delta) {
    interaction_radius_ = std::clamp(interaction_radius_ + delta, kRadiusStepMin, kRadiusStepMax);
}

void SimulationEngine::decreaseInteractionRadius(double delta) {
    interaction_radius_ = std::clamp(interaction_radius_ - delta, kRadiusStepMin, kRadiusStepMax);
}

double SimulationEngine::interactionRadius() const noexcept {
    return interaction_radius_;
}

int SimulationEngine::rebuildEvery() const noexcept {
    return options_.rebuild_every;
}

std::size_t SimulationEngine::particleCount() {
    auto lock = lockSourcesScoped();
    return sources_.size();
}

std::unique_lock<std::mutex> SimulationEngine::lockSources() {
    return lockSourcesScoped();
}

const std::vector<fmm::Source>& SimulationEngine::sources() const noexcept {
    return sources_;
}

std::vector<std::pair<Complex, double>> SimulationEngine::boxGeometries() {
    std::lock_guard<std::mutex> tree_lock(tree_mutex_);
    return tree_.getBoxGeometries();
}

const EngineFrameStats& SimulationEngine::frameStats() const noexcept {
    return stats_;
}

}  // namespace sim