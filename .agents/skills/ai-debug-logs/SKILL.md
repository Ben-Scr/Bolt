---
name: ai-debug-logs
description: Read structured JSON Lines log output from a project that has AI-debug logging enabled. Use this whenever the user reports a bug or unexpected behaviour and you need to see what the engine logged during a recent session — instead of asking them to paste console output. Triggers on "check the logs", "what went wrong in the editor", "the editor crashed", "I see a warning but can't read the console", or any debugging task where structured log data would be more reliable than asking the user to scroll a terminal. Also use when authoring new logging code in a project that uses this convention so the schema stays stable.
---

# AI-Debug Logs

A convention for "machine-readable logs an AI agent can consume directly". Designed first for the Axiom engine; the format is engine-agnostic and reusable in any project that hooks a logging library.

## What it gives you

A file at `<projectRoot>/.axiom/logs/<sessionId>_<process>.jsonl` containing every log record emitted during a session — one JSON object per line. You can `Grep` for specific levels, tags, or messages without parsing colour codes or wrapping a regex around timestamp formats.

A pointer file at `<projectRoot>/.axiom/logs/current.txt` holds the filename of the most-recently-started log so you can find the active session without listing the directory.

## Schema (stable)

Every record is a single-line JSON object with exactly these fields:

```json
{"ts":"2026-05-04T17:42:11.382Z","sess":"2026-05-04T17-42-09Z","proc":"Application","lvl":"warn","tag":"Asset","thr":15244,"msg":"Texture asset UUID 161… could not be resolved to a path."}
```

| Field  | Type    | Meaning |
|--------|---------|---------|
| `ts`   | string  | ISO 8601 UTC timestamp with millisecond precision. Always sortable as text. |
| `sess` | string  | Session ID. Same string across processes spawned together (set via `AXIOM_SESSION_ID` env var) so you can correlate editor + runtime + native package logs. |
| `proc` | string  | Process name passed by the application (`Engine`, `Application`, `Editor`, `Runtime`, ...). Useful when one log file mixes processes (rare) or when filtering across multiple files. |
| `lvl`  | string  | One of: `trace`, `debug`, `info`, `warn`, `error`, `critical`. |
| `tag`  | string  | Logger name + tag. Examples: `AXIOM`, `APP`, `EDITOR`, or a sub-system tag like `Asset`, `FileWatcher`, `Renderer2D`. |
| `thr`  | integer | Native thread id that emitted the record. |
| `msg`  | string  | The fully-formatted message. JSON-escaped (control chars and quotes safe). |

The schema is **append-only**: future fields (e.g. `src` for source location once the macros plumb it through) will be added without removing existing ones. An agent reading these files should access fields by name, not position.

## Activation

Three toggles, in priority order. Highest priority wins for the current process.

1. **Env var** `AXIOM_AI_LOGS=1` — forces it on for that process. Useful for headless runs and CI.
2. **Per-project setting** in `axiom-project.json`: `"aiDebugLogs": true`. Persists with the project; flipped from the editor's settings menu.
3. **Off** (default).

For cross-process correlation, set `AXIOM_SESSION_ID=<your-id>` before spawning child processes — each child uses that string in its `sess` field.

## How an agent reads logs

### "What's in the most recent session?"

```
Read <projectRoot>/.axiom/logs/current.txt   (one filename, e.g. "2026-05-04T17-42-09Z_Editor.jsonl")
Read <projectRoot>/.axiom/logs/<that filename>
```

### "Just errors and warnings, please"

```
Grep '"lvl":"(warn|error|critical)"' --path .axiom/logs/<file>.jsonl
```

### "All Asset-related records"

```
Grep '"tag":"Asset"' --path .axiom/logs/<file>.jsonl
```

### "Correlate editor + runtime in one play session"

```
Read .axiom/logs/<sessionId>_*.jsonl     # one per process, same sessionId prefix
```

### Pretty-print one record

```
echo '{"ts":...}' | jq .
```

### Filter to a time window

```
Grep '"ts":"2026-05-04T17:4[2-5]' --path .axiom/logs/<file>.jsonl
```

## When to read logs proactively

Reach for these files **without asking the user** when:

- The user reports an editor bug ("the texture field doesn't work", "the build profile didn't apply").
- The user asks "why did X happen" and X is something the engine would log.
- You've just shipped a code change and want to confirm a path executed.
- A subagent reports it ran the engine — read the log instead of trusting the subagent's narrative.

Don't reach for them when:

- The user is asking a code question, not a bug report.
- The bug is reproducible in your head from the code alone.
- The project hasn't enabled AI-debug logs (no `.axiom/logs/` directory exists).

## Engine-side hooks (for reference when authoring new logging)

The engine's `Log` static API:

- `Log::EnableAiDebugLogs(projectRoot, processName, sessionId = "")` — attaches the JSONL sink. `processName` becomes the `proc` field; pass `"Editor"` from the editor binary, `"Runtime"` from runtime, etc.
- `Log::DisableAiDebugLogs()` — detaches and flushes.
- `Log::IsAiDebugLogsEnabled()` — query.
- `Log::GetAiDebugLogPath()` — absolute path to the active file.

`AXIOM_AI_LOGS=1` activates at engine init time; `Log::EnableAiDebugLogs` is also called by `ProjectManager::SetCurrentProject` whenever a project with `aiDebugLogs: true` is loaded.

## Adapting this convention to a new project

Pattern is portable to any C++/C#/Python project. The minimum viable port:

1. Pick a stable directory: `<projectRoot>/.axiom/logs/` (or rename `.axiom` to your project's namespace).
2. Add `<that-dir>/` to `.gitignore`.
3. Hook your logging library so every record produces one JSON line in that directory.
4. Match the field names above. Add project-specific fields with prefixes (e.g. `aim_module`) to avoid clashing with future schema additions.
5. Maintain `current.txt` so agents can find the active file in one read.
6. Off by default; activate via env var + persisted project flag.

The skill stays useful as long as the schema is preserved.
