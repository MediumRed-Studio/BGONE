# Shared Computer Rule: Never Take the Mouse or a Window Unasked

## Rule
This development computer is shared with Riley and with MRS-GC sessions. Nothing an agent or script does may take the real mouse, the keyboard, or the foreground window unless Riley has marked the computer free.

## Details
1. Anything that needs the real mouse, keyboard, or foreground game window must acquire the computer lease first via `Wait-QaLease` from `QaLease.ps1`, keep checking `Test-QaLeaseHeld`, stop immediately if revoked, and call `Release-QaLease` when finished.
2. Riley grants or revokes the lease through the T3 Code lease panel or fallback lease window ("It's free for N minutes", standing free hours, or "I need it now"). If no lease panel is running, ask Riley in chat; never assume the computer is free.
3. Background recording (`tools/qa/mp-run.ps1 -Record`) touches no real input and does **not** need a lease.
4. When a task does require the lease, state clearly in plain English why it is needed and how long it will take.
5. Never kill unrelated processes or engines started by Riley or another session.
