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

# T04 Orbit Spawn Mode

## Metadata
- Owner: unassigned
- Created: 2026-08-18
- Updated: 2026-08-18

## Why Needed
Enable a dedicated orbit-seeding mode that creates bodies already in circular trajectories around a central black hole. This supports repeatable orbital test scenarios without manually tuning initial velocity vectors.

## Objective
Add a CLI argument for orbit-mode bodies that spawns additional particles only when a black hole is enabled, with randomized circular orbits around the center.

## Scope
- Add argument: `-o, --orbit <N>`
- Validate orbit count and black-hole dependency
- Spawn `N` orbit bodies around black hole center with randomized circular initial conditions
- Document argument in help output

## Non-Goals
- Changing force law or integration method
- Reworking existing `-c` and `-u` initialization semantics
- Adding rendering/UI changes specific to orbit bodies

## Target Files
- src/main.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T04_orbit_spawn_mode.md

## Verification
- cmake --build build
- ./bin/sim --help
- ./bin/sim -b 100000 -o 100
- ./bin/sim -o 100 (should fail with clear validation error)

## Implementation Notes
- Added `-o`/`--orbit` argument parsing with integer validation
- Added validation guard: orbit bodies require a positive `--black-hole` mass
- Added orbit-mode spawn block that creates random angle/radius around center and tangent velocity for circular trajectories
- Orbit speed uses `sqrt(black_hole_mass)` with small jitter and random clockwise/counterclockwise direction
- Kept existing `-c` and `-u` generation behavior unchanged

## Verification Results
- IDE diagnostics checked for changed file (`src/main.cpp`) using workspace error tool
- Runtime/build commands listed above are pending local execution because shell command execution is unavailable in this tool session

## Rollback
Revert `-o/--orbit` argument parsing and validation, remove orbit body generation block, and revert help text.

## Completion Artifact
Updated CLI help and initialization logic in src/main.cpp, with pending runtime/build checks documented for operator execution.
