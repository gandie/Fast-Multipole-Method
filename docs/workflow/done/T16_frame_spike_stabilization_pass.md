---
agent_evaluation:
  version: 1
  evaluator: human_operator
  evaluated_at: YYYY-MM-DD
  verdict: accepted
  would_delegate_similar_again: true

  score_scale:
    min: 1
    max: 5
    meaning:
      1: poor
      3: acceptable
      5: excellent

  outcome:
    correctness:
    scope_discipline:
    validation_trust:

  collaboration:
    ambiguity_handling:
    operator_load:
    trust_delta:

  notes: >
    HUMAN NOTES
---

# T16 Frame Spike Stabilization Pass

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
With synchronous per-frame rebuild enabled (`rebuild-every=1`), runtime still exhibits large frame-time spikes. A stabilization pass is needed to reduce numerical and scheduling volatility that amplifies rebuild-time variance.

## Objective
Reduce high-magnitude frame-time spikes by hardening FMM near-field numerics and reducing avoidable rebuild-time jitter.

## Scope
- Add minimum root-box safety floor for degenerate particle clouds
- Add minimum near-field distance floor to avoid singular-force blowups
- Reuse rebuild scratch buffers to reduce per-frame allocation jitter
- Use deterministic OpenMP scheduling for force pass to reduce timing variance
- Add regression coverage for finite forces under overlapping-particle edge cases

## Non-Goals
- Changing synchronous force-refresh cadence semantics
- Rewriting the integrator
- Broad renderer/event-loop profiling work

## Target Files
- src/adaptive_quadtree.hpp
- src/fmm_tree.hpp
- tests/test_fmm_accuracy.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T16_frame_spike_stabilization_pass.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure -R fmm
- ctest --test-dir build-release --output-on-failure -R stability
- IDE diagnostics for changed files

## Rollback
Revert this task's commit to restore previous force kernel and scheduling behavior.

## Completion Artifact
- Diff showing numerical guards and scratch-buffer reuse
- Test coverage for overlapping-particle finite-force stability

## Implementation Notes
- Updated `QuadTree::getDataRange` to keep a minimum positive pad (use `max` instead of `min`) so near-collapsed clouds do not produce degenerate root bounds.
- Updated `FmmTree::buildTree` with minimum root box length floor (`1e-3`).
- Updated `FmmTree` internals to reuse scratch storage across builds:
  - persistent thread notepad buffers (`thread_notepads_`)
  - persistent particle-to-leaf map (`particle_to_leaf_`)
- Updated `computeForces`:
  - switched OpenMP schedule from `dynamic` to `static,64` for lower frame-to-frame jitter
  - added near-field squared-distance floor (`r2 >= 1.0`) to prevent singular-force blowups
- Added regression test in `tests/test_fmm_accuracy.cpp` asserting finite forces for overlapping-particle input.

## Validation Notes
- IDE diagnostics: no errors in changed files.
- Command-level CMake/CTest validation remains pending local execution in this session.
