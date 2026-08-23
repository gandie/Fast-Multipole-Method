#ifndef FMM_TREE_H
#define FMM_TREE_H

#include "adaptive_quadtree.hpp"
#include "multipole_expansion.hpp"
#include "local_expansion.hpp"
#include <cassert>
#include <chrono>
#include <omp.h>

namespace fmm {

constexpr unsigned kMaxTreeDepth = 20;

struct BuildTelemetry {
    float sort_ms = 0.0f;
    float node_lists_ms = 0.0f;
    float upward_ms = 0.0f;
    float downward_ms = 0.0f;
    float forces_ms = 0.0f;
    float total_ms = 0.0f;

    std::size_t source_count = 0;
    std::size_t active_nodes = 0;
    std::size_t tree_height = 0;
    std::size_t leaf_nodes = 0;
    std::size_t max_leaf_sources = 0;
    std::size_t total_near_neighbors = 0;
    std::size_t total_interaction_list = 0;
    std::size_t total_list_w = 0;
    std::size_t total_list_x = 0;
    std::size_t list_w_force_evals = 0;
    std::size_t direct_pair_evals = 0;

    int omp_max_threads = 0;
    bool omp_dynamic_enabled = false;
    int omp_threads_node_lists = 0;
    int omp_threads_upward = 0;
    int omp_threads_downward = 0;
    int omp_threads_forces = 0;
};

struct DeferredUpdates {
    // target, source
    std::vector<std::pair<uint32_t, uint32_t>> deferred_near;
    std::vector<std::pair<uint32_t, uint32_t>> deferred_W;   
    
    void reserve_space (size_t capacity) {
        deferred_near.reserve(capacity);
        deferred_W.reserve(capacity);
    }
};

    struct FmmNode : public BaseNode {
        std::vector<uint32_t> near_neighbors; // list U
        std::vector<uint32_t> interaction_list; // list V
        std::vector<uint32_t> list_W;
        std::vector<uint32_t> list_X;

        MultipoleExpansion multipole_expansion;
        LocalExpansion local_expansion;
 
        uint32_t start_id = 0;
        uint32_t num_sources = 0;

        FmmNode() : BaseNode() {}
    };

class FmmTree : public QuadTree<FmmNode> {
public:
    std::vector<Source> &sources;
    std::vector<std::vector<uint32_t>> level_indices;

    size_t max_sources_per_leaf;
    int expansion_order;
    Complex center{500.0, 500.0};
    double boundary_radius = 450.0;

    std::vector<Complex> forces;

    const BuildTelemetry& lastBuildTelemetry() const noexcept {
        return last_build_;
    }

    FmmTree(std::vector<Source> &src, size_t max_s_p_l, int p) 
        : sources(src), max_sources_per_leaf(max_s_p_l), expansion_order(p) 
    {
        SeriesExpansion::binom_table.init(expansion_order * 2);
    }

    void buildTree () {
        using Clock = std::chrono::high_resolution_clock;

        last_build_ = BuildTelemetry{};
        last_build_.source_count = sources.size();
        last_build_.omp_max_threads = omp_get_max_threads();
        last_build_.omp_dynamic_enabled = omp_get_dynamic() != 0;
        last_build_.omp_threads_node_lists = 1;
        last_build_.omp_threads_upward = 1;
        last_build_.omp_threads_downward = 1;
        last_build_.omp_threads_forces = 1;
        if (sources.empty()) return;

        const auto build_start = Clock::now();

        constexpr double kMinBoxLength = 1e-3;

        arena.resize(sources.size() * 2);
        active_nodes = 0;

        level_indices.clear();

        auto [l_bound, u_bound] = getDataRange(sources);

        double box_len = std::max(u_bound.real() - l_bound.real(), u_bound.imag() - l_bound.imag());
        box_len = std::max(box_len, kMinBoxLength);
        Complex center((l_bound.real() + u_bound.real()) / 2.0, (l_bound.imag() + u_bound.imag()) / 2.0);

        bool is_leaf = (sources.size() <= max_sources_per_leaf);
        root_id = allocateNode(center, box_len, 0, NULL_NODE, is_leaf);

        arena[root_id].start_id = 0;
        arena[root_id].num_sources = static_cast<uint32_t>(sources.size());

        level_indices.push_back({root_id});

        if (is_leaf) { // root is small enough
            this->height = 0;
            // For a single-leaf tree, near interactions are sourced from the root itself.
            arena[root_id].near_neighbors.push_back(root_id);

            const auto phase_start = Clock::now();
            last_build_.omp_threads_forces = computeForces();

            last_build_.forces_ms = static_cast<float>(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;
            finalizeBuildTelemetry(build_start);
            return;
        }

        auto phase_start = Clock::now();
        sortTree(root_id); // in place sort
        last_build_.sort_ms = static_cast<float>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

        // forces.assign(sources.size(), Complex{0.0, 0.0}); 

        phase_start = Clock::now();
        int max_node_lists_threads = 1;
        for (const auto& level : level_indices) {
            const int phase_threads = chooseThreadCountForWork(level.size(), 64);
            max_node_lists_threads = std::max(max_node_lists_threads, phase_threads);

            if (thread_notepads_.size() != static_cast<size_t>(phase_threads)) {
                thread_notepads_.assign(static_cast<size_t>(phase_threads), DeferredUpdates{});
            }

            auto& thread_notepads = thread_notepads_;

            size_t estimated = (level.size() / static_cast<size_t>(phase_threads)) / 5 + 10;

            for (auto& notepad : thread_notepads) {
                notepad.deferred_near.clear();
                notepad.deferred_W.clear();
                notepad.reserve_space(estimated);
            }

            if (phase_threads == 1) {
                for (size_t i = 0; i < level.size(); ++i) {
                    computeNodeLists(level[i], thread_notepads[0]);
                }
            } else {
                #pragma omp parallel for num_threads(phase_threads) schedule(dynamic, 64)
                for (size_t i = 0; i < level.size(); ++i) {
                    int thread_id = omp_get_thread_num();
                    computeNodeLists(level[i], thread_notepads[thread_id]);
                }
            }

            for (const auto& notepad : thread_notepads) {
                for (const auto& push : notepad.deferred_near) 
                    arena[push.first].near_neighbors.push_back(push.second);
                for (const auto& push : notepad.deferred_W) 
                    arena[push.first].list_W.push_back(push.second);
            }
        }
        last_build_.omp_threads_node_lists = max_node_lists_threads;
        last_build_.node_lists_ms = static_cast<float>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

        phase_start = Clock::now();
        last_build_.omp_threads_upward = upwardPass();
        last_build_.upward_ms = static_cast<float>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

        phase_start = Clock::now();
        last_build_.omp_threads_downward = downwardPass();
        last_build_.downward_ms = static_cast<float>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

        phase_start = Clock::now();
        last_build_.omp_threads_forces = computeForces();
        last_build_.forces_ms = static_cast<float>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - phase_start).count()) / 1000.0f;

        finalizeBuildTelemetry(build_start);
    }

    std::vector<std::pair<Complex, double>> getBoxGeometries() {
        std::vector<std::pair<Complex, double>> boxes;
        BFS([this, &boxes](uint32_t node_id) {
            boxes.push_back({arena[node_id].center, arena[node_id].box_length});
        });
        return boxes;
    }

private:
    std::size_t active_nodes = 0;
    std::vector<DeferredUpdates> thread_notepads_;
    std::vector<uint32_t> particle_to_leaf_;
    BuildTelemetry last_build_;

    int chooseThreadCountForWork(std::size_t work_items, std::size_t min_items_per_thread) const noexcept {
        if (work_items == 0) return 1;

        const int max_threads = std::max(1, last_build_.omp_max_threads);
        if (max_threads == 1) return 1;
        if (work_items < min_items_per_thread) return 1;

        const std::size_t scaled = (work_items + min_items_per_thread - 1) / min_items_per_thread;
        const int candidate = static_cast<int>(std::min<std::size_t>(scaled, static_cast<std::size_t>(max_threads)));
        return std::max(1, candidate);
    }

    void finalizeBuildTelemetry(const std::chrono::high_resolution_clock::time_point& build_start) {
        last_build_.total_ms = static_cast<float>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - build_start)
                .count()) / 1000.0f;

        last_build_.active_nodes = active_nodes;
        last_build_.tree_height = this->height;

        std::size_t leaf_nodes = 0;
        std::size_t max_leaf_sources = 0;
        std::size_t total_near_neighbors = 0;
        std::size_t total_interaction_list = 0;
        std::size_t total_list_w = 0;
        std::size_t total_list_x = 0;

        for (std::size_t id = 0; id < active_nodes; ++id) {
            const FmmNode& node = arena[id];
            total_near_neighbors += node.near_neighbors.size();
            total_interaction_list += node.interaction_list.size();
            total_list_w += node.list_W.size();
            total_list_x += node.list_X.size();

            if (node.is_leaf && node.num_sources > 0) {
                ++leaf_nodes;
                max_leaf_sources = std::max(max_leaf_sources, static_cast<std::size_t>(node.num_sources));
            }
        }

        last_build_.leaf_nodes = leaf_nodes;
        last_build_.max_leaf_sources = max_leaf_sources;
        last_build_.total_near_neighbors = total_near_neighbors;
        last_build_.total_interaction_list = total_interaction_list;
        last_build_.total_list_w = total_list_w;
        last_build_.total_list_x = total_list_x;
    }

    uint32_t allocateNode (Complex center, double box_len, size_t level, uint32_t parent, bool is_leaf) {
        uint32_t id;
        #pragma omp atomic capture
        {
            id = active_nodes;
            active_nodes++;
        }
        
        arena[id].center = center;
        arena[id].box_length = box_len;
        arena[id].level = level;
        arena[id].parent = parent;
        arena[id].is_leaf = is_leaf;
        arena[id].children.fill(NULL_NODE);
        arena[id].near_neighbors.clear();
        arena[id].interaction_list.clear();
        arena[id].list_W.clear();
        arena[id].list_X.clear();
        
        arena[id].local_expansion = LocalExpansion(center, expansion_order);
        arena[id].multipole_expansion = MultipoleExpansion();
        arena[id].multipole_expansion.center = center;
        arena[id].multipole_expansion.order = expansion_order;
        arena[id].multipole_expansion.coefficients.fill(Complex{0.0, 0.0});
        
        return id;
    }

    void sortTree (uint32_t initial_root) {
        unsigned depth = 0;
        bool subdivide = true;
        std::vector<uint32_t> prev_internal_nodes = {initial_root};
        while (subdivide) {
            depth++;

            size_t max_children = prev_internal_nodes.size() * 4;
            std::vector<uint32_t> next_level(max_children, NULL_NODE);
            std::vector<uint32_t> next_internal_nodes(max_children, NULL_NODE);

            for (size_t i = 0; i < prev_internal_nodes.size(); i++) {
                uint32_t parent_id = prev_internal_nodes[i];

                double cx = arena[parent_id].center.real();
                double cy = arena[parent_id].center.imag();
                double child_len = arena[parent_id].box_length / 2.0;

                uint32_t start_id = arena[parent_id].start_id;
                uint32_t count = arena[parent_id].num_sources;

                auto begin = sources.begin() + start_id;
                auto end = begin + count;
                auto mid_x  = std::partition(begin, end,   [cx](const Source &s) { return s.position.real() < cx; });
                auto mid_y1 = std::partition(begin, mid_x, [cy](const Source &s) { return s.position.imag() < cy; });
                auto mid_y2 = std::partition(mid_x, end,   [cy](const Source &s) { return s.position.imag() < cy; });

                std::array<uint32_t, 4> q_counts = {
                    static_cast<uint32_t>(std::distance(begin, mid_y1)),
                    static_cast<uint32_t>(std::distance(mid_y1, mid_x)),
                    static_cast<uint32_t>(std::distance(mid_x, mid_y2)),
                    static_cast<uint32_t>(std::distance(mid_y2, end))
                };
                
                uint32_t child_start_id = start_id;

                for (size_t c = 0; c < 4; c++) {
                    uint32_t c_count = q_counts[c];
                    if (c_count > 0)  {
                        Complex child_center = arena[parent_id].center + child_directions[c] * (child_len / 2.0);
                        bool is_leaf = (c_count <= max_sources_per_leaf) || (depth >= kMaxTreeDepth);

                        uint32_t child_id = allocateNode(child_center, child_len, depth, parent_id, is_leaf);

                        arena[child_id].start_id = child_start_id;
                        arena[child_id].num_sources = c_count;
                        arena[parent_id].children[c] = child_id;

                        next_level[i * 4 + c] = child_id;

                        if (!is_leaf) next_internal_nodes[i * 4 + c] = child_id;
                    }
                    child_start_id += c_count;
                }

            }

            next_level.erase(std::remove(next_level.begin(), next_level.end(), NULL_NODE), next_level.end());
            next_internal_nodes.erase(std::remove(next_internal_nodes.begin(), next_internal_nodes.end(), NULL_NODE), next_internal_nodes.end());
            
            if (!next_level.empty()) level_indices.push_back(next_level);
            subdivide = !next_internal_nodes.empty();
            prev_internal_nodes = std::move(next_internal_nodes);
        }
        this->height = depth;
    }

    

    void computeNodeLists (uint32_t node_id, DeferredUpdates &notepad) {
        FmmNode &node = arena[node_id];
        uint32_t parent_id = node.parent;

        // if node is root, itself is only NN
        if (parent_id == NULL_NODE) {
            node.near_neighbors.push_back(node_id);
            return;
        }

        for (uint32_t p_neighbor_id : arena[parent_id].near_neighbors) {
            FmmNode &p_neighbor = arena[p_neighbor_id];
            if (p_neighbor.is_leaf) { // Parent neighbor is childless.
                // If adjacent, then obviously NN. If ifself childless, also add to neighbor's NN
                if (node.adjacent(p_neighbor)) {
                    node.near_neighbors.push_back(p_neighbor_id);
                    if (node.is_leaf) // Also reciprocate on the smaller level
                        notepad.deferred_near.emplace_back(p_neighbor_id, node_id);
                } else {
                    // W only when childless, contains all descendants of colleagues w/
                    // parents adjacent but not themselves adjacent
                    // X stores all cells that have this cell (node_id) as part of W list
                    node.list_X.push_back(p_neighbor_id);
                    notepad.deferred_W.emplace_back(p_neighbor_id, node_id);
                }
            } else {
                for (int c = 0; c < 4; c++) {
                    uint32_t child_id = p_neighbor.children[c];
                    if (child_id == NULL_NODE) continue;

                    if (node.adjacent(arena[child_id])) {
                        // If childless, only add if other is childless. Otherwise just add since neighbor
                        if (!node.is_leaf || arena[child_id].is_leaf)
                            node.near_neighbors.push_back(child_id);
                    } else {
                        // Well-separated, children of parent
                        node.interaction_list.push_back(child_id);
                    }
                }
            }
        }
    }

    int upwardPass () {
        int max_threads_used = 1;
        for (int depth = this->height; depth >= 0; depth--) {
            const auto &level = level_indices[depth];
            const int phase_threads = chooseThreadCountForWork(level.size(), 128);
            max_threads_used = std::max(max_threads_used, phase_threads);

            if (phase_threads == 1) {
                for (size_t i = 0; i < level.size(); i++) {
                    uint32_t node_id = level[i];
                    FmmNode &node = arena[node_id];

                    if (node.is_leaf) {
                        auto begin = sources.begin() + node.start_id;
                        auto end = begin + node.num_sources;
                        node.multipole_expansion = MultipoleExpansion(node.center, expansion_order, begin, end);
                    } else {
                        std::array<const MultipoleExpansion*, 4> child_me;
                        size_t child_count = 0;

                        for (size_t c = 0; c < 4; c++) {
                            uint32_t child_id = node.children[c];
                            if (child_id == NULL_NODE) continue;
                            child_me[child_count++] = &arena[child_id].multipole_expansion;
                        }

                        node.multipole_expansion = MultipoleExpansion(
                            node.center,
                            std::span<const MultipoleExpansion* const>(child_me.data(), child_count)
                        );
                    }
                }
            } else {
                #pragma omp parallel for num_threads(phase_threads) schedule(static)
                for (size_t i = 0; i < level.size(); i++) {
                    uint32_t node_id = level[i];
                    FmmNode &node = arena[node_id];

                    if (node.is_leaf) {
                        auto begin = sources.begin() + node.start_id;
                        auto end = begin + node.num_sources;
                        node.multipole_expansion = MultipoleExpansion(node.center, expansion_order, begin, end);
                    } else {
                        std::array<const MultipoleExpansion*, 4> child_me;
                        size_t child_count = 0;

                        for (size_t c = 0; c < 4; c++) {
                            uint32_t child_id = node.children[c];
                            if (child_id == NULL_NODE) continue;
                            child_me[child_count++] = &arena[child_id].multipole_expansion;
                        }

                        node.multipole_expansion = MultipoleExpansion(
                            node.center,
                            std::span<const MultipoleExpansion* const>(child_me.data(), child_count)
                        );
                    }
                }
            }
        }
        return max_threads_used;
    }

    int downwardPass () {
        int max_threads_used = 1;
        for (size_t depth = 2; depth <= this->height; depth++) {
            const auto &level = level_indices[depth];
            const int phase_threads = chooseThreadCountForWork(level.size(), 128);
            max_threads_used = std::max(max_threads_used, phase_threads);

            if (phase_threads == 1) {
                for (size_t i = 0; i < level.size(); i++) {
                    uint32_t node_id = level[i];
                    FmmNode &node = arena[node_id];

                    // Multipole to local, V_b bounded by 32
                    std::array<const MultipoleExpansion*, 32> incoming;
                    size_t incoming_count = 0;
                    for (uint32_t v_id : node.interaction_list) {
                        incoming[incoming_count++] = &arena[v_id].multipole_expansion;
                    }

                    if (incoming_count > 0) {
                        node.local_expansion += LocalExpansion(
                            node.center,
                            std::span<const MultipoleExpansion* const>(incoming.data(), incoming_count)
                        );
                    }

                    // Particle to Local (X_b)
                    for (uint32_t x_id : node.list_X) {
                        auto begin = sources.begin() + arena[x_id].start_id;
                        auto end = begin + arena[x_id].num_sources;
                        node.local_expansion += LocalExpansion(node.center, expansion_order, begin, end);
                    }

                    // Local to Local (parent expansions)
                    uint32_t parent_id = node.parent;
                    if (parent_id != NULL_NODE) {
                        node.local_expansion += LocalExpansion(node.center, arena[parent_id].local_expansion);
                    }
                }
            } else {
                #pragma omp parallel for num_threads(phase_threads) schedule(static)
                for (size_t i = 0; i < level.size(); i++) {
                    uint32_t node_id = level[i];
                    FmmNode &node = arena[node_id];

                    // Multipole to local, V_b bounded by 32
                    std::array<const MultipoleExpansion*, 32> incoming;
                    size_t incoming_count = 0;
                    for (uint32_t v_id : node.interaction_list) {
                        incoming[incoming_count++] = &arena[v_id].multipole_expansion;
                    }

                    if (incoming_count > 0) {
                        node.local_expansion += LocalExpansion(
                            node.center,
                            std::span<const MultipoleExpansion* const>(incoming.data(), incoming_count)
                        );
                    }

                    // Particle to Local (X_b)
                    for (uint32_t x_id : node.list_X) {
                        auto begin = sources.begin() + arena[x_id].start_id;
                        auto end = begin + arena[x_id].num_sources;
                        node.local_expansion += LocalExpansion(node.center, expansion_order, begin, end);
                    }

                    // Local to Local (parent expansions)
                    uint32_t parent_id = node.parent;
                    if (parent_id != NULL_NODE) {
                        node.local_expansion += LocalExpansion(node.center, arena[parent_id].local_expansion);
                    }
                }
            }
        }
        return max_threads_used;
    }

    int computeForces () {
        constexpr double kMinDistanceSq = 1.0;

        forces.assign(sources.size(), Complex{0.0, 0.0});

        particle_to_leaf_.assign(sources.size(), NULL_NODE);
        
        for (uint32_t id = 0; id < active_nodes; id++) {
            if (arena[id].is_leaf && arena[id].num_sources > 0) {
                for (uint32_t t_id = arena[id].start_id; t_id < arena[id].start_id + arena[id].num_sources; t_id++) {
                    particle_to_leaf_[t_id] = id;
                }
            }
        }

        std::size_t list_w_force_evals = 0;
        std::size_t direct_pair_evals = 0;
        const int phase_threads = chooseThreadCountForWork(sources.size(), 256);

        if (phase_threads == 1) {
            for (size_t t_id = 0; t_id < sources.size(); t_id++) {
                uint32_t leaf_id = particle_to_leaf_[t_id];
                if (leaf_id == NULL_NODE) continue;

                FmmNode &leaf = arena[leaf_id];
                const Source &target = sources[t_id];
                Complex total_force{0.0, 0.0};

                total_force += leaf.local_expansion.evaluateForce(target.position);

                for (uint32_t w_id : leaf.list_W) {
                    ++list_w_force_evals;
                    total_force += arena[w_id].multipole_expansion.evaluateForce(target.position);
                }

                for (uint32_t u_id : leaf.near_neighbors) {
                    FmmNode &n_leaf = arena[u_id];

                    for (uint32_t s_id = n_leaf.start_id; s_id < n_leaf.start_id + n_leaf.num_sources; s_id++) {
                        if (t_id == s_id) continue;

                        const Source &src = sources[s_id];
                        double dx = target.position.real() - src.position.real();
                        double dy = target.position.imag() - src.position.imag();

                        double r2 = dx * dx + dy * dy;
                        if (r2 < kMinDistanceSq) r2 = kMinDistanceSq;

                        ++direct_pair_evals;
                        total_force += Complex{-src.q * dx / r2, -src.q * dy / r2};
                    }
                }

                forces[t_id] = total_force;
            }
        } else {
            #pragma omp parallel for num_threads(phase_threads) schedule(dynamic, 64) reduction(+:list_w_force_evals,direct_pair_evals)
            for (size_t t_id = 0; t_id < sources.size(); t_id++) {
                uint32_t leaf_id = particle_to_leaf_[t_id];
                if (leaf_id == NULL_NODE) continue;

                FmmNode &leaf = arena[leaf_id];
                const Source &target = sources[t_id];
                Complex total_force{0.0, 0.0};

                total_force += leaf.local_expansion.evaluateForce(target.position);

                for (uint32_t w_id : leaf.list_W) {
                    ++list_w_force_evals;
                    total_force += arena[w_id].multipole_expansion.evaluateForce(target.position);
                }

                for (uint32_t u_id : leaf.near_neighbors) {
                    FmmNode &n_leaf = arena[u_id];

                    for (uint32_t s_id = n_leaf.start_id; s_id < n_leaf.start_id + n_leaf.num_sources; s_id++) {
                        if (t_id == s_id) continue;

                        const Source &src = sources[s_id];
                        double dx = target.position.real() - src.position.real();
                        double dy = target.position.imag() - src.position.imag();

                        double r2 = dx * dx + dy * dy;
                        if (r2 < kMinDistanceSq) r2 = kMinDistanceSq;

                        ++direct_pair_evals;
                        total_force += Complex{-src.q * dx / r2, -src.q * dy / r2};
                    }
                }

                forces[t_id] = total_force;
            }
        }

        last_build_.list_w_force_evals = list_w_force_evals;
        last_build_.direct_pair_evals = direct_pair_evals;
        return phase_threads;
    }
};

}

#endif