# Automated Bot & Video Verification Rule

## Rule
Every pull request that changes something a player can see or do (missile guidance, lock-on HUD/audio, launcher handling, multiplayer sync, artillery computer UI, or weapon prefabs) must carry a bot video of that change so Riley can review it by watching instead of playing every pass manually.

The video must come from the PR's **current head commit**. A video of an earlier commit, or of uncommitted edits, does not count. Record again after any push that changes what the video shows. `tools/qa/publish-evidence.ps1` refuses a run whose commit is not the PR head or that used uncommitted files.

## When to Record: After `@codex review` Clears
Fixes for code review findings change the head commit, invalidating earlier evidence. Therefore:
1. **While building**, run scenarios as often as needed to test your work (`-Record` included). Do not publish scratch runs as final evidence.
2. **Once `@codex review` clears with zero P1/P2 findings at the PR's current head** (`.agents/rules/codex-pr-review.md`), record the verification video at that head, inspect the captured frames, and publish it.
3. If anything changes the head after that (a later review, a video finding, or a fix), repeat: `@codex review` at the new head, then a fresh video.

A run that reveals a bug is always a finding: report it plainly and fix it regardless of review state.

## Automated Headless MP Bot Rig (No `-Hand` Requirement for Missile Gameplay)
BGONE runs its multiplayer verification through `tools/qa/mp-run.ps1 -Record` using automated headless/embodied bot inputs.
- Launcher aim, lock-on, firing, SACLOS/PLOS/VIS tracking, and multiplayer missile flight are driven programmatically without requiring `-Hand` or taking Riley's real mouse/keyboard.
- Background recording (`-Record`) touches no real mouse or keyboard input and needs no computer lease.
- Only if a future UI/menu flow requires the real foreground mouse must it acquire the computer lease first (`.agents/rules/shared-computer.md`).

## What a Scenario Must Cover
- **Server-side outcome verification**, not only what a client rendered (e.g. server missile spawn, lock/target sync, impact/detonation).
- **Two clients where replication matters** (shooter + observer) to verify missile flight, audio/lock cues, and impact sync across clients.
- **Refusal / edge cases** (e.g. firing without lock, losing line of sight, out-of-range, invalid target) where applicable.
- **No hidden shortcuts**: setup steps (spawning fixtures, positioning shooter/target) are clearly labelled as setup; gameplay steps exercise the actual weapon/guidance pipeline.
- **Rerun existing scenarios** covering the touched guidance mode or system.
- The only PR without a video is one with nothing a player can see or do (governance rules, workflows, lint, docs, pure refactors); its PR description states "No player-visible change" and why.

## Review the Video, Not Just the Exit Code
After recording, extract and inspect frames (`tools/qa/review-frames.ps1`) to confirm the HUD, lock box, missile flight, and detonation actually look right to a player (no clipped UI, missing reticles, frozen missiles, or desynced explosions).

## Plain English & Public Repo Hygiene
- Video captions, evidence summaries, and top-level PR comments are written in **plain English** describing what the player does and sees; put technical logs/IDs inside a collapsed `<details>` block.
- **`MediumRed-Studio/BGONE` is a PUBLIC repository.** Never include private credentials, local secrets, or unrelated internal MRS-GC data in published videos, logs, or PR comments.
- Post evidence to:
  1. The PR (`tools/qa/publish-evidence.ps1 -RunDir <run> -Pr <number>`), linked in the PR's **Video evidence** section.
  2. The linked issue (`-Issue <number>`).
  3. The active agent chat session (local `.mp4` path).
