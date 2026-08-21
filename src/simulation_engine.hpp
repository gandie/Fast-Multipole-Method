#ifndef SIMULATION_ENGINE_HPP
#define SIMULATION_ENGINE_HPP

#include <atomic>
#include <cstddef>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "fmm_tree.hpp"
#include "force_swap.hpp"
#include "sim_options.hpp"

namespace sim {

struct EngineFrameStats {
    float integrate_ms = 0.0f;
    float build_ms = 0.0f;
    float phase3_ms = 0.0f;
    float frame_ms = 0.0f;
    float ema_build_ms = 0.0f;
    float max_build_ms = 0.0f;
};

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
    struct AsyncTreeBuilder {
        std::thread worker_thread;
        std::atomic<bool> building{false};
        std::mutex forces_mutex;
        std::mutex sources_mutex;
        std::vector<Complex> pending_forces;

        void startBuild(fmm::FmmTree& tree, std::mutex& tree_mutex);
        bool trySwapForces(std::vector<Complex>& current_forces);
        std::unique_lock<std::mutex> lockSourcesScoped();

        ~AsyncTreeBuilder();
    };

    void initializeSources();
    void pinBlackHoleLocked();
    void rebuildTreeSynchronously();

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

    AsyncTreeBuilder async_builder_;
    std::mutex tree_mutex_;

    int total_frames_ = 0;
    double interaction_radius_ = 50.0;

    EngineFrameStats stats_{};
};

}  // namespace sim

#endif