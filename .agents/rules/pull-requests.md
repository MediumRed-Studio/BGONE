# Pull Request Workflow Rule

## Repository & Remotes (Fork Safety)
`MediumRed-Studio/BGONE` is a public fork of `SpaceStrider/BGONE`.
- **ALWAYS pass `-R MediumRed-Studio/BGONE`** to every `gh pr`, `gh issue`, and `gh api` command.
- **NEVER open a PR against `SpaceStrider/BGONE` (upstream)** unless Riley explicitly asks for an upstream contribution.

## Branching & Targeting
- **Integration Branch**: All normal feature, fix, and chore PRs target `Dev`.
- **Release Snapshot**: `main` is reserved exclusively for clean release promotions from `Dev`. Never open a feature or fix PR directly against `main`.
- **Working Branches**: Create topic branches off `Dev` in an isolated worktree using:
  `feat/<issue-number>-<short-slug>`, `fix/<issue-number>-<short-slug>`, or `chore/<short-slug>`.

## Commit & PR Standards
- **Commit Messages**: Use Conventional Commits formatting:
  `feat(scope): <description> (#<issue>)`, `fix(scope): <description> (#<issue>)`, or `chore(scope): <description> (#<issue>)`.
- **PR Description**: Fill out `.github/pull_request_template.md` completely:
  1. Intent and summary of functional changes.
  2. Primary issue reference (`Refs #<N>` — never auto-close before manual verification; see `issue-closure-manual-verification.md`).
  3. Evidence of verification (lint, compile/autotests, and bot run video link via `publish-evidence.ps1` where player-visible).
- **Squash-Merge**: All PRs into `Dev` squash-merge (`gh pr merge --squash --delete-branch`). Never rebase-merge or merge-commit feature PRs into `Dev`.
- **Code Review**: Every new PR and every push to a new head goes through `@codex review` (see `.agents/rules/codex-pr-review.md`).
- **T3 PR Linking**: Whenever running in an environment with the `link_pull_request` tool, call it with the full PR URL immediately upon PR creation or when starting work on an existing PR.

## Approval Gate
- Never merge a PR without explicit authorization from Riley (`.agents/rules/merge-explicit-approval.md`).
