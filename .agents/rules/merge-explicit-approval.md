# Merge Rule: No Auto-Merge — Riley Gives the Explicit Go-Ahead

## Rule
Never merge a PR without Riley's explicit merge instruction for THAT PR. Opening PRs, pushing fix-ups, answering review, and re-running QA rigs are all fine unprompted. The merge action itself waits.

## Details
1. An explicit instruction names the PR (number or title) with a merge verb ("merge it", "lets merge them", "go ahead", "squash it"). Green checks, watch notifications, a clear `@codex review`, or "looks good" from another agent are NEVER merge authorization.
2. A merge instruction authorises the merge only — not issue closure (see `issue-closure-manual-verification.md`). Require a separate explicit close instruction, or confirmation that manual verification passed.
3. Promotions from `Dev` to `main` and issue closures ALWAYS require Riley's explicit approval.
4. On merge instruction for a PR into `Dev`: squash-merge (`gh pr merge <N> -R MediumRed-Studio/BGONE --squash --delete-branch`), post a status comment with verification verdicts, post a `DIGEST` on the linked issue (`.agents/rules/session-digests.md`), and remove the worktree.
5. Without instruction: leave the PR open, report readiness (checks state, Codex review state, verify summary, what the merge will do), and continue with other work.
