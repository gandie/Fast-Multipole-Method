#include <SFML/Graphics.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include <array>
#include <random>
#include <algorithm>
#include <cstdint>
#include <omp.h>
#include "sim_options.hpp"
#include "simulation_engine.hpp"

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
              << "  -s, --scenario FILE      Load bodies from JSON scenario file (overrides -c/-u/-o/-b)\n\n"
              << "Examples:\n"
              << "  sim                              # Run with defaults\n"
              << "  sim -r 4 -c 30000 -u 5000       # Custom rebuild/body counts\n"
              << "  sim --rebuild-every 2 --cluster-bodies 20000\n"
              << "  sim -b 100000                    # Add black hole with mass 100000\n"
              << "  sim -b 100000 -o 2000            # Spawn 2000 orbit bodies around black hole\n"
              << "  sim --scenario examples/scenarios/two_body_minimal.json\n";
}

int main(int argc, char* argv[]) {
    try {
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

        omp_set_dynamic(0);
        const int startup_omp_threads = omp_get_max_threads();
        omp_set_num_threads(startup_omp_threads);
        std::cout << "OMP policy: dynamic=0 threads=" << startup_omp_threads << "\n";

        sf::Color p_color = sf::Color::Cyan;
        const int screen_size = 1380;
        sf::RenderWindow window(sf::VideoMode({screen_size, screen_size}), "Simulation");
        window.setFramerateLimit(60);

        const double dt = 0.001;

        sf::Clock frameTimer;
        sf::Clock renderClock;
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

        sim::SimulationEngine engine(parsed.options, screen_size);

        bool drawBoxes = false;
        bool drawStars = true;
        bool drawStreaks = true;
        bool drawGlow = true;
        int palette_index = 0;

        const double radius_step = 5.0;

        auto clamp01 = [](float v) {
            return std::max(0.0f, std::min(1.0f, v));
        };

        auto mixColor = [](const sf::Color& a, const sf::Color& b, float t) {
            const float u = std::max(0.0f, std::min(1.0f, t));
            const auto lerp = [u](std::uint8_t x, std::uint8_t y) {
                return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * u);
            };
            return sf::Color(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a));
        };

        std::array<sf::Color, 256> speed_lut{};
        const std::array<std::array<sf::Color, 4>, 3> palettes{{
            std::array<sf::Color, 4>{
                sf::Color(34, 44, 114, 245),
                sf::Color(54, 196, 255, 250),
                sf::Color(255, 216, 124, 255),
                sf::Color(255, 246, 222, 255)},
            std::array<sf::Color, 4>{
                sf::Color(44, 76, 146, 245),
                sf::Color(118, 255, 214, 250),
                sf::Color(255, 255, 166, 255),
                sf::Color(255, 255, 240, 255)},
            std::array<sf::Color, 4>{
                sf::Color(58, 24, 118, 245),
                sf::Color(255, 100, 220, 250),
                sf::Color(255, 190, 106, 255),
                sf::Color(255, 245, 232, 255)}
        }};

        auto rebuildSpeedLut = [&]() {
            const auto& p = palettes[static_cast<std::size_t>(palette_index) % palettes.size()];
            for (size_t i = 0; i < speed_lut.size(); ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(speed_lut.size() - 1);
                if (t < 0.38f) {
                    speed_lut[i] = mixColor(p[0], p[1], t / 0.38f);
                } else if (t < 0.78f) {
                    speed_lut[i] = mixColor(p[1], p[2], (t - 0.38f) / 0.40f);
                } else {
                    speed_lut[i] = mixColor(p[2], p[3], (t - 0.78f) / 0.22f);
                }
            }
        };
        rebuildSpeedLut();

        // Reused draw buffers
        sf::VertexArray particle_va(sf::PrimitiveType::Points, engine.particleCount());
        sf::VertexArray streak_va(sf::PrimitiveType::Lines);
        sf::VertexArray glow_va(sf::PrimitiveType::Triangles);

        float speed_scale = 180.0f;

        const int glow_grid_dim = 192;
        const float glow_cell = static_cast<float>(screen_size) / static_cast<float>(glow_grid_dim);
        const std::size_t glow_cell_count = static_cast<std::size_t>(glow_grid_dim * glow_grid_dim);
        std::vector<float> glow_counts(glow_cell_count, 0.0f);
        std::vector<float> glow_smoothed(glow_cell_count, 0.0f);
        std::vector<float> glow_blurred(glow_cell_count, 0.0f);

        glow_va.resize(glow_cell_count * 6);
        for (int gy = 0; gy < glow_grid_dim; ++gy) {
            for (int gx = 0; gx < glow_grid_dim; ++gx) {
                const std::size_t idx = static_cast<std::size_t>(gy * glow_grid_dim + gx);
                const std::size_t v = idx * 6;
                const float x0 = static_cast<float>(gx) * glow_cell;
                const float y0 = static_cast<float>(gy) * glow_cell;
                const float x1 = x0 + glow_cell;
                const float y1 = y0 + glow_cell;

                glow_va[v]     = sf::Vertex{sf::Vector2f(x0, y0), sf::Color(0, 0, 0, 0)};
                glow_va[v + 1] = sf::Vertex{sf::Vector2f(x1, y0), sf::Color(0, 0, 0, 0)};
                glow_va[v + 2] = sf::Vertex{sf::Vector2f(x1, y1), sf::Color(0, 0, 0, 0)};
                glow_va[v + 3] = sf::Vertex{sf::Vector2f(x0, y0), sf::Color(0, 0, 0, 0)};
                glow_va[v + 4] = sf::Vertex{sf::Vector2f(x1, y1), sf::Color(0, 0, 0, 0)};
                glow_va[v + 5] = sf::Vertex{sf::Vector2f(x0, y1), sf::Color(0, 0, 0, 0)};
            }
        }

        const std::size_t star_far_count = 520;
        const std::size_t star_near_count = 860;
        std::mt19937 star_rng(1337u);
        std::uniform_real_distribution<float> star_pos_dist(0.0f, static_cast<float>(screen_size));
        std::uniform_real_distribution<float> star_alpha_dist(0.0f, 1.0f);

        std::vector<sf::Vector2f> star_far_base(star_far_count);
        std::vector<sf::Vector2f> star_near_base(star_near_count);
        sf::VertexArray stars_far(sf::PrimitiveType::Points, star_far_count);
        sf::VertexArray stars_near(sf::PrimitiveType::Points, star_near_count);

        for (std::size_t i = 0; i < star_far_count; ++i) {
            star_far_base[i] = sf::Vector2f(star_pos_dist(star_rng), star_pos_dist(star_rng));
            const std::uint8_t alpha = static_cast<std::uint8_t>(35 + 60 * star_alpha_dist(star_rng));
            stars_far[i].color = sf::Color(120, 150, 200, alpha);
            stars_far[i].position = star_far_base[i];
        }
        for (std::size_t i = 0; i < star_near_count; ++i) {
            star_near_base[i] = sf::Vector2f(star_pos_dist(star_rng), star_pos_dist(star_rng));
            const std::uint8_t alpha = static_cast<std::uint8_t>(55 + 90 * star_alpha_dist(star_rng));
            stars_near[i].color = sf::Color(170, 210, 255, alpha);
            stars_near[i].position = star_near_base[i];
        }

        auto wrapToScreen = [screen_size](float v) {
            const float s = static_cast<float>(screen_size);
            while (v < 0.0f) v += s;
            while (v >= s) v -= s;
            return v;
        };

        auto syncParticleBuffers = [&](std::size_t n) {
            particle_va.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                particle_va[i].color = p_color;
            }
        };

        syncParticleBuffers(engine.particleCount());

        sf::Text overlay(font, "", 20);
        overlay.setFillColor(sf::Color::White);
        overlay.setPosition({10.f, 8.f});

        sf::Text controls_legend(font, "", 16);
        controls_legend.setFillColor(sf::Color(210, 224, 255, 220));
        controls_legend.setPosition({10.f, static_cast<float>(screen_size) - 26.f});

        float tRenderMs = 0.f;
        float tFrameMs = 0.f;
        size_t particle_count_snapshot = particle_va.getVertexCount();
        std::size_t telemetry_frame = 0;

        std::cout << "Visual toggles: [G]=glow [V]=streaks [N]=stars [C]=palette [B]=boxes\n";

          std::cout << "# build_telemetry frame rebuild spike build_ms build_internal_ms sort_ms node_lists_ms upward_ms downward_ms forces_ms "
                << "nodes height leaves max_leaf list_w_evals direct_pair_evals near_total interaction_total list_w_total list_x_total "
                << "omp_max_threads omp_dynamic omp_threads_node_lists omp_threads_upward omp_threads_downward omp_threads_forces particles\n";

        while (window.isOpen()) {
        frameTimer.restart();

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) window.close();
            else if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) window.close();
                else if (keyPressed->scancode == sf::Keyboard::Scancode::G) drawGlow = !drawGlow;
                else if (keyPressed->scancode == sf::Keyboard::Scancode::V) drawStreaks = !drawStreaks;
                else if (keyPressed->scancode == sf::Keyboard::Scancode::N) drawStars = !drawStars;
                else if (keyPressed->scancode == sf::Keyboard::Scancode::B) drawBoxes = !drawBoxes;
                else if (keyPressed->scancode == sf::Keyboard::Scancode::C) {
                    palette_index = (palette_index + 1) % static_cast<int>(palettes.size());
                    rebuildSpeedLut();
                }
            }
            else if (const auto* mouseButton = event->getIf<sf::Event::MouseButtonPressed>()) {
                sf::Vector2f cursor = window.mapPixelToCoords(sf::Mouse::getPosition(window));

                if (mouseButton->button == sf::Mouse::Button::Left) {
                    engine.addParticlesAt(cursor.x, cursor.y, 1000);
                } else if (mouseButton->button == sf::Mouse::Button::Right) {
                    engine.removeParticlesAt(cursor.x, cursor.y);
                }

                const size_t particle_count = engine.particleCount();
                if (particle_count != particle_va.getVertexCount()) {
                    syncParticleBuffers(particle_count);
                }
            }
            else if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>()) {
                if (scroll->delta > 0) {
                    engine.increaseInteractionRadius(radius_step);
                } else {
                    engine.decreaseInteractionRadius(radius_step);
                }
            }
        }

        window.clear(sf::Color(5, 7, 16));
        engine.step(dt);

        renderClock.restart();
        Complex center_of_geometry{0.0, 0.0};
        {
            auto lock = engine.lockSources();
            const auto& sources = engine.sources();
            particle_count_snapshot = sources.size();

            if (particle_va.getVertexCount() != sources.size()) {
                syncParticleBuffers(sources.size());
            }

            float observed_max_speed = 1e-3f;

            for (size_t i = 0; i < sources.size(); ++i) {
                const sf::Vector2f p(
                    static_cast<float>(sources[i].position.real()),
                    static_cast<float>(sources[i].position.imag()));
                particle_va[i].position = p;

                const float vx = static_cast<float>(sources[i].velocity.real());
                const float vy = static_cast<float>(sources[i].velocity.imag());
                const float speed = std::sqrt(vx * vx + vy * vy);
                observed_max_speed = std::max(observed_max_speed, speed);
                const float speed_norm = clamp01(speed / std::max(1.0f, speed_scale));
                const std::size_t lut_idx = static_cast<std::size_t>(speed_norm * 255.0f);
                particle_va[i].color = speed_lut[lut_idx];

                center_of_geometry += sources[i].position;
            }

            if (!sources.empty()) {
                center_of_geometry /= static_cast<double>(sources.size());
            }

            speed_scale = 0.92f * speed_scale + 0.08f * std::max(40.0f, observed_max_speed * 1.6f);

            const std::size_t n = sources.size();
            const std::size_t glow_stride =
                (n > 200000) ? 8 :
                (n > 120000) ? 6 :
                (n > 70000)  ? 4 :
                (n > 30000)  ? 2 : 1;

            std::fill(glow_counts.begin(), glow_counts.end(), 0.0f);
            if (drawGlow && n > 0) {
                for (std::size_t i = 0; i < n; i += glow_stride) {
                    const float x = particle_va[i].position.x;
                    const float y = particle_va[i].position.y;
                    if (x < 0.0f || x >= static_cast<float>(screen_size) || y < 0.0f || y >= static_cast<float>(screen_size)) {
                        continue;
                    }

                    const int gx = static_cast<int>(x / glow_cell);
                    const int gy = static_cast<int>(y / glow_cell);
                    if (gx < 0 || gx >= glow_grid_dim || gy < 0 || gy >= glow_grid_dim) continue;

                    const std::size_t idx = static_cast<std::size_t>(gy * glow_grid_dim + gx);
                    glow_counts[idx] += static_cast<float>(glow_stride);
                }

                for (std::size_t i = 0; i < glow_cell_count; ++i) {
                    glow_smoothed[i] = 0.84f * glow_smoothed[i] + 0.16f * glow_counts[i];
                }
            } else {
                for (std::size_t i = 0; i < glow_cell_count; ++i) {
                    glow_smoothed[i] *= 0.86f;
                }
            }

            if (drawGlow) {
                auto idxAt = [glow_grid_dim](int x, int y) {
                    return static_cast<std::size_t>(y * glow_grid_dim + x);
                };

                for (int gy = 0; gy < glow_grid_dim; ++gy) {
                    for (int gx = 0; gx < glow_grid_dim; ++gx) {
                        const int x0 = std::max(0, gx - 1);
                        const int x1 = std::min(glow_grid_dim - 1, gx + 1);
                        const int y0 = std::max(0, gy - 1);
                        const int y1 = std::min(glow_grid_dim - 1, gy + 1);

                        const float c = glow_smoothed[idxAt(gx, gy)] * 4.0f;
                        const float n = glow_smoothed[idxAt(gx, y0)] * 2.0f;
                        const float s = glow_smoothed[idxAt(gx, y1)] * 2.0f;
                        const float w = glow_smoothed[idxAt(x0, gy)] * 2.0f;
                        const float e = glow_smoothed[idxAt(x1, gy)] * 2.0f;
                        const float nw = glow_smoothed[idxAt(x0, y0)];
                        const float ne = glow_smoothed[idxAt(x1, y0)];
                        const float sw = glow_smoothed[idxAt(x0, y1)];
                        const float se = glow_smoothed[idxAt(x1, y1)];

                        glow_blurred[idxAt(gx, gy)] = (c + n + s + w + e + nw + ne + sw + se) / 16.0f;
                    }
                }

                const float base_grid = 72.0f;
                const float gain = (static_cast<float>(glow_grid_dim) * static_cast<float>(glow_grid_dim)) / (base_grid * base_grid);
                for (std::size_t idx = 0; idx < glow_cell_count; ++idx) {
                    const float density = glow_blurred[idx];
                    const float response = 1.0f - std::exp(-density * 0.028f * gain * 0.85f);
                    const float warmth = clamp01(response * 1.15f);
                    const std::uint8_t a = static_cast<std::uint8_t>(120.0f * response);
                    const sf::Color glow_col = mixColor(
                        sf::Color(72, 140, 255, a),
                        sf::Color(255, 220, 150, a),
                        warmth);

                    const std::size_t v = idx * 6;
                    glow_va[v].color = glow_col;
                    glow_va[v + 1].color = glow_col;
                    glow_va[v + 2].color = glow_col;
                    glow_va[v + 3].color = glow_col;
                    glow_va[v + 4].color = glow_col;
                    glow_va[v + 5].color = glow_col;
                }
            }

            const std::size_t render_stride =
                (n > 140000) ? 6 :
                (n > 80000)  ? 4 :
                (n > 30000)  ? 2 : 1;

            std::size_t segment_count = 0;
            if (n > 0) {
                segment_count = (n + render_stride - 1) / render_stride;
            }
            streak_va.resize(segment_count * 2);

            std::size_t seg = 0;
            for (std::size_t i = 0; i < n; i += render_stride) {
                const sf::Vector2f p = particle_va[i].position;
                const float vx = static_cast<float>(sources[i].velocity.real());
                const float vy = static_cast<float>(sources[i].velocity.imag());
                const float speed = std::sqrt(vx * vx + vy * vy);
                const float inv_speed = 1.0f / std::max(speed, 1e-6f);

                const float streak_len = std::min(16.0f, std::max(0.9f, speed * 0.08f));
                const sf::Vector2f back(vx * inv_speed * streak_len, vy * inv_speed * streak_len);

                const std::size_t a = seg * 2;
                streak_va[a] = sf::Vertex{p, sf::Color(190, 232, 255, 44)};
                streak_va[a + 1] = sf::Vertex{sf::Vector2f(p.x - back.x, p.y - back.y), sf::Color(190, 232, 255, 0)};
                ++seg;
            }
        }

        const float dx = static_cast<float>(center_of_geometry.real()) - static_cast<float>(screen_size) * 0.5f;
        const float dy = static_cast<float>(center_of_geometry.imag()) - static_cast<float>(screen_size) * 0.5f;
        for (std::size_t i = 0; i < star_far_count; ++i) {
            stars_far[i].position.x = wrapToScreen(star_far_base[i].x - dx * 0.015f);
            stars_far[i].position.y = wrapToScreen(star_far_base[i].y - dy * 0.015f);
        }
        for (std::size_t i = 0; i < star_near_count; ++i) {
            stars_near[i].position.x = wrapToScreen(star_near_base[i].x - dx * 0.032f);
            stars_near[i].position.y = wrapToScreen(star_near_base[i].y - dy * 0.032f);
        }

        if (drawStars) {
            window.draw(stars_far);
            window.draw(stars_near);
        }

        if (drawBoxes) {
            auto boxes = engine.boxGeometries();
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

        sf::RenderStates additive_states;
        additive_states.blendMode = sf::BlendAdd;

        if (drawGlow) window.draw(glow_va, additive_states);
        if (drawStreaks) window.draw(streak_va, additive_states);
        window.draw(particle_va);

        tRenderMs = static_cast<float>(renderClock.getElapsedTime().asMicroseconds()) / 1000.0f;
        tFrameMs = static_cast<float>(frameTimer.getElapsedTime().asMicroseconds()) / 1000.0f;
        const auto& stats = engine.frameStats();

        if (stats.build_telemetry_updated_this_frame) {
            const float spike_threshold = std::max(5.0f, stats.ema_build_ms * 1.8f);
            const bool spike = stats.build_total_internal_ms > spike_threshold;
            std::cout << "BTEL"
                      << " frame=" << telemetry_frame
                      << " rebuild=" << (stats.rebuilt_forces_this_frame ? 1 : 0)
                      << " spike=" << (spike ? 1 : 0)
                      << " build_ms=" << stats.build_ms
                      << " build_internal_ms=" << stats.build_total_internal_ms
                      << " sort_ms=" << stats.build_sort_ms
                      << " node_lists_ms=" << stats.build_node_lists_ms
                      << " upward_ms=" << stats.build_upward_ms
                      << " downward_ms=" << stats.build_downward_ms
                      << " forces_ms=" << stats.build_forces_ms
                      << " nodes=" << stats.build_active_nodes
                      << " height=" << stats.build_tree_height
                      << " leaves=" << stats.build_leaf_nodes
                      << " max_leaf=" << stats.build_max_leaf_sources
                      << " list_w_evals=" << stats.build_list_w_force_evals
                      << " direct_pair_evals=" << stats.build_direct_pair_evals
                      << " near_total=" << stats.build_total_near_neighbors
                      << " interaction_total=" << stats.build_total_interaction_list
                      << " list_w_total=" << stats.build_total_list_w
                      << " list_x_total=" << stats.build_total_list_x
                      << " omp_max_threads=" << stats.build_omp_max_threads
                      << " omp_dynamic=" << (stats.build_omp_dynamic_enabled ? 1 : 0)
                      << " omp_threads_node_lists=" << stats.build_omp_threads_node_lists
                      << " omp_threads_upward=" << stats.build_omp_threads_upward
                      << " omp_threads_downward=" << stats.build_omp_threads_downward
                      << " omp_threads_forces=" << stats.build_omp_threads_forces
                      << " particles=" << particle_count_snapshot
                      << "\n";
        }

        // Update overlay at ~10 Hz
        if (canDrawText && uiTimer.getElapsedTime().asMilliseconds() >= 100) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2)
                << "frame: " << tFrameMs << " ms\n"
                << "integrate: " << stats.integrate_ms << " ms\n"
                << "buildTree: " << stats.build_ms << " ms"
                << " (ema " << stats.ema_build_ms << ", max " << stats.max_build_ms << ")\n"
                << "build phases [ms] s/l/u/d/f/t: "
                << stats.build_sort_ms << "/"
                << stats.build_node_lists_ms << "/"
                << stats.build_upward_ms << "/"
                << stats.build_downward_ms << "/"
                << stats.build_forces_ms << "/"
                << stats.build_total_internal_ms << "\n"
                << "build nodes/h/leaf/maxLeaf: "
                << stats.build_active_nodes << "/"
                << stats.build_tree_height << "/"
                << stats.build_leaf_nodes << "/"
                << stats.build_max_leaf_sources << "\n"
                << "build evals w/direct: "
                << stats.build_list_w_force_evals << "/"
                << stats.build_direct_pair_evals << "\n"
                << "render: " << tRenderMs << " ms\n"
                << "build every N: " << engine.rebuildEvery() << "\n"
                << "particles: " << particle_count_snapshot << "\n"
                << "interaction radius: " << static_cast<int>(engine.interactionRadius()) << " px\n"
                << "visual speed scale: " << speed_scale << "\n"
                << "visual toggles g/v/n/c/b: "
                << (drawGlow ? "1" : "0") << "/"
                << (drawStreaks ? "1" : "0") << "/"
                << (drawStars ? "1" : "0") << "/"
                << palette_index << "/"
                << (drawBoxes ? "1" : "0");
            overlay.setString(oss.str());
            controls_legend.setString(
                "Toggles  [G] glow " + std::string(drawGlow ? "on" : "off") +
                "   [V] streaks " + std::string(drawStreaks ? "on" : "off") +
                "   [N] stars " + std::string(drawStars ? "on" : "off") +
                "   [C] palette " + std::to_string(palette_index) +
                "   [B] boxes " + std::string(drawBoxes ? "on" : "off"));
            uiTimer.restart();
        }

        if (canDrawText) {
            window.draw(overlay);
            window.draw(controls_legend);
        }

        window.display();
        ++telemetry_frame;
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
