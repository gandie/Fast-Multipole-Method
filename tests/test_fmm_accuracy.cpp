#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <tuple>
#include <vector>
#include <omp.h>

#include "barnes_hut_tree.hpp"
#include "diagnostics.hpp"
#include "fmm_tree.hpp"

using Catch::Approx;

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

std::vector<fmm::Source> translateSources(const std::vector<fmm::Source>& sources, const Complex& shift) {
    std::vector<fmm::Source> shifted = sources;
    for (auto& source : shifted) {
        source.position += shift;
    }
    return shifted;
}

Complex rotate90(const Complex& z) {
    return Complex{-z.imag(), z.real()};
}

std::vector<fmm::Source> rotateSources90(const std::vector<fmm::Source>& sources) {
    std::vector<fmm::Source> rotated = sources;
    for (auto& source : rotated) {
        source.position = rotate90(source.position);
        source.velocity = rotate90(source.velocity);
    }
    return rotated;
}

struct ForceSnapshot {
    double x = 0.0;
    double y = 0.0;
    double q = 0.0;
    double fx = 0.0;
    double fy = 0.0;
};

std::vector<ForceSnapshot> captureSortedForceSnapshots(
    const std::vector<fmm::Source>& sources,
    const std::vector<Complex>& forces) {

    std::vector<ForceSnapshot> out;
    out.reserve(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        out.push_back(ForceSnapshot{
            sources[i].position.real(),
            sources[i].position.imag(),
            sources[i].q,
            forces[i].real(),
            forces[i].imag()});
    }

    std::sort(out.begin(), out.end(), [](const ForceSnapshot& a, const ForceSnapshot& b) {
        return std::tie(a.x, a.y, a.q) < std::tie(b.x, b.y, b.q);
    });
    return out;
}

struct OmpState {
    int max_threads = 1;
    int dynamic_enabled = 0;
};

struct OmpStateGuard {
    OmpState saved;
    explicit OmpStateGuard(OmpState state) : saved(state) {}
    ~OmpStateGuard() {
        omp_set_dynamic(saved.dynamic_enabled);
        omp_set_num_threads(saved.max_threads);
    }
};

OmpState captureOmpState() {
    return OmpState{omp_get_max_threads(), omp_get_dynamic()};
}

void requireSnapshotMatch(const std::vector<ForceSnapshot>& lhs,
                         const std::vector<ForceSnapshot>& rhs,
                         double abs_tol,
                         double rel_tol) {
    REQUIRE(lhs.size() == rhs.size());
    for (size_t i = 0; i < lhs.size(); ++i) {
        const auto& a = lhs[i];
        const auto& b = rhs[i];

        REQUIRE(a.x == Approx(b.x).margin(0.0));
        REQUIRE(a.y == Approx(b.y).margin(0.0));
        REQUIRE(a.q == Approx(b.q).margin(0.0));

        const double dx = std::abs(a.fx - b.fx);
        const double dy = std::abs(a.fy - b.fy);

        const double sx = std::max(std::abs(a.fx), std::abs(b.fx));
        const double sy = std::max(std::abs(a.fy), std::abs(b.fy));

        REQUIRE(dx <= abs_tol + rel_tol * sx);
        REQUIRE(dy <= abs_tol + rel_tol * sy);
    }
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

    const Complex f0 = tree.forces[0];
    const Complex f1 = tree.forces[1];

    REQUIRE(std::abs(f0) > 0.0);
    REQUIRE(std::abs(f1) > 0.0);

    constexpr double dx01 = 200.0 - 230.0;
    constexpr double dy01 = 200.0 - 210.0;
    constexpr double r2 = dx01 * dx01 + dy01 * dy01;

    const Complex expected_f0{-1.2 * dx01 / r2, -1.2 * dy01 / r2};
    const Complex expected_f1{-1.0 * (-dx01) / r2, -1.0 * (-dy01) / r2};

    REQUIRE(f0.real() == Approx(expected_f0.real()).margin(1e-12));
    REQUIRE(f0.imag() == Approx(expected_f0.imag()).margin(1e-12));
    REQUIRE(f1.real() == Approx(expected_f1.real()).margin(1e-12));
    REQUIRE(f1.imag() == Approx(expected_f1.imag()).margin(1e-12));
}

TEST_CASE("Barnes-Hut exposes box geometries after build", "[accuracy][barnes-hut][boxes]") {
    std::vector<fmm::Source> sources{{100.0, 100.0, 1.0}, {160.0, 130.0, 1.0}, {220.0, 190.0, 1.0}};

    fmm::BhTree tree(sources, 1, 0.8);
    tree.buildTree();

    const auto boxes = tree.getBoxGeometries();
    REQUIRE_FALSE(boxes.empty());
    REQUIRE(boxes.front().second > 0.0);
}

TEST_CASE("FMM overlapping particles keep finite near-field forces", "[accuracy][fmm][stability]") {
    std::vector<fmm::Source> sources{{200.0, 200.0, 1.0}, {200.0, 200.0, 1.2}, {240.0, 210.0, 1.0}};

    fmm::FmmTree tree(sources, 10, 8);
    tree.buildTree();

    REQUIRE(tree.forces.size() == sources.size());
    for (const Complex& f : tree.forces) {
        REQUIRE(std::isfinite(f.real()));
        REQUIRE(std::isfinite(f.imag()));
    }
}

TEST_CASE("FMM build is thread-policy invariant for topology and forces", "[accuracy][fmm][threading]") {
    const OmpState initial_omp = captureOmpState();
    OmpStateGuard guard(initial_omp);

    const int runtime_threads = omp_get_max_threads();
    if (runtime_threads < 2) {
        SUCCEED("Runtime exposes one OpenMP thread; multi-thread invariance check skipped.");
        return;
    }

    std::vector<fmm::Source> sources_serial = makeGridSources(29, 35, 7.0, 110.0, 140.0);
    std::vector<fmm::Source> sources_parallel = sources_serial;

    omp_set_dynamic(0);

    omp_set_num_threads(1);
    fmm::FmmTree serial_tree(sources_serial, 8, 10);
    serial_tree.buildTree();

    const int multi_threads = std::min(8, runtime_threads);
    omp_set_num_threads(multi_threads);
    fmm::FmmTree parallel_tree(sources_parallel, 8, 10);
    parallel_tree.buildTree();

    const fmm::BuildTelemetry& serial_tm = serial_tree.lastBuildTelemetry();
    const fmm::BuildTelemetry& parallel_tm = parallel_tree.lastBuildTelemetry();

    REQUIRE(serial_tm.active_nodes == parallel_tm.active_nodes);
    REQUIRE(serial_tm.tree_height == parallel_tm.tree_height);
    REQUIRE(serial_tm.leaf_nodes == parallel_tm.leaf_nodes);
    REQUIRE(serial_tm.max_leaf_sources == parallel_tm.max_leaf_sources);
    REQUIRE(serial_tm.total_near_neighbors == parallel_tm.total_near_neighbors);
    REQUIRE(serial_tm.total_interaction_list == parallel_tm.total_interaction_list);
    REQUIRE(serial_tm.total_list_w == parallel_tm.total_list_w);
    REQUIRE(serial_tm.total_list_x == parallel_tm.total_list_x);

    const auto serial_snapshots = captureSortedForceSnapshots(sources_serial, serial_tree.forces);
    const auto parallel_snapshots = captureSortedForceSnapshots(sources_parallel, parallel_tree.forces);

    constexpr double kAbsTol = 1e-12;
    constexpr double kRelTol = 1e-10;
    requireSnapshotMatch(serial_snapshots, parallel_snapshots, kAbsTol, kRelTol);

    const fmm::ErrorData serial_error =
        fmm::evaluateSimulationError(sources_serial, serial_tree.forces, sources_serial.size());
    const fmm::ErrorData parallel_error =
        fmm::evaluateSimulationError(sources_parallel, parallel_tree.forces, sources_parallel.size());

    REQUIRE(serial_error.l2_relative_error < 0.12);
    REQUIRE(parallel_error.l2_relative_error < 0.12);
    REQUIRE(serial_error.mean_absolute_error < 0.01);
    REQUIRE(parallel_error.mean_absolute_error < 0.01);
}

TEST_CASE("FMM remains physically accurate under repeated shuffled source order", "[accuracy][fmm][threading][oracle]") {
    const OmpState initial_omp = captureOmpState();
    OmpStateGuard guard(initial_omp);

    const int runtime_threads = omp_get_max_threads();
    if (runtime_threads < 2) {
        SUCCEED("Runtime exposes one OpenMP thread; shuffled multi-thread physics-oracle check skipped.");
        return;
    }

    omp_set_dynamic(0);

    std::vector<fmm::Source> base_sources = makeGridSources(20, 20, 12.0, 120.0, 160.0);
    std::mt19937 rng(20260823u);

    constexpr int kShuffles = 20;
    constexpr double kCrossRunAbsTol = 1e-12;
    constexpr double kCrossRunRelTol = 1e-10;
    constexpr double kExactAbsTol = 1e-12;
    constexpr double kExactRelTol = 1e-10;

    std::vector<ForceSnapshot> baseline_multi;

    for (int run = 0; run < kShuffles; ++run) {
        std::vector<fmm::Source> shuffled = base_sources;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        std::vector<fmm::Source> sources_serial = shuffled;
        std::vector<fmm::Source> sources_multi = shuffled;

        // Force a single-leaf direct-interaction configuration so this test is
        // a strict physics-oracle check against exact all-pairs reference.
        const size_t direct_leaf_cap = sources_serial.size() + 1;

        omp_set_num_threads(1);
        fmm::FmmTree serial_tree(sources_serial, direct_leaf_cap, 10);
        serial_tree.buildTree();

        omp_set_num_threads(std::min(8, runtime_threads));
        fmm::FmmTree multi_tree(sources_multi, direct_leaf_cap, 10);
        multi_tree.buildTree();

        REQUIRE(serial_tree.height == 0);
        REQUIRE(multi_tree.height == 0);

        const auto serial_snapshots = captureSortedForceSnapshots(sources_serial, serial_tree.forces);
        const auto multi_snapshots = captureSortedForceSnapshots(sources_multi, multi_tree.forces);

        requireSnapshotMatch(serial_snapshots, multi_snapshots, kCrossRunAbsTol, kCrossRunRelTol);

        if (baseline_multi.empty()) {
            baseline_multi = multi_snapshots;
        } else {
            requireSnapshotMatch(baseline_multi, multi_snapshots, kCrossRunAbsTol, kCrossRunRelTol);
        }

        const std::vector<Complex> exact_forces = fmm::computeExactForces(shuffled);
        const auto exact_snapshots = captureSortedForceSnapshots(shuffled, exact_forces);

        requireSnapshotMatch(multi_snapshots, exact_snapshots, kExactAbsTol, kExactRelTol);

        const fmm::ErrorData multi_error =
            fmm::evaluateSimulationError(sources_multi, multi_tree.forces, sources_multi.size());
        REQUIRE(multi_error.l2_relative_error < 1e-12);
        REQUIRE(multi_error.mean_absolute_error < 1e-12);
    }
}

TEST_CASE("FMM direct mode is translation invariant", "[accuracy][fmm][metamorphic][translation]") {
    std::vector<fmm::Source> base = makeGridSources(16, 14, 13.0, 180.0, 220.0);
    std::vector<fmm::Source> shifted = translateSources(base, Complex{137.5, -91.25});

    const size_t direct_leaf_cap = base.size() + 1;

    fmm::FmmTree base_tree(base, direct_leaf_cap, 10);
    base_tree.buildTree();
    fmm::FmmTree shifted_tree(shifted, direct_leaf_cap, 10);
    shifted_tree.buildTree();

    REQUIRE(base_tree.height == 0);
    REQUIRE(shifted_tree.height == 0);
    REQUIRE(base_tree.forces.size() == shifted_tree.forces.size());

    constexpr double kAbsTol = 1e-12;
    constexpr double kRelTol = 1e-10;
    for (size_t i = 0; i < base_tree.forces.size(); ++i) {
        const double dx = std::abs(base_tree.forces[i].real() - shifted_tree.forces[i].real());
        const double dy = std::abs(base_tree.forces[i].imag() - shifted_tree.forces[i].imag());

        const double sx = std::max(std::abs(base_tree.forces[i].real()), std::abs(shifted_tree.forces[i].real()));
        const double sy = std::max(std::abs(base_tree.forces[i].imag()), std::abs(shifted_tree.forces[i].imag()));

        REQUIRE(dx <= kAbsTol + kRelTol * sx);
        REQUIRE(dy <= kAbsTol + kRelTol * sy);
    }
}

TEST_CASE("FMM direct mode is 90-degree rotation equivariant", "[accuracy][fmm][metamorphic][rotation]") {
    std::vector<fmm::Source> base = makeGridSources(16, 14, 13.0, 180.0, 220.0);
    std::vector<fmm::Source> rotated = rotateSources90(base);

    const size_t direct_leaf_cap = base.size() + 1;

    fmm::FmmTree base_tree(base, direct_leaf_cap, 10);
    base_tree.buildTree();
    fmm::FmmTree rotated_tree(rotated, direct_leaf_cap, 10);
    rotated_tree.buildTree();

    REQUIRE(base_tree.height == 0);
    REQUIRE(rotated_tree.height == 0);
    REQUIRE(base_tree.forces.size() == rotated_tree.forces.size());

    constexpr double kAbsTol = 1e-12;
    constexpr double kRelTol = 1e-10;
    for (size_t i = 0; i < base_tree.forces.size(); ++i) {
        const Complex expected = rotate90(base_tree.forces[i]);

        const double dx = std::abs(expected.real() - rotated_tree.forces[i].real());
        const double dy = std::abs(expected.imag() - rotated_tree.forces[i].imag());

        const double sx = std::max(std::abs(expected.real()), std::abs(rotated_tree.forces[i].real()));
        const double sy = std::max(std::abs(expected.imag()), std::abs(rotated_tree.forces[i].imag()));

        REQUIRE(dx <= kAbsTol + kRelTol * sx);
        REQUIRE(dy <= kAbsTol + kRelTol * sy);
    }
}
