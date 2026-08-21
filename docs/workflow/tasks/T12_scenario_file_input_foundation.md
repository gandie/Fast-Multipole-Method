# T12 Scenario File Input Foundation

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
To evaluate long-run orbital stability and bounded orbital elements, we need deterministic, reusable initial conditions that are externalized from test code and runnable in both automated tests and CLI workflows.

## Objective
Prepare engine-level scenario ingestion from files (JSON format) with a shared pathway usable both programmatically and through console arguments.

## Scope
- Define a JSON schema for simulation scenarios (bodies, masses/charges, positions, velocities, metadata)
- Introduce scenario-loading API surface for programmatic use in engine/test code
- Add CLI argument contract to load a scenario file at startup
- Define validation/error behavior for malformed or incompatible scenario files
- Ensure default behavior remains unchanged when no scenario file is provided

## Non-Goals
- Implementing long-duration stability tests
- Retuning force/integrator physics semantics
- Adding scenario authoring UI or editor tooling

## Target Files
- src/sim_options.hpp
- src/main.cpp
- src/simulation_engine.hpp
- src/simulation_engine.cpp
- tests/README.md
- README.md
- docs/workflow/changelog/2026-08.md
- docs/workflow/tasks/T12_scenario_file_input_foundation.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure
- ./build-release/bin/sim --help

## Rollback
Revert scenario-file input additions (CLI/API/schema wiring) and restore prior initialization flow that only uses generated distributions and existing options.

## Completion Artifact
Scenario files can be supplied from CLI and loaded through programmatic API, with deterministic initialization parity between runtime and test entry points.
