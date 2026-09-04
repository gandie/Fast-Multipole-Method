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

# T11 Engine Correctness Audit and Physics Regression Tests

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
After the T07-T10 engine/render split and follow-up hardening, runtime performance improved significantly, but we need proof that simulation correctness was preserved and no missing calculations or update calls slipped in.

## Objective
Audit simulation engine behavior for obvious correctness defects and add deterministic regression tests that validate key physical expectations such as symmetric pairwise motion and bounded energy drift in a closed two-body setup.

## Scope
- Review simulation update flow for missing calls/calculations after T07-T10
- Add deterministic engine-level tests for two-body behavior against analytic expectations
- Add deterministic energy-behavior checks for short-run closed systems
- Run full C++ test suite and report findings

## Non-Goals
- Redesigning numerical integration or force model
- Refactoring rendering/UI code beyond what tests require
- Large-scale algorithm retuning

## Target Files
- src/simulation_engine.cpp
- src/simulation_engine.hpp
- src/fmm_tree.hpp
- tests/test_simulation_engine.cpp
- tests/test_fmm_accuracy.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T11_engine_correctness_audit_and_physics_regression_tests.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure

## Rollback
Revert T11 test additions and any engine changes if the new checks expose unstable assumptions that require broader design decisions.

## Completion Artifact
A documented audit result and passing deterministic tests that either expose a concrete correctness bug or provide evidence that core engine calculations remain valid after T07-T10.

## Implementation Notes
- Audit finding: with black-hole mode enabled, right-click removal at center could delete the black hole, violating the engine invariant that index 0 is reserved for the pinned black hole during integration.
- Fix applied in engine removal path: protected-mass filtering now prevents deletion of the configured black-hole source.
- Root-cause finding from failing two-body analytic test: `FmmTree::buildTree()` single-leaf path did not seed root near-neighbors, so `computeForces()` produced zero forces for small systems (<= max leaf size).
- Fix applied in FMM build path: root leaf now includes itself in near-neighbors before force evaluation.
- Added deterministic physics-focused tests:
	- two-body first-step analytic update agreement
	- two-body pseudo-energy bounded drift over short run
	- black-hole non-removability regression
- Added FMM regression proof for single-leaf two-body non-zero, opposite forces.
- Follow-up from operator test run: Newton-pair cancellation assertion was invalid for unequal charges under the implemented source-weighted force law; test now checks exact analytic component values instead.

## Verification Results
- Static diagnostics (changed files): no errors in
	- src/simulation_engine.hpp
	- src/simulation_engine.cpp
	- src/fmm_tree.hpp
	- tests/test_simulation_engine.cpp
	- tests/test_fmm_accuracy.cpp
- Operator-confirmed runtime validation:
	- build + test workflow is green after fixes
	- two-body analytic engine test now passes
	- single-leaf FMM regression test now passes with corrected analytic expectation

## Completion Notes
- Objective met: audit found and fixed two correctness issues (black-hole removal invariant and single-leaf FMM near-neighbor seeding), and the follow-up failing assertion was corrected to match the implemented source-weighted force model.
- Task is ready to move from tasks to done with changelog entry recorded.
