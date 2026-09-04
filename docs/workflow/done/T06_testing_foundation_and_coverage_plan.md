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
    correctness: 4
    scope_discipline: 5
    validation_trust: 4

  collaboration:
    ambiguity_handling: 5
    operator_load: 5
    trust_delta: 4

  notes: >
    Robot even asks for scores. Robot did well, not much friction
---

# T06 Testing Foundation and Coverage Ramp

## Metadata
- Owner: unassigned
- Created: 2026-08-19
- Updated: 2026-08-19

## Why Needed
The project currently has no first-class automated test suite in CMake, which makes refactors risky and slows confidence in numerical correctness, tree invariants, and recent async/interaction behavior changes.

## Objective
Establish a practical C++ testing foundation and execute a staged plan toward high coverage, with deterministic tests for math/tree kernels and regression guards for known failure modes.

## Scope
- Add a C++ test framework and wire it into CMake/CTest
- Add deterministic unit tests for core math and expansion primitives
- Add structure/invariant tests for quadtree, Barnes-Hut, and FMM construction
- Add numerical-accuracy tests against exact-force diagnostics on fixed fixtures
- Add regression tests for previously fixed failures (stale force swap, argument validation)
- Add coverage instrumentation and reporting commands for local development
- Add dedicated testing documentation in `tests/README.md` (how tests work, how to run, and how to add tests)

## Non-Goals
- Rewriting simulation architecture in this task unless needed to expose test seams
- GPU benchmarking or real-time performance benchmarking framework
- Golden-image visual rendering tests in this first testing pass
- Replacing SFML runtime behavior with full end-to-end UI automation

## Target Files
- CMakeLists.txt
- README.md
- src/main.cpp
- src/adaptive_quadtree.hpp
- src/math_utils.hpp
- src/multipole_expansion.hpp
- src/local_expansion.hpp
- src/barnes_hut_tree.hpp
- src/fmm_tree.hpp
- src/diagnostics.hpp
- src/sim_options.hpp
- src/force_swap.hpp
- tests/CMakeLists.txt
- tests/README.md
- tests/test_math_utils.cpp
- tests/test_expansions.cpp
- tests/test_quadtree_invariants.cpp
- tests/test_barnes_hut_accuracy.cpp
- tests/test_fmm_accuracy.cpp
- tests/test_regressions.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T06_testing_foundation_and_coverage_plan.md

## Verification
- cmake -S . -B build -DBUILD_TESTING=ON
- cmake --build build --config Release
- ctest --test-dir build --output-on-failure
- cmake -S . -B build-coverage -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
- cmake --build build-coverage --config Debug
- ctest --test-dir build-coverage --output-on-failure
- gcovr --root . --filter src --exclude 'build|build-coverage|venv|_deps'

## Implementation Notes
- Phase 1 implemented:
  - Root CMake now includes CTest and conditionally fetches Catch2 when BUILD_TESTING is enabled.
  - Added tests subdirectory build wiring with Catch2 test discovery.
  - Fixed CMake ordering so `find_package(OpenMP REQUIRED)` runs before test target configuration.
- Phase 2 implemented:
  - Added deterministic unit tests for BinomialTable behavior.
  - Added deterministic quadtree tests for data-range bounds and adjacency edge cases.
  - Added deterministic expansion tests that compare multipole/local evaluations against direct computations.
- Phase 3 implemented:
  - Extended invariant coverage with Barnes-Hut and FMM leaf partition coverage checks (contiguous, gap-free coverage of source ranges).
  - Added force-vector sizing assertions after Barnes-Hut and FMM tree builds.
- Phase 4 implemented:
  - Added Barnes-Hut and FMM conservative numerical-accuracy tests against exact-force diagnostics on deterministic grid fixtures.
- Phase 5 implemented:
  - Added testable CLI parsing seam in `src/sim_options.hpp` and integrated it into `src/main.cpp`.
  - Added testable force-swap seam in `src/force_swap.hpp` and integrated it into `src/main.cpp`.
  - Added regression tests for argument validation and stale pending-force swap safety.
- Phase 6 implemented:
  - Added optional `ENABLE_COVERAGE` CMake option with GNU/Clang coverage flags.
- Phase 7 implemented:
  - Expanded `tests/README.md` with full suite layout, coverage workflow, and test authoring conventions.
- Added test onboarding documentation in `tests/README.md` with local run instructions and test authoring conventions.
- Fixed header self-containment issues needed by standalone test compilation:
  - `src/adaptive_quadtree.hpp` now includes `<tuple>` and `<functional>`.
  - `src/math_utils.hpp` now includes `<iomanip>` and `<stdexcept>`.
- Resolved linker ODR failures by marking header-defined `readFile` and `toFile` inline in `src/math_utils.hpp`.

## Verification Results
- Local user verification confirmed configure/build/test issues were surfaced and resolved during implementation:
  - OpenMP target visibility during test target configure
  - Header ODR linker errors in `src/math_utils.hpp`
  - Header ODR linker errors in `src/diagnostics.hpp`
  - Force-swap regression test failure fixed by one-way pending-force consumption
- User-provided coverage report after the second pass:
  - `src/adaptive_quadtree.hpp`: 100%
  - `src/fmm_tree.hpp`: 100%
  - `src/local_expansion.hpp`: 100%
  - `src/multipole_expansion.hpp`: 100%
  - `src/math_utils.hpp`: 100%
  - `src/diagnostics.hpp`: 100%
  - `src/force_swap.hpp`: 100%
  - `src/sim_options.hpp`: 97%
  - `src/barnes_hut_tree.hpp`: 94%
  - `src/main.cpp`: 0% (runtime loop not yet exercised by automated tests)
  - Total coverage: 66%
- IDE diagnostics report no errors in newly added/edited test source files and related headers.

## Coverage Iteration Notes (Pass 2)
- Added additional parser regression coverage in `tests/test_regressions.cpp`:
  - help short-circuit path
  - missing value errors
  - invalid integer errors
  - unknown argument errors
  - rebuild lower-bound validation
  - zero-total-body validation
  - negative black-hole validation
  - successful mixed-option parse path
- Added quadtree BFS coverage in `tests/test_quadtree_invariants.cpp`:
  - null-root early return
  - level-order traversal with child enqueue paths
- Added FMM/Barnes-Hut utility path coverage in `tests/test_fmm_accuracy.cpp`:
  - FMM single-leaf build path and `getBoxGeometries()`
  - Barnes-Hut `getBoxGeometries()` usage after build

## Rollback
Disable testing targets in CMake and remove new tests directory/documentation, restoring the previous build-only workflow.

## Completion Artifact
An automated CTest suite runs locally, includes deterministic numerical and regression tests for core algorithms, includes clear onboarding docs in `tests/README.md`, and produces coverage output showing broad exercise of source modules.

## Completion Notes
- Completed all planned phases 1 through 7 for local testing and coverage workflow.
- Added root-level workflow documentation in `README.md` to prevent Debug/Coverage vs Release build confusion.
- Left runtime-loop coverage in `src/main.cpp` as a known follow-up opportunity (requires bounded runtime smoke execution path).

## Plan
1. Testing scaffold
- Introduce Catch2 via CMake FetchContent and add `enable_testing()` plus `tests/` subdirectory.
- Ensure tests compile with the same C++ standard and warning posture as production code.

2. Deterministic unit tests
- Validate `BinomialTable` growth/indexing and stable value retrieval.
- Validate `MultipoleExpansion` and `LocalExpansion` evaluate/shift behavior on tiny analytic fixtures.
- Validate `QuadTree::getDataRange()` and node adjacency edge cases.

3. Tree/invariant tests
- Verify leaf partition coverage and non-overlap invariants for Barnes-Hut and FMM trees.
- Verify force vector size always equals source size after tree builds.

4. Accuracy tests
- Compare Barnes-Hut and FMM forces against `computeExactForces()` for fixed seeded particle sets.
- Use conservative relative-error thresholds first, then tighten once baseline is stable.

5. Regression tests
- Add tests that lock in behavior for argument validation (`--orbit` requires positive `--black-hole`, non-negative counts).
- Add a seam to test async force swapping logic without SFML event loop coupling.

6. Coverage workflow
- Add optional `ENABLE_COVERAGE` CMake switch for GCC/Clang.
- Document and run `gcovr` reporting focused on `src/`.

7. Test documentation
- Add `tests/README.md` that explains test architecture (unit/invariant/accuracy/regression).
- Document local commands for configure/build/run and optional coverage runs.
- Define conventions for adding new tests, naming files, fixture placement, and deterministic seeds.