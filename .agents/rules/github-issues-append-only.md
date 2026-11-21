# GitHub Issues Append-Only Rule

## Rule
GitHub issues and pull requests in this repository are strictly **append-only**:
- **Never edit or delete** existing issue titles, issue bodies, or existing comments.
- **Never edit or replace** text written by Riley, prior agents, or yourself in prior turns.

## How to Add or Update Information
1. **New Information**: Post all status updates, implementation plans, review findings, session digests, and bot run evidence as **new, separate comments**.
2. **Corrections / Errata**: If a previous comment was inaccurate, do not edit it. Post a new comment explaining the correction (e.g., *"Correction to comment above: ..."*).
3. **Closing Issues**: Never close an issue until it has been explicitly verified in-game by Riley (`.agents/rules/issue-closure-manual-verification.md`).

## Rationale
Parallel subagents and human contributors work concurrently across issues. Editing existing comments introduces race conditions, destroys the audit trail, and confuses active sessions that rely on immutable comment timestamps.
