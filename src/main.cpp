#include <SFML/Graphics.hpp>
#include <vector>
#include <random>
#include <iostream>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>

#include <omp.h>

#include "fmm_tree.hpp"
#include "barnes_hut_tree.hpp"
#include "diagnostics.hpp"
#include "sim_options.hpp"
#include "force_swap.hpp"

void printHelp() {
    std::cout << "FMM Simulation - N-body simulation using Fast Multipole Method\n\n"
              << "Usage: sim [options]\n\n"
              << "Options:\n"
              << "  -h, --help                Print this help message and exit\n"
              << "  -r, --rebuild-every N    Tree rebuild frequency (default: 1, every frame)\n"
              << "  -c, --cluster-bodies N   Number of cluster-distributed bodies (default: 40000)\n"
              << "  -u, --uniform-bodies N   Number of uniformly-distributed bodies (default: 10000)\n"
              << "  -o, --orbit N            Number of circular-orbit bodies (requires --black-hole)\n"
              << "  -b, --black-hole M       Create massive central body with mass M (default: disabled)\n\n"
              << "Examples:\n"
              << "  sim                              # Run with defaults\n"
              << "  sim -r 4 -c 30000 -u 5000       # Custom rebuild/body counts\n"
              << "  sim --rebuild-every 2 --cluster-bodies 20000\n"
              << "  sim -b 100000                    # Add black hole with mass 100000\n"
              << "  sim -b 100000 -o 2000            # Spawn 2000 orbit bodies around black hole\n";
}

// Async tree building manager
struct AsyncTreeBuilder {
    std::thread worker_thread;
    std::atomic<bool> building{false};
    std::atomic<bool> should_stop{false};
    std::mutex forces_mutex;
    std::mutex sources_mutex;
    std::vector<Complex> pending_forces;
    
    template<typename TreeType>
    void startBuild(TreeType& tree) {
        if (building.load()) return; // Already building
        
        building.store(true);
        if (worker_thread.joinable()) worker_thread.join();
        
        worker_thread = std::thread([this, &tree]() {
            {
                std::lock_guard<std::mutex> lock(sources_mutex);
                tree.buildTree();
            }
            {
                std::lock_guard<std::mutex> lock(forces_mutex);
                pending_forces = tree.forces;
            }
            building.store(false);
        });
    }
    
    bool trySwapForces(std::vector<Complex>& current_forces) {
        if (!building.load()) {
            std::lock_guard<std::mutex> lock(forces_mutex);
            return sim::trySwapPendingForces(current_forces, pending_forces);
        }
        return false;
    }
    
    std::unique_lock<std::mutex> lockSourcesScoped() {
        return std::unique_lock<std::mutex>(sources_mutex);
    }
    
    ~AsyncTreeBuilder() {
        should_stop.store(true);
        if (worker_thread.joinable()) worker_thread.join();
    }
};


// Add particles around cursor position with random velocity
void addParticles(std::vector<fmm::Source>& sources, const sf::Vector2f& cursor, 
                  double radius, std::mt19937& gen, int screen_size, int count = 1000) {
    std::uniform_real_distribution<double> angle_dist(0.0, 2.0 * M_PI);
    std::uniform_real_distribution<double> r_dist(0.0, radius);
    std::uniform_real_distribution<double> speed_dist(50.0, 200.0);
    
    // Pre-reserve to avoid reallocation during adds
    sources.reserve(sources.size() + count);
    
    for (int i = 0; i < count; ++i) {
        // Random position within radius circle
        double theta = angle_dist(gen);
        double r = r_dist(gen);
        double x = cursor.x + r * std::cos(theta);
        double y = cursor.y + r * std::sin(theta);
        
        // Clamp to screen bounds [100, screen_size - 100]
        x = std::clamp(x, 100.0, static_cast<double>(screen_size - 100));
        y = std::clamp(y, 100.0, static_cast<double>(screen_size - 100));
        
        // Random velocity direction and magnitude
        double vel_theta = angle_dist(gen);
        double vel_mag = speed_dist(gen);
        double vx = vel_mag * std::cos(vel_theta);
        double vy = vel_mag * std::sin(vel_theta);
        
        sources.emplace_back(x, y, 1.0);
        sources.back().velocity = Complex{vx, vy};
    }
}

// Remove particles within radius of cursor position
void removeParticles(std::vector<fmm::Source>& sources, const sf::Vector2f& cursor, double radius) {
    auto it = sources.begin();
    while (it != sources.end()) {
        double dx = it->position.real() - cursor.x;
        double dy = it->position.imag() - cursor.y;
        double dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist <= radius) {
            it = sources.erase(it);
        } else {
            ++it;
        }
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> cli_args;
    cli_args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) cli_args.emplace_back(argv[i]);

    const sim::ParseResult parsed = sim::parseSimulationArgs(cli_args);
    if (parsed.options.help_requested) {
        printHelp();
        return 0;
    }
    if (!parsed.ok) {
        std::cerr << parsed.error_message << "\n";
        std::cerr << "Use -h or --help for usage information\n";
        return 1;
    }

    const int rebuild_every = parsed.options.rebuild_every;
    const int cluster_bodies = parsed.options.cluster_bodies;
    const int uniform_bodies = parsed.options.uniform_bodies;
    const int orbit_bodies = parsed.options.orbit_bodies;
    const double black_hole_mass = parsed.options.black_hole_mass;

    sf::Color p_color = sf::Color::Cyan;
    const int screen_size = 1380;
    sf::RenderWindow window(sf::VideoMode({screen_size, screen_size}), "Simulation");
    window.setFramerateLimit(60);

    // Simulation settings
    const double dt = 0.001;

    sf::Clock frameTimer;
    sf::Clock phaseClock;
    sf::Clock uiTimer;

    // Font loading with fallbacks
    sf::Font font;
    bool canDrawText = false;
    {
        const std::vector<std::string> fontCandidates = {
            "assets/fonts/JetBrainsMonoNL-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
            "/Library/Fonts/JetBrainsMonoNL-Regular.ttf"
        };

        for (const auto& path : fontCandidates) {
            if (font.openFromFile(path)) {
                std::cout << "Loaded font: " << path << "\n";
                canDrawText = true;
                break;
            }
        }

        if (!canDrawText) {
            std::cerr << "Warning: no font loaded. Overlay text disabled.\n";
        }
    }

    // Initial particle setup
    std::mt19937 gen(42);
    std::normal_distribution<double> cluster(1.0 * screen_size / 2, 1.0 * screen_size / 10);
    std::uniform_real_distribution<double> uniform(100.0, screen_size * 1.0 - 100.0);

    std::vector<fmm::Source> sources;
    int total_bodies = cluster_bodies + uniform_bodies + orbit_bodies;
    if (black_hole_mass > 0.0) total_bodies += 1;
    sources.reserve(total_bodies);

    Complex center{1.0 * screen_size / 2, 1.0 * screen_size / 2};
    
    // Create massive central body (black hole) if enabled
    if (black_hole_mass > 0.0) {
        sources.emplace_back(center.real(), center.imag(), black_hole_mass);
        sources.back().velocity = Complex{0.0, 0.0};  // Keep at center
    }
    
    for (int i = 0; i < cluster_bodies; i++) {
        double x = cluster(gen), y = cluster(gen);
        while (x < 100 || x > screen_size - 100) x = cluster(gen);
        while (y < 100 || y > screen_size - 100) y = cluster(gen);
        sources.emplace_back(x, y, 10.0);
    }
    for (int i = 0; i < uniform_bodies; i++)
        sources.emplace_back(uniform(gen), uniform(gen), 1.0);

    const double orbital_speed = 200.0;
    for (auto& s : sources) {
        Complex disp = s.position - center;
        double r = std::abs(disp);
        if (r > 1.0) {
            Complex tangent{-disp.imag(), disp.real()};
            tangent /= r;
            s.velocity = tangent * orbital_speed;
        }
    }

    if (orbit_bodies > 0) {
        std::uniform_real_distribution<double> orbit_angle(0.0, 2.0 * M_PI);
        std::uniform_real_distribution<double> orbit_radius(120.0, static_cast<double>(screen_size) * 0.5 - 120.0);
        std::uniform_real_distribution<double> speed_jitter(0.9, 1.1);
        constexpr double orbit_direction = 1.0;  // +1 for one consistent tangential direction

        const double base_orbit_speed = std::sqrt(black_hole_mass);
        for (int i = 0; i < orbit_bodies; ++i) {
            double theta = orbit_angle(gen);
            double r = orbit_radius(gen);

            double x = center.real() + r * std::cos(theta);
            double y = center.imag() + r * std::sin(theta);

            sources.emplace_back(x, y, 1.0);

            Complex radial = sources.back().position - center;
            double radial_norm = std::abs(radial);
            if (radial_norm > 1e-9) {
                Complex tangent{-radial.imag(), radial.real()};
                tangent /= radial_norm;
                sources.back().velocity = tangent * (orbit_direction * base_orbit_speed * speed_jitter(gen));
            }
        }
    }

    // Build initial tree/forces
    fmm::FmmTree tree(sources, 60, 10);
    // fmm::BhTree tree(sources, 1, 2.5);
    tree.buildTree();

    bool drawBoxes = false;

    // Interactive particle manipulation
    double interaction_radius = 50.0;
    const double radius_step = 5.0;
    const double radius_min = 10.0;
    const double radius_max = 300.0;

    // Async tree building infrastructure
    AsyncTreeBuilder async_builder;
    std::vector<Complex> current_forces = tree.forces;

    // Reused draw buffers
    sf::VertexArray particle_va(sf::PrimitiveType::Points, sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        particle_va[i].color = p_color;
    }

    sf::Text overlay(font, "", 20);
    overlay.setFillColor(sf::Color::White);
    overlay.setPosition({10.f, 8.f});

    // Timing stats
    int tot_frames = 0;
    float tIntegrateMs = 0.f, tBuildMs = 0.f, tRenderMs = 0.f, tFrameMs = 0.f;
    float emaBuildMs = 0.f;
    float maxBuildMs = 0.f;
    constexpr float emaAlpha = 0.10f;

    while (window.isOpen()) {
        frameTimer.restart();

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) window.close();
            else if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) window.close();
            }
            else if (const auto* mouseButton = event->getIf<sf::Event::MouseButtonPressed>()) {
                sf::Vector2f cursor = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                
                {
                    auto lock = async_builder.lockSourcesScoped();
                    size_t old_size = sources.size();
                    
                    if (mouseButton->button == sf::Mouse::Button::Left) {
                        addParticles(sources, cursor, interaction_radius, gen, screen_size, 1000);
                    } else if (mouseButton->button == sf::Mouse::Button::Right) {
                        removeParticles(sources, cursor, interaction_radius);
                    }
                    
                    size_t new_size = sources.size();
                    
                    // If particle count changed, IMMEDIATELY rebuild tree synchronously
                    // to maintain consistency between tree indices, forces, and sources.
                    // Adding/removing particles invalidates tree node indices (start_id, num_sources).
                    if (new_size != old_size) {
                        // Wait for any in-flight async build to finish
                        if (async_builder.worker_thread.joinable()) {
                            async_builder.worker_thread.join();
                        }
                        async_builder.building.store(false);
                        
                        // CRITICAL: Clear pending_forces to prevent stale async build results
                        // from being swapped in after we've added/removed particles!
                        {
                            std::lock_guard<std::mutex> lock(async_builder.forces_mutex);
                            async_builder.pending_forces.clear();
                        }
                        
                        // Rebuild tree synchronously with current particle state
                        tree.buildTree();
                        
                        // Update forces and auxiliary data structures
                        current_forces = tree.forces;
                        
                        // Rebuild vertex array
                        particle_va.resize(new_size);
                        for (size_t i = 0; i < new_size; ++i) {
                            particle_va[i].color = p_color;
                        }
                    }
                }
            }
            else if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>()) {
                if (scroll->delta > 0) {
                    interaction_radius = std::min(interaction_radius + radius_step, radius_max);
                } else {
                    interaction_radius = std::max(interaction_radius - radius_step, radius_min);
                }
            }
        }

        window.clear(sf::Color::Black);

        // Phase 1: integrate (half-kick + drift)
        phaseClock.restart();
        {
            auto lock = async_builder.lockSourcesScoped();
            // Skip black hole (index 0) if present
            int start_idx = (black_hole_mass > 0.0) ? 1 : 0;
            #pragma omp parallel for schedule(static)
            for (int i = start_idx; i < static_cast<int>(sources.size()); i++) {
                sources[i].velocity += 0.5 * current_forces[i] * dt;
                sources[i].position += sources[i].velocity * dt;
            }
        }
        tIntegrateMs = static_cast<float>(phaseClock.getElapsedTime().asMicroseconds()) / 1000.0f;

        // Ensure black hole stays fixed at center (find it by its large mass)
        if (black_hole_mass > 0.0) {
            for (int i = 0; i < static_cast<int>(sources.size()); i++) {
                if (sources[i].q >= black_hole_mass * 0.9) {  // Account for floating point
                    sources[i].position = center;
                    sources[i].velocity = Complex{0.0, 0.0};
                    break;  // Only one black hole
                }
            }
        }

        // Phase 2: attempt to swap forces if tree building completed, then start new build
        phaseClock.restart();
        async_builder.trySwapForces(current_forces);
        
        if (rebuild_every <= 1 || (tot_frames % rebuild_every) == 0) {
            async_builder.startBuild(tree);
        }
        tBuildMs = static_cast<float>(phaseClock.getElapsedTime().asMicroseconds()) / 1000.0f;

        // Build-time stats
        if (tot_frames == 0) emaBuildMs = tBuildMs;
        else emaBuildMs = (1.0f - emaAlpha) * emaBuildMs + emaAlpha * tBuildMs;
        maxBuildMs = std::max(maxBuildMs, tBuildMs);

        // Phase 3: second half-kick + draw prep + draw
        phaseClock.restart();

        {
            auto lock = async_builder.lockSourcesScoped();
            // Skip black hole (index 0) if present
            int start_idx = (black_hole_mass > 0.0) ? 1 : 0;
            #pragma omp parallel for schedule(static)
            for (int i = start_idx; i < static_cast<int>(sources.size()); i++) {
                sources[i].velocity += 0.5 * current_forces[i] * dt;
            }

            // Updating vertex buffer is often smoother single-threaded
            for (size_t i = 0; i < sources.size(); ++i) {
                particle_va[i].position = sf::Vector2f(
                    static_cast<float>(sources[i].position.real()),
                    static_cast<float>(sources[i].position.imag())
                );
            }
        }

        if (drawBoxes) {
            auto boxes = tree.getBoxGeometries();
            sf::VertexArray box_va(sf::PrimitiveType::Lines, boxes.size() * 8);
            sf::Color box_color(66, 191, 245);

            for (size_t i = 0; i < boxes.size(); i++) {
                int id_b = static_cast<int>(i) * 8;

                float cx = static_cast<float>(boxes[i].first.real());
                float cy = static_cast<float>(boxes[i].first.imag());
                float len = static_cast<float>(boxes[i].second / 2.0);

                sf::Vector2f tl(cx - len, cy - len);
                sf::Vector2f tr(cx + len, cy - len);
                sf::Vector2f br(cx + len, cy + len);
                sf::Vector2f bl(cx - len, cy + len);

                box_va[id_b    ] = sf::Vertex{tl, box_color};
                box_va[id_b + 1] = sf::Vertex{tr, box_color};
                box_va[id_b + 2] = sf::Vertex{tr, box_color};
                box_va[id_b + 3] = sf::Vertex{br, box_color};
                box_va[id_b + 4] = sf::Vertex{br, box_color};
                box_va[id_b + 5] = sf::Vertex{bl, box_color};
                box_va[id_b + 6] = sf::Vertex{bl, box_color};
                box_va[id_b + 7] = sf::Vertex{tl, box_color};
            }
            window.draw(box_va);
        }

        window.draw(particle_va);

        tRenderMs = static_cast<float>(phaseClock.getElapsedTime().asMicroseconds()) / 1000.0f;
        tFrameMs = static_cast<float>(frameTimer.getElapsedTime().asMicroseconds()) / 1000.0f;

        // Update overlay at ~10 Hz
        if (canDrawText && uiTimer.getElapsedTime().asMilliseconds() >= 100) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2)
                << "frame: " << tFrameMs << " ms\n"
                << "integrate: " << tIntegrateMs << " ms\n"
                << "buildTree: " << tBuildMs << " ms"
                << " (ema " << emaBuildMs << ", max " << maxBuildMs << ")\n"
                << "render: " << tRenderMs << " ms\n"
                << "build every N: " << rebuild_every << "\n"
                << "particles: " << sources.size() << "\n"
                << "interaction radius: " << static_cast<int>(interaction_radius) << " px";
            overlay.setString(oss.str());
            uiTimer.restart();
        }

        if (canDrawText) {
            window.draw(overlay);
        }

        window.display();
        tot_frames++;
    }

    return 0;
}
