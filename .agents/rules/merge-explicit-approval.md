# Merge Rule: Clear Reviews & Green Checks for Dev; Explicit Approval for main

## Rule
- **PRs targeting `Dev`** (owner authorization, 2026-11-21, `#62`): may be squash-merged once **all** of the following hold at the **current PR head**:
  1. `@codex review` is clear at the current head (zero outstanding P1/P2 findings).
  2. GitHub CI (`Lint`) and any required `--priority 6` QA rig verification are green at the current head (with video evidence posted when player-visible behavior changed).
- **Promotions or resets to `main` and GitHub issue closures ALWAYS require Riley's explicit approval.** Green checks, watch notifications, or a clear Codex review NEVER authorize merging/promoting to `main` or closing an issue.

## Details
1. Merging a PR authorizes the merge only — never issue closure (see `issue-closure-manual-verification.md`). Issues stay open until Riley manually verifies in-game and confirms closure.
2. On merge into `Dev`: squash-merge (`gh pr merge <N> -R MediumRed-Studio/BGONE --squash --delete-branch`), post a status comment with verification verdicts, post a `DIGEST` on the linked issue (`.agents/rules/session-digests.md`), and remove the worktree.
3. If any check is red, any Codex finding is unresolved, or a review was only against an older head, do not merge. Fix, push, re-verify, and re-clear `@codex review` first.
4. For any PR targeting `main` (release promotion): leave the PR open, report readiness (checks state, Codex review state, verify summary, what the merge will do), and wait for Riley's explicit go-ahead.
