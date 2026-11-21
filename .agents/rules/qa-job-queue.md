# Submit QA Once; Let the Shared Machine Queue Wait

## Shared Queue & Priority
BGONE shares this development machine's Arma Reforger QA rig queue at `%LOCALAPPDATA%\MRS-QA\queue` with MRS-GC via MRS-GC's `rig_queue.py` (`tools/qa/rig_queue.py submit`).

- **BGONE Priority**: Always submit BGONE jobs with **`--priority 6`** (owner decision, 2026-11-21: behind MRS-GC M4 RC `10`, Rail `9`, Fire `8`, and Crane `7`; ahead of default `0`). Never pass a priority higher than `6` for BGONE work.
- Priority orders waiting jobs only; it never preempts a running job.

## Submission & Notification Protocol
1. Register a thread-private completion webhook BEFORE submitting using T3 `schedule_task` with `schedule: {type: "webhook"}` and `bindToCurrentThread: true`. This registers a one-thread completion listener, not a recurring schedule. Reuse it for this thread.
2. Store the webhook URL in a private JSON file **outside Git** and pass `--notify-file <path>` to `rig_queue.py submit`. If T3 provides no webhook URL, report the missing completion transport before submitting; do not pretend a result file can wake an idle agent.
3. Submit once with a stable key. After submission, continue independent work or end the turn. Completion wakes the thread; do not write shell sleep/grep loops, repeatedly poll logs, or resubmit a job because the rig is busy. Explicit diagnostic status checks are fine.
4. Notifications are at-least-once: deduplicate by `job_id` and never rerun on a duplicate notification.

## Checkout Freeze & Failure Safety
- **Freeze the submitted checkout until completion.** Modifying tracked source files while a queued job is pending or running yields `stale`, not acceptance.
- Read `result.json` and the runner's own log on completion; queue exit `0` means the runner exited zero, not manual or production acceptance.
- An `interrupted` job fences dispatch. Inspect the wrapper, its engines, and the admission record before explicitly acknowledging it.
- **Never clear locks, kill unrelated engine processes, or acknowledge blindly** to move the queue. Cancel only queued jobs you own.
- Direct runner invocations remain available for runner development, offline self-tests, and human diagnosis; they still honor the rig's original locks.
