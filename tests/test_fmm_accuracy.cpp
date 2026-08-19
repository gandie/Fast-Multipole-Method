#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "barnes_hut_tree.hpp"
#include "diagnostics.hpp"
#include "fmm_tree.hpp"

namespace {

std::vector<fmm::Source> makeGridSources(int nx, int ny, double spacing, double origin_x, double origin_y) {
    std::vector<fmm::Source> sources;
    sources.reserve(static_cast<size_t>(nx * ny));

    for (int iy = 0; iy < ny; ++iy) {
        for (int ix = 0; ix < nx; ++ix) {
            const double x = origin_x + static_cast<double>(ix) * spacing;
            const double y = origin_y + static_cast<double>(iy) * spacing;
            const double charge = 1.0 + static_cast<double>((ix + 2 * iy) % 7) * 0.05;
            sources.emplace_back(x, y, charge);
        }
    }

    return sources;
}

}  // namespace

TEST_CASE("FMM force approximation stays within conservative relative error", "[accuracy][fmm]") {
    std::vector<fmm::Source> sources = makeGridSources(14, 10, 22.0, 150.0, 180.0);

    fmm::FmmTree tree(sources, 6, 10);
    tree.buildTree();

    REQUIRE(tree.forces.size() == sources.size());

    const fmm::ErrorData error = fmm::evaluateSimulationError(sources, tree.forces, sources.size());
    REQUIRE(error.l2_relative_error < 0.12);
    REQUIRE(error.mean_absolute_error < 0.01);
}

TEST_CASE("FMM single-leaf build computes forces and exposes one box", "[accuracy][fmm][boxes]") {
    std::vector<fmm::Source> sources{{200.0, 200.0, 1.0}, {230.0, 210.0, 1.2}};

    fmm::FmmTree tree(sources, 10, 8);
    tree.buildTree();

    REQUIRE(tree.height == 0);
    REQUIRE(tree.forces.size() == sources.size());

    const auto boxes = tree.getBoxGeometries();
    REQUIRE(boxes.size() == 1);
    REQUIRE(boxes[0].second > 0.0);
}

TEST_CASE("Barnes-Hut exposes box geometries after build", "[accuracy][barnes-hut][boxes]") {
    std::vector<fmm::Source> sources{{100.0, 100.0, 1.0}, {160.0, 130.0, 1.0}, {220.0, 190.0, 1.0}};

    fmm::BhTree tree(sources, 1, 0.8);
    tree.buildTree();

    const auto boxes = tree.getBoxGeometries();
    REQUIRE_FALSE(boxes.empty());
    REQUIRE(boxes.front().second > 0.0);
}
