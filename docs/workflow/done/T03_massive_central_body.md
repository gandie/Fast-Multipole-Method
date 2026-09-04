---
agent_evaluation:
  version: 1
  evaluator: human_operator
  evaluated_at: 
  verdict: 
  would_delegate_similar_again: 

  score_scale:
    min: 1
    max: 5
  
  outcome:
    correctness: 
    scope_discipline: 
    validation_trust: 
  
  collaboration:
    ambiguity_handling: 
    operator_load: 
    trust_delta: 
---

# T03: Massive Central Body (Black Hole)

## Metadata
- Owner: unassigned
- Created: 2026-08-18
- Updated: 2026-08-18

## Why Needed
Add an optional massive central body feature to enable more realistic astrophysical simulations where a dominant central mass (like a black hole) anchors orbital dynamics. This allows visualization of orbital systems around a fixed center of gravity.

## Objective
Add a console argument to optionally create a massive central body at the center of the simulation that remains fixed in place. This body should be significantly more massive than regular bodies to create a visible orbital effect around it.

## Scope
- New console argument: `--black-hole <mass>` or `-b <mass>`
- Central body created at screen center with specified mass
- Central body remains stationary (zero velocity, zero acceleration)
- All other bodies orbit naturally around the center
- Help text documents the new argument

## Non-Goals
- Modifying orbital velocity calculation for regular bodies
- Adding visualization or rendering changes for black hole
- Adding physics interactions beyond standard force calculations

## Target Files
- `src/main.cpp`: argument parsing, help text, body initialization, frame integration

## Verification
- Code compiles without errors
- New argument `-b` and `--black-hole` parse correctly
- Central body appears at screen center when enabled
- Central body remains stationary throughout simulation
- Other bodies orbit around center without drift
- Help text clearly documents feature with example
- Negative/zero mass values properly rejected

## Rollback
Remove argument parsing block, remove body creation block, remove position/velocity reset in integration loop, revert help text.

## Completion Artifact
- Task file: docs/workflow/done/T03_massive_central_body.md
- Code changes: src/main.cpp with complete implementation
- Changelog entry in docs/workflow/changelog/2026-08.md
- All bodies remain in orbit; black hole fixed at center

## Implementation Notes
**Key Discovery**: The tree building process uses `std::partition()` to reorder the sources vector, causing the black hole to move from index 0 to an arbitrary position. Initial fix (skipping index 0 during integration) failed because the black hole's position in the array was no longer predictable.

**Final Solution**: Identify black hole by its distinctive massive property (q >= black_hole_mass * 0.9) and explicitly reset its position/velocity each frame, regardless of where partitioning moved it in the sources array.

**Changes Made**:
1. Updated `printHelp()` with new argument documentation and usage example
2. Added `black_hole_mass` variable (default 0.0 = disabled) 
3. Added argument parsing for `-b`/`--black-hole` with double precision
4. Added validation rejecting negative mass values
5. Modified body initialization to create central body before orbital velocity setup
6. Skipped black hole integration in both Phase 1 and Phase 3
7. Added position/velocity reset loop that finds black hole by mass and resets it every frame
