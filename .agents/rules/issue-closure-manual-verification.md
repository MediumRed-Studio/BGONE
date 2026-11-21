# Issue Closure Rule: Manual Verification Required

## Rule
Never close a GitHub issue until Riley has manually verified the fix in-game (Workbench or live session) and explicitly confirmed closure.

## Details
1. Merging a PR does **not** imply its issue can be closed. Headless QA, autotests, and code reviews are gates for merging, not substitutes for in-engine verification.
2. After a PR merges, the linked issue stays **open** as Workbench-pending until Riley runs the human verification checklist and says to close it.
3. "Lets merge them" (or any merge instruction) authorises the merge only — not issue closure. Require a separate explicit close instruction, or confirmation that manual verification passed.
4. When in doubt, leave the issue open and state what verification is still outstanding.
