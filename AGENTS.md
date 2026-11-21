# Repository agent instructions

`.agents/` is the canonical source for this repository's rules. Keep rule bodies there rather than copying them into tool-specific instruction files.

## Project rules

Before planning, reviewing, or changing repository work, read and follow every Markdown rule in `.agents/rules/`. Clients that support `@path` imports load the files below automatically; other agents must open and read the same files explicitly. If a rule cannot be read or instructions conflict, report the gap rather than silently assuming an exemption.

@.agents/rules/bot-video-verification.md

@.agents/rules/codex-pr-review.md

@.agents/rules/enforce-script-gotchas.md

@.agents/rules/github-issues-append-only.md

@.agents/rules/issue-closure-manual-verification.md

@.agents/rules/merge-explicit-approval.md

@.agents/rules/pull-requests.md

@.agents/rules/qa-job-queue.md

@.agents/rules/record-decisions-live.md

@.agents/rules/reference-source.md

@.agents/rules/search-scope.md

@.agents/rules/session-digests.md

@.agents/rules/shared-computer.md

@.agents/rules/testing-policy.md

@.agents/rules/verify-dont-guess.md

@.agents/rules/verify-issues-in-code.md

## Maintaining the entrypoint

When adding, removing, or renaming a rule, update this import list in the same change. Keep the substantive instructions in their canonical `.agents/rules/` files so there is only one maintained copy.
