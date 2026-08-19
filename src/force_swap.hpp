#ifndef FORCE_SWAP_HPP
#define FORCE_SWAP_HPP

#include <vector>

#include "adaptive_quadtree.hpp"

namespace sim {

inline bool trySwapPendingForces(std::vector<Complex>& current_forces, std::vector<Complex>& pending_forces) {
    if (pending_forces.empty()) return false;

    if (pending_forces.size() != current_forces.size()) {
        pending_forces.clear();
        return false;
    }

    current_forces = std::move(pending_forces);
    pending_forces.clear();
    return true;
}

}  // namespace sim

#endif
