# Reference Vanilla Source Rule

## Core Rules

1. **Source of Truth:**
   - Always reference the pinned Arma Reforger 1.9.0.37 vanilla script source at `Z:/Reforger Source 1.9.0.37/scripts` first.
   - Check the actual vanilla class definitions, method signatures, attributes, and component lifecycles before writing or reviewing code against engine APIs.

2. **Never Guess Missing APIs:**
   - If you cannot locate a specific vanilla source file (for example `SCR_WeaponComponent.c` or `RplComponent.c`) using targeted searches:
   - STOP immediately.
   - Do **not** guess method signatures, enum values, or engine callback ordering.
   - Ask Riley to provide the file path or paste the relevant definition before proceeding.
