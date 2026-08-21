#ifndef SIMULATION_ENGINE_HPP
#define SIMULATION_ENGINE_HPP

#include <cstddef>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "fmm_tree.hpp"
#include "sim_options.hpp"

namespace sim {

struct EngineFrameStats {
    float integrate_ms = 0.0f;
    float build_ms = 0.0f;
    float phase3_ms = 0.0f;
    float frame_ms = 0.0f;
    float ema_build_ms = 0.0f;
    float max_build_ms = 0.0f;
    bool rebuilt_forces_this_frame = false;
    int frames_since_force_rebuild = 0;
};

struct ScenarioLoadResult {
    bool ok = false;
    std::vector<fmm::Source> sources;
    std::string error_message;
};

ScenarioLoadResult loadScenarioFromFile(const std::string& file_path);

class SimulationEngine {
public:
    explicit SimulationEngine(const SimulationOptions& options, int screen_size);
    ~SimulationEngine();

    SimulationEngine(const SimulationEngine&) = delete;
    SimulationEngine& operator=(const SimulationEngine&) = delete;

    void step(double dt);

    void addParticlesAt(double x, double y, int count = 1000);
    void removeParticlesAt(double x, double y);

    void increaseInteractionRadius(double delta);
    void decreaseInteractionRadius(double delta);
    double interactionRadius() const noexcept;

    int rebuildEvery() const noexcept;
    std::size_t particleCount();

    std::unique_lock<std::mutex> lockSources();
    const std::vector<fmm::Source>& sources() const noexcept;

    std::vector<std::pair<Complex, double>> boxGeometries();

    const EngineFrameStats& frameStats() const noexcept;

private:
    void initializeSources();
    void pinBlackHoleLocked();
    void rebuildTreeSynchronously();
    std::unique_lock<std::mutex> lockSourcesScoped();

    static void addParticles(std::vector<fmm::Source>& sources,
                             double x,
                             double y,
                             double radius,
                             std::mt19937& gen,
                             int screen_size,
                             int count);

    static void removeParticles(std::vector<fmm::Source>& sources,
                                double x,
                                double y,
                                double radius,
                                double protected_mass);

    SimulationOptions options_{};
    int screen_size_ = 0;
    Complex center_{0.0, 0.0};
    std::mt19937 gen_{42};
    std::normal_distribution<double> cluster_dist_;
    std::uniform_real_distribution<double> uniform_dist_;

    std::vector<fmm::Source> sources_;
    fmm::FmmTree tree_;
    std::vector<Complex> current_forces_;

    std::mutex sources_mutex_;
    std::mutex tree_mutex_;

    int total_frames_ = 0;
    double interaction_radius_ = 50.0;

    EngineFrameStats stats_{};
};

}  // namespace sim

#endif