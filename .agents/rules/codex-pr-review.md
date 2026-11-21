# Codex PR Review Rule

## Rule
Every pull request in `MediumRed-Studio/BGONE` is reviewed via the Codex GitHub connector (`@codex review`), not via a duplicate 4- or 5-agent local review panel.

## Triggering & Watching
1. **On PR creation and after every push to a new head**, post a comment:
   ```powershell
   gh pr comment <N> -R MediumRed-Studio/BGONE --body "@codex review"
   ```
   (Optional short focus notes may follow `@codex review` on the same comment.)
2. Because Codex posts from its own bot account, call `watch_pull_request` (top-level coordinator threads only; delegated children never call `watch_pull_request`) and end the turn rather than polling with sleep loops or `gh --watch`.
3. After a restart or handoff, re-query `gh pr view <N> -R MediumRed-Studio/BGONE --json reviews,comments` before claiming a PR has no reviews.
4. **Never launch a duplicate 4/5-agent coding handover review panel** on a PR that uses `@codex review`.

## Handling Findings (Fix-Then-Re-Review Loop)
1. **Verify every finding against the actual code and vanilla source** before acting (see `verify-dont-guess.md`).
2. **Confirmed P1 / P2 findings**: Fix them immediately without stopping to ask Riley first, push a new commit, and comment `@codex review` again. Repeat until `@codex review` clears with **zero P1/P2 findings at the current head commit**.
3. **Verified false positives**: Push back concisely on the PR thread with exact file/line evidence. Riley still makes product/design calls.
4. **Stopping rule — zero P1/P2 (Medium+) = done**: Do not bloat PRs chasing Low/nit findings. Answer Lows with *less* code (delete or simplify), never more prose, defensive bloat, or redundant tests.

## Loop Hygiene & Failure Guards
- **No review-round IDs in code, docs, or test names**: Never embed tags like `R3-P2` or `per round 2` in source files, comments, or test names. Review history lives in PR comments only.
- **Prose cap**: State a non-obvious invariant once where it belongs, not repeated at every call site.
- **Stacked PRs re-merge their base's final head before every review round**: Check ancestry (`git merge-base --is-ancestor`) before requesting a review; a review against a stale base is invalid.
- **Never advance a round on reviewer failure**: If Codex or any automated reviewer fails (rate limit, timeout, infra error), stop or retry that same round once — never treat "0 findings + reviewer errored" as clear, and never spin in an unbounded loop.
