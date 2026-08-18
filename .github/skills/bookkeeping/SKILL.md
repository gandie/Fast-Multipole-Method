---
name: Bookkeeping
description: Use for task execution bookkeeping, changelog updates, and folder-driven task lifecycle enforcement.
---

# Bookkeeping

## Mission
Enforce low-ceremony, folder-driven bookkeeping so each task has a clear artifact, verified outcome, and changelog record.

## Canonical Sources
- docs/workflow/README.md
- docs/workflow/tasks/
- docs/workflow/done/
- docs/workflow/changelog/

## Hard Rules
- One task equals one markdown file.
- Active tasks live in docs/workflow/tasks/.
- Completed tasks are moved to docs/workflow/done/.
- No separate in-progress tracking file is used.
- No separate backlog index file is used.
- Each task file must contain a `Why Needed` section explaining why the change is required.
- A task is not complete until changelog entry and validation evidence are recorded.
- Folder location is the only status protocol (`tasks/` = active, `done/` = complete).
- No protocol extension is allowed (no custom lifecycle metadata fields such as `Status` or `Priority` in task files).
- Each done task file must include an operator-filled `agent_evaluation` YAML frontmatter block after it is moved into `docs/workflow/done/`.

## Mandatory Task Template

Every new task file must follow this template exactly (no additional lifecycle metadata fields):

```markdown
# TXX Short Task Title

## Metadata
- Owner: unassigned
- Created: YYYY-MM-DD
- Updated: YYYY-MM-DD

## Why Needed
<Why this change is required>

## Objective
<Expected outcome>

## Scope
- <In-scope item>

## Non-Goals
- <Out-of-scope item>

## Target Files
- <path/to/file>

## Verification
- .\\venv\\Scripts\\python.exe -m unittest discover .\\tests\\

## Rollback
<Rollback plan>

## Completion Artifact
<What proves completion>
```

## Task Handling Protocol
1. Before implementation:
- Ensure the task file exists in docs/workflow/tasks/.
- Ensure the task file follows the mandatory template exactly.
- Ensure no custom lifecycle metadata fields are present.

2. During implementation:
- Keep task file updated with verification commands/results.
- Keep scope minimal; do not widen task intent.

3. On completion:
- Run full suite and confirm pass: `.\\venv\\Scripts\\python.exe -m unittest discover .\\tests\\`.
- Add an entry to current monthly file in docs/workflow/changelog/.
- Ensure `Why Needed` remains accurate for the shipped change.
- Update task metadata and completion notes.
- Move the task file from docs/workflow/tasks/ to docs/workflow/done/.
- Add or confirm `agent_evaluation` frontmatter at the top of the done task file.
- Ensure split-score fields exist in two categories:
  - outcome: `correctness`, `scope_discipline`, `validation_trust`
  - collaboration: `ambiguity_handling`, `operator_load`, `trust_delta`

Use this exact template for the `agent_evaluation` frontmatter block in done task files. Do not dare to fill any score values or notes in the template; those are for the human operator to fill after task completion.

```yaml
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
```

## Changelog Enforcement
- Changelog is append-only.
- One completion event per entry.
- Include: date, task id/link, outcome summary, files changed, validation result, residual risk.

## Validation Contract
- Run targeted or full tests for implementation changes per task verification section.

## Anti-Patterns
- Moving a task to docs/workflow/done/ before full-suite unittest discover passes.
- Closing a task without moving it to docs/workflow/done/.
- Recording completion without test evidence.
- Completing a task file without an explicit `Why Needed` section.
- Leaving a task in done folder without `agent_evaluation` frontmatter in the file.
- Creating invalid YAML frontmatter in task files.
- Using `Status`, `Priority`, or any custom lifecycle metadata in task files.
- Reintroducing backlog or in-progress index ceremony.
- Bundling multiple unrelated tasks into one task file.
