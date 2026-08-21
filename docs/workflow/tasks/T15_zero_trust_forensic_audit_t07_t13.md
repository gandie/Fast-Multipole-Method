# T15 Zero-Trust Forensic Audit of T07-T13

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
A critical trust break occurred after discovering that force-freshness semantics diverged from operator expectations across earlier sessions. Before new implementation work proceeds, T07-T13 must be re-audited with strict evidence discipline to identify any remaining semantic drift, misleading records, or residual risk.

## Objective
Produce an evidence-only forensic audit of tasks T07 through T13 that maps claims to implementation and validation artifacts, identifies confirmed mismatches, and proposes minimal corrective actions with explicit confidence levels.

## Scope
- Audit T07-T13 task artifacts, changelog entries, and corresponding code/test surfaces.
- Build a claim-vs-code matrix with line-level evidence references.
- Separate findings into confirmed mismatch, probable issue, and unknown.
- Identify stale or contradictory documentation that can mislead future sessions.
- Propose minimal cleanup and guardrail actions, but do not implement code changes in this task without explicit operator approval.
- Provide a residual-risk summary and a prioritized remediation list.

## Non-Goals
- Implementing feature changes unrelated to audit findings.
- Rewriting integrator/physics algorithms in this task.
- Retrofitting historical intent beyond what repository evidence supports.
- Auto-applying broad refactors before audit sign-off.

## Target Files
- docs/workflow/done/T07_refactor_main_engine_render_split.md
- docs/workflow/done/T08_border_click_spawn_stability.md
- docs/workflow/done/T09_simulation_engine_coverage_ramp.md
- docs/workflow/done/T10_build_output_isolation_and_workflow_alignment.md
- docs/workflow/done/T11_engine_correctness_audit_and_physics_regression_tests.md
- docs/workflow/done/T12_scenario_file_input_foundation.md
- docs/workflow/done/T13_stability_tests_from_predefined_scenarios.md
- docs/workflow/changelog/2026-08.md
- src/simulation_engine.cpp
- src/simulation_engine.hpp
- src/main.cpp
- tests/test_simulation_engine.cpp
- tests/README.md

## Verification
- cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_COVERAGE=OFF
- cmake --build build-release
- ctest --test-dir build-release --output-on-failure
- ctest --test-dir build-release --output-on-failure -R engine
- ctest --test-dir build-release --output-on-failure -R scenario

## Rollback
If audit conclusions are shown to contain factual errors, revert only the affected audit documentation updates and re-issue the report with corrected evidence.

## Completion Artifact
A signed-off forensic report for T07-T13 containing: (1) claim-vs-code matrix with line-linked evidence, (2) validated mismatch list with impact/confidence, (3) residual-risk register, and (4) operator-approved remediation plan.
