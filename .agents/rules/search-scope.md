# Search Scope Rule: Never Search a Whole Drive

## Rule
Search only the folder that can hold the answer. Never run a recursive search (`grep -r`, `rg`, `find`, `Get-ChildItem -Recurse`, `dir /s`) on a drive root (`Z:/`, `C:/`) or a large parent folder (`C:/wt`, `Z:/SteamLibrary`).

## Where to Look
- **Our code:** your own worktree checkout only.
- **Vanilla game scripts:** `Z:/Reforger Source 1.9.0.37/scripts` (see `reference-source.md`), narrowed to the relevant subfolder when known (e.g. `Z:/Reforger Source 1.9.0.37/scripts/Game/Weapon`).
- **Other repos or assets:** that repo's own folder, never its parent directory.

## Why
`Z:` is a shared disk holding game installs, QA rigs, Steam libraries, and many checkouts. Unscoped recursive scans saturate disk I/O for hours and stall every QA run on the machine. A timed-out shell search also keeps running in the background after the tool call gives up.

## Details
1. Always pass an explicit narrow path to search commands; never rely on a broad working directory.
2. Put a time limit on potentially large searches instead of letting a tool call hang.
3. Never leave `tail -f` or `tail -F` watchers running. On Windows they can lock log files and outlive the session that started them.
4. Only stop processes you started yourself, or processes Riley explicitly approved by PID (`.agents/rules/shared-computer.md`).
