#include "simulation_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <omp.h>

#include "spawn_utils.hpp"

namespace sim {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRadiusStepMin = 10.0;
constexpr double kRadiusStepMax = 300.0;
constexpr float kEmaAlpha = 0.10f;
}

void SimulationEngine::AsyncTreeBuilder::startBuild(fmm::FmmTree& tree, std::mutex& tree_mutex) {
    if (building.load()) return;

    building.store(true);
    if (worker_thread.joinable()) worker_thread.join();

    worker_thread = std::thread([this, &tree, &tree_mutex]() {
        {
            std::lock_guard<std::mutex> source_lock(sources_mutex);
            std::lock_guard<std::mutex> tree_lock(tree_mutex);
            tree.buildTree();
        }
        {
            std::lock_guard<std::mutex> force_lock(forces_mutex);
            pending_forces = tree.forces;
        }
        building.store(false);
    });
}

bool SimulationEngine::AsyncTreeBuilder::trySwapForces(std::vector<Complex>& current_forces) {
    if (building.load()) return false;

    std::lock_guard<std::mutex> lock(forces_mutex);
    return sim::trySwapPendingForces(current_forces, pending_forces);
}

std::unique_lock<std::mutex> SimulationEngine::AsyncTreeBuilder::lockSourcesScoped() {
    return std::unique_lock<std::mutex>(sources_mutex);
}

SimulationEngine::AsyncTreeBuilder::~AsyncTreeBuilder() {
    if (worker_thread.joinable()) worker_thread.join();
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
                                       double radius) {
    auto it = sources.begin();
    while (it != sources.end()) {
        const double dx = it->position.real() - x;
        const double dy = it->position.imag() - y;
        const double dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= radius) it = sources.erase(it);
        else ++it;
    }
}

void SimulationEngine::initializeSources() {
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
    // Join before taking sources lock to avoid deadlock when worker is waiting on the same mutex.
    if (async_builder_.worker_thread.joinable()) async_builder_.worker_thread.join();
    async_builder_.building.store(false);

    {
        std::lock_guard<std::mutex> force_lock(async_builder_.forces_mutex);
        async_builder_.pending_forces.clear();
    }

    auto sources_lock = async_builder_.lockSourcesScoped();
    std::lock_guard<std::mutex> tree_lock(tree_mutex_);
    tree_.buildTree();
    current_forces_ = tree_.forces;
}

void SimulationEngine::step(double dt) {
    using Clock = std::chrono::high_resolution_clock;
    const auto frame_start = Clock::now();

    auto phase_start = Clock::now();
    {
        auto lock = async_builder_.lockSourcesScoped();
        if (current_forces_.size() != sources_.size()) {
            current_forces_.assign(sources_.size(), Complex{0.0, 0.0});
        }

        const int start_idx = (options_.black_hole_mass > 0.0) ? 1 : 0;
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
    async_builder_.trySwapForces(current_forces_);
    if (options_.rebuild_every <= 1 || (total_frames_ % options_.rebuild_every) == 0) {
        async_builder_.startBuild(tree_, tree_mutex_);
    }

    stats_.build_ms = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

    if (total_frames_ == 0) stats_.ema_build_ms = stats_.build_ms;
    else stats_.ema_build_ms = (1.0f - kEmaAlpha) * stats_.ema_build_ms + kEmaAlpha * stats_.build_ms;
    stats_.max_build_ms = std::max(stats_.max_build_ms, stats_.build_ms);

    phase_start = Clock::now();
    {
        auto lock = async_builder_.lockSourcesScoped();
        const int start_idx = (options_.black_hole_mass > 0.0) ? 1 : 0;

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
        auto lock = async_builder_.lockSourcesScoped();
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
        auto lock = async_builder_.lockSourcesScoped();
        const std::size_t old_size = sources_.size();

        removeParticles(sources_, x, y, interaction_radius_);
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
    auto lock = async_builder_.lockSourcesScoped();
    return sources_.size();
}

std::unique_lock<std::mutex> SimulationEngine::lockSources() {
    return async_builder_.lockSourcesScoped();
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