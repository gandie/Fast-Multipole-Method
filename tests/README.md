# Test Suite Guide

This directory contains the local automated test suite for core numerical logic.

## Test Layout

- `test_math_utils.cpp`: deterministic unit tests for math primitives such as `BinomialTable`.
- `test_quadtree_invariants.cpp`: deterministic quadtree behavior and tree partition invariants for Barnes-Hut and FMM.
- `test_expansions.cpp`: deterministic checks for multipole and local expansion behavior against direct computations.
- `test_barnes_hut_accuracy.cpp`: conservative accuracy checks against exact-force diagnostics.
- `test_fmm_accuracy.cpp`: conservative accuracy checks against exact-force diagnostics.
- `test_regressions.cpp`: regression checks for CLI validation and stale force-swap safety.
- `test_simulation_engine.cpp`: deterministic engine API and stepping behavior coverage.
- Scenario-file ingestion coverage lives in `test_regressions.cpp` and `test_simulation_engine.cpp`.
- Predefined stability fixtures live in `fixtures/scenarios/` and are consumed by `test_simulation_engine.cpp`.

## How Tests Are Built

- Tests are enabled through CMake with `BUILD_TESTING=ON`.
- Catch2 is fetched via CMake FetchContent from the root project configuration.
- CTest discovers and runs Catch2 test cases automatically.

## Run Tests Locally

From repository root:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

Run the release executable:

```bash
./build-release/bin/sim
```

Run with a scenario file:

```bash
./build-release/bin/sim --scenario path/to/scenario.json
```

Run with verbose listing:

```bash
ctest --test-dir build-release --output-on-failure -V
```

Run tests matching a label substring:

```bash
ctest --test-dir build-release --output-on-failure -R expansion
```

## Optional Coverage Run

From repository root:

```bash
cmake -S . -B build-coverage -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build-coverage
ctest --test-dir build-coverage --output-on-failure
gcovr --root . --filter src --exclude build --exclude build-release --exclude build-coverage --exclude venv --exclude _deps
```

Run the coverage executable:

```bash
./build-coverage/bin/sim
```

If `gcovr` is missing, install it in your active environment first.

## Add New Tests

1. Add a new file in this folder named `test_<topic>.cpp`.
2. Register the file in `tests/CMakeLists.txt` under `add_executable(unit_tests ...)`.
3. Use deterministic fixtures and fixed seeds where randomness is involved.
4. Prefer direct analytic checks or trusted reference computations.
5. Keep each test case focused on one behavior and tag tests by domain, for example `[expansion]`, `[quadtree]`, `[math]`.
6. For regression tests, include the original failure mode in the test name or comments so intent stays explicit.

## Scenario Stability Fixtures

- Store deterministic stability fixtures in `tests/fixtures/scenarios/*.json`.
- Keep fixture body ordering stable when tests depend on specific pair metrics (for example planet/moon distance checks).
- Use `metadata.name` for human-readable fixture identity; runtime behavior is driven by the `bodies` list.
- Stability tests in `test_simulation_engine.cpp` use three fixed envelopes:
	- Relative pseudo-energy drift envelope.
	- Radius envelope from the initial geometric center.
	- Non-escape checks (finite states and bounded pair distance constraints).
- Additional confidence tests include:
  - timestep-refinement consistency checks (`dt`, `dt/2`, `dt/4`) against separation and drift metrics
  - full-trajectory (not only final-state) boundedness envelopes for hierarchical fixtures
- For new fixtures, add thresholds in the local test helper next to the fixture-specific test case and justify constants in comments if they are not self-evident.

## Conventions

- Avoid non-deterministic test data.
- Use tight but realistic floating-point tolerances with `Approx(...).epsilon(...)`.
- Keep tests independent, side-effect free, and fast enough for normal local runs.
- Place reusable helpers as `static` or anonymous-namespace functions in the same test file unless shared heavily.
