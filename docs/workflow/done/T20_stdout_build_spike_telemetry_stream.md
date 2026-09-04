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

# T20 Stdout Build Spike Telemetry Stream

## Metadata
- Owner: unassigned
- Created: 2026-08-21
- Updated: 2026-08-21

## Why Needed
Overlay telemetry is difficult to read during runtime jumps. A copy-paste friendly stdout stream is needed to capture spike passages for post-run analysis.

## Objective
Emit concise rebuild-frame BuildTree telemetry to stdout in a structured single-line format suitable for copy/paste.

## Scope
- Add stdout one-line telemetry output in main loop
- Include spike flag and key BuildTree metrics
- Keep existing simulation behavior unchanged

## Non-Goals
- Reworking telemetry collection internals
- Adding file logging
- Changing force-refresh semantics

## Target Files
- src/main.cpp
- docs/workflow/changelog/2026-08.md
- docs/workflow/done/T20_stdout_build_spike_telemetry_stream.md

## Verification
- IDE diagnostics for changed files
- Runtime manual check that lines are emitted to stdout

## Rollback
Revert this task commit to remove stdout telemetry output.

## Completion Artifact
- Rebuild-frame telemetry lines emitted to stdout with spike markers

## Implementation Notes
- Added a stdout telemetry header in `src/main.cpp` describing emitted keys.
- Added one-line telemetry records for each rebuild frame (when `build_telemetry_updated_this_frame` is true):
  - prefix: `BTEL`
  - includes phase timings, tree structure counters, and workload counters
  - includes `spike=1` marker when `build_internal_ms > max(5.0, ema_build_ms * 1.8)`
- Kept overlay telemetry unchanged; stdout output is additive for copy/paste workflows.

## Validation Notes
- IDE diagnostics: no errors in changed files.
- Runtime manual stdout verification remains pending local run.
