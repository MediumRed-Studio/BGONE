# Testing Policy & Speed Rule

Engine launches on this shared machine are serial and take several minutes per run. Follow this policy to keep verification thorough without burning rig time on redundant runs.

## When to Run Tests
1. **Test at PR-ready, not after every small edit**: Finish the planned implementation, then run one clean verification batch before opening the PR (lint + relevant compile/autotest + MP bot scenario if player-visible).
2. **Fix passes**: After review fixes, run static lint and a compile check first; re-run only the affected autotest or MP scenario once all confirmed review findings are addressed.
3. **Doc-, rule-, or comment-only PRs**: No engine run is needed. Run `.\tools\lint\lint-run.ps1` (full pass and `-SelfTest`) only.
4. **Targeted suites**: Run only the suites and scenarios exercised by the change. When a batch fails, fix all reported defects before re-running the batch once.

## Mutation Checks
- Run mutation checks selectively on **authority, security, and replication/MP guards** (for example verifying that removing a server check or identity derivation fails the test/lint).
- Batch independent mutations into a single engine run when each trips a distinct assertion.

## `resourceDatabase.rdb` Hygiene
- **Keep regenerated `resourceDatabase.rdb` files locally** in the worktree (`.gitignore` already ignores `*.rdb`) — discarding them forces a multi-minute RDB rebuild before the next engine run.
- **Never commit any `.rdb` file** to Git.
