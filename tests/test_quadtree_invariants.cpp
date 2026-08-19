#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "adaptive_quadtree.hpp"
#include "barnes_hut_tree.hpp"
#include "fmm_tree.hpp"

using Catch::Approx;

namespace {

std::vector<fmm::Source> makeInvariantFixture() {
    std::vector<fmm::Source> sources;
    for (int i = 0; i < 24; ++i) {
        const double x = 120.0 + static_cast<double>(i % 6) * 40.0;
        const double y = 160.0 + static_cast<double>(i / 6) * 35.0;
        sources.emplace_back(x, y, 1.0);
    }
    return sources;
}

template <typename TreeT>
void verifyLeafPartitionAndCoverage(const TreeT& tree, size_t source_count) {
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    uint32_t total = 0;

    for (const auto& level : tree.level_indices) {
        for (uint32_t id : level) {
            const auto& node = tree.arena[id];
            if (!node.is_leaf || node.num_sources == 0) continue;

            const uint32_t begin = node.start_id;
            const uint32_t end = node.start_id + node.num_sources;
            ranges.emplace_back(begin, end);
            total += node.num_sources;
        }
    }

    REQUIRE(total == source_count);
    REQUIRE_FALSE(ranges.empty());

    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    REQUIRE(ranges.front().first == 0U);
    REQUIRE(ranges.back().second == source_count);

    for (size_t i = 1; i < ranges.size(); ++i) {
        REQUIRE(ranges[i - 1].second == ranges[i].first);
    }
}

}  // namespace

TEST_CASE("getDataRange returns padded bounds for non-empty input", "[quadtree][range]") {
    std::vector<fmm::Source> points{
        {1.0, 5.0, 1.0},
        {3.0, 2.0, 1.0},
        {7.0, 9.0, 1.0},
        {4.0, 1.0, 1.0}
    };

    auto [lo, hi] = fmm::QuadTree<fmm::BaseNode>::getDataRange(points);

    REQUIRE(lo.real() == Approx(0.99999));
    REQUIRE(lo.imag() == Approx(0.99999));
    REQUIRE(hi.real() == Approx(7.00001));
    REQUIRE(hi.imag() == Approx(9.00001));
}

TEST_CASE("getDataRange handles empty input", "[quadtree][range]") {
    std::vector<fmm::Source> points;

    auto [lo, hi] = fmm::QuadTree<fmm::BaseNode>::getDataRange(points);

    REQUIRE(lo.real() == Approx(0.0));
    REQUIRE(lo.imag() == Approx(0.0));
    REQUIRE(hi.real() == Approx(0.0));
    REQUIRE(hi.imag() == Approx(0.0));
}

TEST_CASE("BaseNode adjacency identifies touching and separated boxes", "[quadtree][adjacency]") {
    fmm::BaseNode a;
    a.center = {0.0, 0.0};
    a.box_length = 2.0;

    fmm::BaseNode touching;
    touching.center = {2.8, 0.0};
    touching.box_length = 2.0;

    fmm::BaseNode far;
    far.center = {3.2, 0.0};
    far.box_length = 2.0;

    REQUIRE(a.adjacent(touching));
    REQUIRE_FALSE(a.adjacent(far));
}

TEST_CASE("Barnes-Hut tree leaves partition the source array without gaps", "[invariant][barnes-hut]") {
    std::vector<fmm::Source> sources = makeInvariantFixture();

    fmm::BhTree tree(sources, 3, 0.7);
    tree.buildTree();

    verifyLeafPartitionAndCoverage(tree, sources.size());
    REQUIRE(tree.forces.size() == sources.size());
}

TEST_CASE("FMM tree leaves partition the source array without gaps", "[invariant][fmm]") {
    std::vector<fmm::Source> sources = makeInvariantFixture();

    fmm::FmmTree tree(sources, 3, 8);
    tree.buildTree();

    verifyLeafPartitionAndCoverage(tree, sources.size());
    REQUIRE(tree.forces.size() == sources.size());
}

TEST_CASE("BFS returns immediately when root is null", "[quadtree][bfs]") {
    fmm::QuadTree<fmm::BaseNode> tree;

    int visited = 0;
    tree.BFS([&visited](uint32_t) {
        visited++;
    });

    REQUIRE(visited == 0);
}

TEST_CASE("BFS visits nodes level-by-level", "[quadtree][bfs]") {
    fmm::QuadTree<fmm::BaseNode> tree;
    tree.arena.resize(4);
    tree.root_id = 0;

    tree.arena[0].children = {1, 2, fmm::NULL_NODE, fmm::NULL_NODE};
    tree.arena[1].children = {3, fmm::NULL_NODE, fmm::NULL_NODE, fmm::NULL_NODE};
    tree.arena[2].children = {fmm::NULL_NODE, fmm::NULL_NODE, fmm::NULL_NODE, fmm::NULL_NODE};
    tree.arena[3].children = {fmm::NULL_NODE, fmm::NULL_NODE, fmm::NULL_NODE, fmm::NULL_NODE};

    std::vector<uint32_t> visit_order;
    tree.BFS([&visit_order](uint32_t node_id) {
        visit_order.push_back(node_id);
    });

    REQUIRE(visit_order.size() == 4);
    REQUIRE(visit_order[0] == 0);
    REQUIRE(visit_order[1] == 1);
    REQUIRE(visit_order[2] == 2);
    REQUIRE(visit_order[3] == 3);
}
