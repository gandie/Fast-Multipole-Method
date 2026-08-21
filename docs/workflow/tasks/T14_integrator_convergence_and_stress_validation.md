# T14 Integrator Convergence and Stress Validation

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
Current confidence is strong for selected deterministic scenarios, but scientific reliability requires broader and deeper evidence for integrator precision, stability envelopes, and failure boundaries. A structured follow-up is needed to quantify convergence behavior and stress resilience across independent fixtures and difficult edge cases.

## Objective
Plan and execute an expanded verification campaign focused on integrator-quality evidence: timestep convergence order across multiple fixtures, long-horizon stress behavior for extreme regimes, and explicit reporting of observed numerical trends.

## Scope
- Add a timestep sweep study over several independent scenario fixtures and measure convergence trends.
- Report observed order behavior from the sweep using deterministic metrics.
- Add long-horizon stress tests for high mass-ratio systems.
- Add long-horizon stress tests for close-approach and near-singular interaction regimes.
- Expand integrator-focused assertions for trajectory-wide boundedness and finite-state guarantees.
- Document fixture design rules, threshold rationale, and interpretation guidance for scientific review.

## Non-Goals
- Replacing the current physics law.
- Rewriting or swapping the integrator algorithm in this task.
- Adding performance benchmarking infrastructure unrelated to correctness/stability claims.

## Target Files
- tests/test_simulation_engine.cpp
- tests/fixtures/scenarios/
- tests/README.md
- docs/workflow/changelog/2026-08.md
- docs/workflow/tasks/T14_integrator_convergence_and_stress_validation.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure
- ctest --test-dir build-release --output-on-failure -R scenario
- ctest --test-dir build-release --output-on-failure -R precision
- ctest --test-dir build-release --output-on-failure -R stability

## Rollback
Revert new convergence and stress-test additions if thresholds are shown to be non-robust or scientifically invalid, then re-baseline with documented rationale.

## Completion Artifact
A deterministic integrator-focused test suite demonstrates convergence trends across multiple fixtures and stable long-horizon behavior under defined stress regimes, with documented limits and acceptance envelopes.
