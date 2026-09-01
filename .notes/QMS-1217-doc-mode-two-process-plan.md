# Documentation mode: two processes

Replaces section 7 of `QMS-1217-screenshot-framework-plan.md`. Everything else there still holds.

## Why

The writer's process sets a scenario up by restarting itself. That single decision produced the
restart handshake through the scratch configuration, the launcher loop in `shots.py`, the panel that
must refuse to close so the writer cannot lose it, the lost picture selection, and the hang where
`qApp->quit()` is answered with `closeAllWindows()` that the panel then vetoes.

The headless build never had any of it, because it runs one process per scenario and throws it away.

## Shape

Two processes, one window each.

```
qmapshack --doc <repo> --doc-chapter <ch>                    the launcher: the panel, nothing else
    └── qmapshack --doc <repo> --doc-chapter <ch>            the state: one scenario, thrown away
                  --doc-scenario <name|-> --config <ini>
```

- **The launcher owns the panel and never holds application state.** It reads the chapter file,
  shows the pictures and their previews, and does every operation that is a file operation:
  rename, delete, rebind, reap. Its main window is constructed but never shown - `CMainWindow::self()`
  is what initialises `IUnit`, `CWptIconManager` and eight more singletons the panel's data touches.
- **The state process is disposable.** It comes up in one scenario, replays it, and stays for as
  long as the writer works in it. Switching scenarios kills it and starts another. Nothing is ever
  taken back down, which is the guarantee the restart was for.
- **The panel keeps every button.** The state process has no UI of its own; the launcher writes one
  line to its stdin and it acts.

## The channel

Line protocol, launcher to state on stdin:

| Command | Meaning |
|---|---|
| `region` | photograph a rectangle the writer drags |
| `update` | store the running settings and arrangement into this scenario |
| `record` / `stop` | start, then store what the writer performed |
| `retake` | take every picture of this chapter again, here |

State to launcher on stdout, one line each: `status <text>`, `tagged <id>`, `recorded <name>`,
`done`. Anything else the state process prints is a log line and is passed through.

`Ctrl+Shift+F9` stays in the state process: it owns the window the writer is pointing at.

## What the launcher must not do

- No fixture, no replay, no `CShotWriter`. A picture is always taken by the state process, so what
  the writer accepts is what a build reproduces.
- No configuration handshake. There is no request to write, because there is no restart.

## Configuration

`shots.py compose <chapter> [<scenario>] --out <ini>` is the one implementation. The launcher runs
it before spawning, so the doc path and the build path cannot drift apart.

## What this deletes

`requestRestart()`, `openInScenario`, `openRecording`, `openShot`, `liveScenario`, the
`Documentation/restart*` keys, the loop in `cmd_doc`, and `CShotDocPanel::closeEvent()`'s veto.

## What it does not fix

Message boxes, progress dialogs and WebEngine views stay out of reach - see section 8.4 of the main
plan. Cross-machine byte-stability is still unmeasured.
