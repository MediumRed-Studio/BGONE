# Verify, Don't Guess

## Rule
Never present a guess as an answer, a plan, or a design. Before stating how something works, what was decided, or what to build, check it against the source that holds the answer and cite what you checked. If you cannot check it, state plainly that it is unverified and name what would settle it. Do not fill the gap with an assumption.

## Check the Record Before Proposing
- **Decisions and scope:** read the parent issue, linked sub-issues, and their decision comments on `MediumRed-Studio/BGONE` before proposing a design.
- **Engine and vanilla behaviour:** read `Z:/Reforger Source 1.9.0.37/scripts` (`.agents/rules/reference-source.md`) or measure it on the QA rig. Remembered or "probable" API behaviour is not evidence.
- **Our code:** read the actual files and git history (`.agents/rules/verify-issues-in-code.md`). A file name, symbol name, or open issue state proves nothing about current behaviour.
- **Test and rig results:** read the actual log output and `result.json`. Never report a run you have not inspected.

## When Something Cannot Be Checked Yet
- Label it in the same sentence as "unverified", "assumed", or "starting hypothesis", along with what would settle it (a source file, a rig run, or a decision from Riley).
- Flag any starting tunable value explicitly rather than presenting it as verified.
- Ask Riley for open product decisions instead of guessing, then record the answer live (`.agents/rules/record-decisions-live.md`).
