#ifndef SPAWN_UTILS_HPP
#define SPAWN_UTILS_HPP

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace sim {

inline std::pair<double, double> sampleSpawnPointWithinBounds(
    double center_x,
    double center_y,
    double radius,
    int screen_size,
    std::mt19937& gen) {

    constexpr int attempts = 64;
    constexpr double min_bound = 100.0;

    const double max_bound = static_cast<double>(screen_size - 100);

    const double spawn_cx = std::clamp(center_x, min_bound, max_bound);
    const double spawn_cy = std::clamp(center_y, min_bound, max_bound);
    const double spawn_radius = std::max(radius, 1.0);

    std::uniform_real_distribution<double> angle_dist(0.0, 2.0 * 3.14159265358979323846);
    std::uniform_real_distribution<double> unit_dist(0.0, 1.0);
    std::uniform_real_distribution<double> jitter_dist(-2.0, 2.0);

    for (int attempt = 0; attempt < attempts; ++attempt) {
        const double theta = angle_dist(gen);
        const double r = spawn_radius * std::sqrt(unit_dist(gen));

        const double px = spawn_cx + r * std::cos(theta);
        const double py = spawn_cy + r * std::sin(theta);

        if (px >= min_bound && px <= max_bound && py >= min_bound && py <= max_bound) {
            return {px, py};
        }
    }

    // Rare fallback: keep bounded and non-degenerate to avoid singular body overlap.
    return {
        std::clamp(spawn_cx + jitter_dist(gen), min_bound, max_bound),
        std::clamp(spawn_cy + jitter_dist(gen), min_bound, max_bound)
    };
}

}  // namespace sim

#endif