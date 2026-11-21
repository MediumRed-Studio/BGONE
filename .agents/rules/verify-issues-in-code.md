# Verify Issues in Code Before Reporting Work-to-Do

When asked what work remains (release status, triage, "what's left", "what must be built"), never report an open issue as outstanding work on issue state alone. Openness is not evidence: issues stay open awaiting manual verification, go stale after silent fixes, or describe symptoms whose root cause moved. Verify each candidate against the code first, then report a classified verdict.

## Verification Procedure (Per Issue)
1. Read the full issue body and all recent comments (`gh issue view <N> -R MediumRed-Studio/BGONE --comments`).
2. Check for in-flight work **before** touching the code:
   - `git branch -a` for active topic branches naming the issue or subsystem (note branches with zero unique commits past `Dev`).
   - Open PRs (`gh pr list -R MediumRed-Studio/BGONE --state open`) referencing the issue; read the PR body, review state, and checks.
   - Classify active work as `IN-FLIGHT-PR` or `IN-FLIGHT-BRANCH` instead of proposing to rebuild it.
3. Inspect the named scripts, prefabs, configs, and layouts in the current checkout:
   - Confirm whether they exist and actually implement or break what the issue claims.
   - Check git history (`git log --grep="#<N>"` with boundary awareness, and `git log -- <path>`) to catch silent fixes or subsequent reverts.
4. Classify each issue with one line of concrete evidence:
   - `UNBUILT` — no implementing code exists on `Dev`.
   - `BUILT-NEEDS-PLAYTEST` — implementation is present on `Dev`, awaiting manual in-engine verification.
   - `STALE-CLOSABLE` — code proves the issue's premise is already resolved (cite file, line, commit).
   - `PREFAB-DONE-NEEDS-ENGINE` — static assets check out; cause requires Workbench or live diagnosis.
   - `PARTIAL` — partially addressed; state exactly what remains.
   - `REVERTED` — a fix landed but a later commit reverted or replaced it.

## History & Code Checks
- Read every matched commit in full: issue numbers get mis-tagged or buried in batch commits, and a later revert silently reopens a fixed issue.
- Presence is not behaviour: a component or action existing does not prove its conditions, authority checks, or null paths handle the issue's exact scenario. Read the actual execution path.
- An unmerged or unpushed branch counts as `UNBUILT` on `Dev` (name the branch).
- Leftover diagnostic/debug instrumentation in shipped code counts as open work.
