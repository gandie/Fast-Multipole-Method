#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "barnes_hut_tree.hpp"
#include "diagnostics.hpp"

namespace {

std::vector<fmm::Source> makeGridSources(int nx, int ny, double spacing, double origin_x, double origin_y) {
    std::vector<fmm::Source> sources;
    sources.reserve(static_cast<size_t>(nx * ny));

    for (int iy = 0; iy < ny; ++iy) {
        for (int ix = 0; ix < nx; ++ix) {
            const double x = origin_x + static_cast<double>(ix) * spacing;
            const double y = origin_y + static_cast<double>(iy) * spacing;
            const double charge = 1.0 + static_cast<double>((ix + iy) % 5) * 0.1;
            sources.emplace_back(x, y, charge);
        }
    }

    return sources;
}

}  // namespace

TEST_CASE("Barnes-Hut force approximation stays within conservative relative error", "[accuracy][barnes-hut]") {
    std::vector<fmm::Source> sources = makeGridSources(14, 10, 22.0, 150.0, 180.0);

    fmm::BhTree tree(sources, 4, 0.55);
    tree.buildTree();

    REQUIRE(tree.forces.size() == sources.size());

    const fmm::ErrorData error = fmm::evaluateSimulationError(sources, tree.forces, sources.size());
    REQUIRE(error.l2_relative_error < 0.20);
    REQUIRE(error.mean_absolute_error < 0.015);
}
