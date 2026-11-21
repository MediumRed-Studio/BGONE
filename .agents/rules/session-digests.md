# Session Digests Rule

## Rule
Every agent session that modifies code, scripts, configurations, or repository state must finish with a **Session Digest** posted as a new comment on the primary GitHub issue being worked on (plus one-line reference comments on any other touched issues).

## Format
Session digests must strictly follow this exact 5-field structure:

```markdown
DIGEST (YYYY-MM-DD):
- Intent: <what problem this session was solving>
- Mechanism: <how it was solved, files changed, architectural decisions made>
- Invariants: <what rules, constraints, or invariants were established or maintained>
- Citations: <relevant issue numbers, PR links, commits, or documentation references>
- Remember: <gotchas, lessons learned, or critical context for the next agent>
```

## Guidelines
- **Append-only**: Never edit or overwrite an existing issue description or prior comments to add a digest; always post it as a new comment.
- **Accuracy**: Explicitly note what was verified in-game vs. what was verified only in automated test rigs or remains Workbench-pending.
