#include <SFML/Graphics.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <vector>
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

        const double radius_step = 5.0;

        // Reused draw buffers
        sf::VertexArray particle_va(sf::PrimitiveType::Points, engine.particleCount());
        for (size_t i = 0; i < particle_va.getVertexCount(); ++i) {
            particle_va[i].color = p_color;
        }

        sf::Text overlay(font, "", 20);
        overlay.setFillColor(sf::Color::White);
        overlay.setPosition({10.f, 8.f});

        float tRenderMs = 0.f;
        float tFrameMs = 0.f;
        size_t particle_count_snapshot = particle_va.getVertexCount();

        while (window.isOpen()) {
        frameTimer.restart();

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) window.close();
            else if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) window.close();
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
                    particle_va.resize(particle_count);
                    for (size_t i = 0; i < particle_count; ++i) {
                        particle_va[i].color = p_color;
                    }
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

        window.clear(sf::Color::Black);
        engine.step(dt);

        renderClock.restart();
        {
            auto lock = engine.lockSources();
            const auto& sources = engine.sources();
            particle_count_snapshot = sources.size();

            if (particle_va.getVertexCount() != sources.size()) {
                particle_va.resize(sources.size());
                for (size_t i = 0; i < sources.size(); ++i) {
                    particle_va[i].color = p_color;
                }
            }

            for (size_t i = 0; i < sources.size(); ++i) {
                particle_va[i].position = sf::Vector2f(
                    static_cast<float>(sources[i].position.real()),
                    static_cast<float>(sources[i].position.imag())
                );
            }
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

        window.draw(particle_va);

        tRenderMs = static_cast<float>(renderClock.getElapsedTime().asMicroseconds()) / 1000.0f;
        tFrameMs = static_cast<float>(frameTimer.getElapsedTime().asMicroseconds()) / 1000.0f;
        const auto& stats = engine.frameStats();

        // Update overlay at ~10 Hz
        if (canDrawText && uiTimer.getElapsedTime().asMilliseconds() >= 100) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2)
                << "frame: " << tFrameMs << " ms\n"
                << "integrate: " << stats.integrate_ms << " ms\n"
                << "buildTree: " << stats.build_ms << " ms"
                << " (ema " << stats.ema_build_ms << ", max " << stats.max_build_ms << ")\n"
                << "render: " << tRenderMs << " ms\n"
                << "build every N: " << engine.rebuildEvery() << "\n"
                << "particles: " << particle_count_snapshot << "\n"
                << "interaction radius: " << static_cast<int>(engine.interactionRadius()) << " px";
            overlay.setString(oss.str());
            uiTimer.restart();
        }

        if (canDrawText) {
            window.draw(overlay);
        }

        window.display();
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
