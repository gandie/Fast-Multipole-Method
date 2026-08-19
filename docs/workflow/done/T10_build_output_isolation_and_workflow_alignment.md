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

# T10 Build Output Isolation and Workflow Alignment

## Metadata
- Owner: unassigned
- Created: 2026-08-19
- Updated: 2026-08-19

## Why Needed
Coverage and release builds currently share a source-level runtime output directory, which can overwrite executables across build modes and leave runtime in slow instrumented state.

## Objective
Isolate runtime output per build directory and align documentation to a strict two-build-folder workflow so release and coverage artifacts cannot clobber each other.

## Scope
- Update CMake runtime output directory to be build-directory-local
- Align test workflow docs with build-release/build-coverage convention
- Add explicit release run command path after build

## Non-Goals
- Changing simulation physics behavior
- Introducing new build systems or presets
- Expanding coverage scope beyond build/workflow correctness

## Target Files
- CMakeLists.txt
- tests/README.md
- README.md
- docs/workflow/tasks/T10_build_output_isolation_and_workflow_alignment.md
- docs/workflow/changelog/2026-08.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ./build-release/bin/sim
- cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON
- cmake --build build-coverage
- ./build-coverage/bin/sim

## Rollback
Revert CMake output directory and documentation edits if any environment requires source-level bin output.

## Completion Artifact
Release and coverage builds produce separate sim binaries in their respective build directories, and workflow documentation references only build-release/build-coverage.

## Implementation Notes
- Updated runtime output path in `CMakeLists.txt` from source-level `bin` to build-local `${CMAKE_BINARY_DIR}/bin`.
- Aligned `tests/README.md` commands to use only `build-release` and `build-coverage`.
- Added explicit run commands for both modes:
	- `./build-release/bin/sim`
	- `./build-coverage/bin/sim`
- Updated root `README.md` workflow snippets to include build-local run paths.

## Verification Results
- IDE diagnostics clean for:
	- `CMakeLists.txt`
	- `README.md`
	- `tests/README.md`
	- `docs/workflow/tasks/T10_build_output_isolation_and_workflow_alignment.md`
- Expected behavior change:
	- coverage builds no longer overwrite release executable
	- release rebuild restores optimized runtime at `./build-release/bin/sim`

## Completion Notes
- Root cause addressed at build artifact routing level, not just command usage.
- No physics or simulation logic changes were made.
