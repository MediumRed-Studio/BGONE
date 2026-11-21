# Record Decisions Live Rule

## Rule
Whenever Riley makes a decision in a discussion (refinement, review, design, triage), record it **immediately**, as it is made. Never batch decisions to the end of a session, and never wait to be asked.

## Where
1. **The relevant GitHub issue(s)**: post a new append-only comment on `MediumRed-Studio/BGONE` stating the decision (`.agents/rules/github-issues-append-only.md`). Record undecided points explicitly as **open**.
2. **Affected documentation**: update any design doc or reference doc the decision changes through a PR targeting `Dev` (`.agents/rules/pull-requests.md`).
3. **Other repos** the decision touches: record it there in the same way.

## Details
- Recording a decision in an issue comment is not a repo-state change that needs a go-ahead. Acting on it (editing code, changing rules, or merging PRs) still follows the normal approval rules.
- Decisions held only in chat are lost across agent handovers; the issue is the durable record.
