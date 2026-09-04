# LLLars Workflow Bookkeeping

## Purpose
This folder is the canonical bookkeeping surface for implementation work.

## File Map
- `tasks/`: active and queued task files.
- `done/`: completed task files moved from `tasks/`.
- `changelog/`: append-only change log files.

## Operating Rules
1. Update task file first, including an explicit `Why Needed` section.
2. Run implementation and validations.
3. Append changelog entry.
4. Move completed task file from `tasks/` to `done/`.

Folder location is the only status protocol:
- `tasks/` means active.
- `done/` means complete.

No protocol extension is allowed:
- Do not add lifecycle metadata fields such as `Status` or `Priority` to task files.
- Done-task evaluation frontmatter is allowed only in files under `done/` and is not a lifecycle status field.

## Mandatory Task Template

```markdown
# TXX Short Task Title

## Metadata
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
- <Verification>

## Rollback
<Rollback plan>

## Completion Artifact
<What proves completion>
```

This file and the bookkeeping skill define the active workflow contract.

## Done Task Evaluation Block (Operator-Filled)

After a task is moved to `done/`, add this frontmatter block at the top of the
done task file and fill values as the human operator.

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
    <short operator rationale>
---
```

Required split-score categories:
- Outcome: `correctness`, `scope_discipline`, `validation_trust`
- Collaboration: `ambiguity_handling`, `operator_load`, `trust_delta`

## Bookkeeping Skill
Use skill definition at `.github/skills/bookkeeping/SKILL.md` for task handling and completion enforcement.

## Eternal Prompt
Use this prompt at the start of any task session:

```text
Look at task.
Understand task, preferring codebase MCP analysis tools over broad file-by-file reading.
If requirements contain policy ambiguity with multiple valid semantics, run forcing-line-development and stop to ask before encoding behavior.
Ask clarifying questions if needed.
Proceed to implementation.
```