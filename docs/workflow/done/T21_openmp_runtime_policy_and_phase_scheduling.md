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

# T21 OpenMP Runtime Policy and Phase Scheduling

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
BTEL spike passages indicate large wall-time jitter across parallel BuildTree phases even when workload counters remain stable. Runtime-level OpenMP behavior needs explicit control and telemetry.

## Objective
Reduce scheduling jitter and improve diagnostics by pinning OpenMP runtime policy and exposing OpenMP runtime/thread telemetry in BTEL output.

## Scope
- Lock OpenMP runtime policy at startup
- Add OpenMP runtime/thread telemetry fields to BuildTelemetry and EngineFrameStats
- Include OpenMP telemetry fields in BTEL stdout lines
- Use explicit static scheduling for node-list/upward/downward phases

## Non-Goals
- Algorithmic FMM rewrite
- Asynchronous force refresh
- Broad system-level tuning beyond OpenMP policy/scheduling in this codebase

## Target Files
- src/fmm_tree.hpp
- src/simulation_engine.hpp
- src/simulation_engine.cpp
- src/main.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T21_openmp_runtime_policy_and_phase_scheduling.md

## Verification
- IDE diagnostics for changed files
- Runtime check of BTEL output fields for OpenMP telemetry

## Rollback
Revert this task commit to restore prior OpenMP configuration and BTEL schema.

## Completion Artifact
- BTEL lines include OpenMP runtime/thread telemetry
- BuildTree node-list/upward/downward phases use explicit static scheduling

## Implementation Notes
- Startup OpenMP policy in `src/main.cpp` now explicitly sets:
  - `OMP_WAIT_POLICY=ACTIVE`
  - `omp_set_dynamic(0)`
  - fixed thread count via `omp_set_num_threads(omp_get_max_threads())`
- `fmm::BuildTelemetry` now records OpenMP runtime and per-phase thread/team info:
  - `omp_max_threads`, `omp_dynamic_enabled`
  - `omp_threads_node_lists`, `omp_threads_upward`, `omp_threads_downward`, `omp_threads_forces`
- `SimulationEngine` now copies these fields into `EngineFrameStats` each rebuild frame.
- BTEL stdout schema in `src/main.cpp` now includes OpenMP telemetry fields.
- BuildTree parallel scheduling changes:
  - node-list phase: `schedule(static)`
  - upward phase: `schedule(static)`
  - downward phase: `schedule(static)`
  - force phase remains `schedule(dynamic, 64)` for load balancing.

## Validation Notes
- IDE diagnostics: no errors in changed files.
- Runtime BTEL verification is pending local run.
