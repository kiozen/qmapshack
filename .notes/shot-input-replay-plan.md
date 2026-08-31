# Scenarios from recorded input

Replace `CShotRecorder`'s state differ with a recorder that stores addressed input. Wants its own
branch.

## Why

`inputsOf()` iterates eight hard-coded widget classes and `captureChanges()` a fixed set of state
kinds, so the vocabulary is indexed by QMapShack's features and a new kind of picture can cost a
code change. Input is indexed by Qt: a click is a click whatever it lands on. The adapters that
keep a recording readable — view row to item path, action to objectName, tab bar to tab name — are
a list of Qt classes and do not grow with the application.

Writers cannot write code, so the recorder is not optional. Squish is not GPL, so it is not an
option either.

## Measured

Taken with a throwaway probe that drove the running application through its own UI, on Linux and
Windows, 13 checks of 13 passing on both. The probe is deleted; this section is what it was for.

- `QTest::mouseClick` fires `clicked()` synchronously in a normal application, offscreen.
  `QAccessibleActionInterface::doAction("Press")` does not — `animateClick()` is behind it.
- A step scheduled on the event loop runs **inside** `exec()`. A modal dialog is addressable,
  renderable and closable from there. `CDetailsWpt` came out 550x473 that way.
- `QContextMenuEvent` must be sent to the **viewport**. Sent to the scroll area,
  `customContextMenuRequested` does not fire even with `Qt::CustomContextMenu` set.
- `QAbstractItemView` drops a double click whose index does not match one a press recorded. A plain
  click has to precede it or nothing is emitted and the coordinates look wrong when they are right.
- `QApplication::activePopupWidget()` needs window activation, which an offscreen run has none of.
  A visible `QMenu` among `topLevelWidgets()` is the fact underneath it.
- A collapsed tree row's children are not in the accessibility tree, so `QAccessible` alone cannot
  address the workspace. `itemPathOf()`/`resolveItemPath()` stay.
- `--dpr 2` changes nothing but text antialiasing. Same image sizes, same layout, same icons.
- `sizeHint`-driven pictures match across platforms exactly (menu 234x372, dialog 550x473). A
  picture sized by whatever the window grew to does not: 1660x741 on Linux, 1482x741 on Windows.

## Rules that fall out of the measurements

- **A window-sized picture must carry an explicit `size`.** Never render at the size the window
  happens to have; widget metrics differ per platform.
- **Address, never a global pixel.** A position is only ever stored relative to an addressed
  widget, for the surfaces that have no vocabulary of their own.
- **A position is never stored alone.** It carries what it hit, and replay verifies the hit before
  acting; a miss is a counted failure, not a picture of the wrong state. The geographic `click`
  already works this way — item path plus lat/lon, item resolved first. A widget metric moving 2 px
  between Linux and Windows is measured, and on a 17 px delegate button that is enough to miss.
- **Every step waits for a condition**, never for a duration.

## Vocabulary

Kept as they are: `layout`, `view`, the geographic `click`, `set`, `select`, `expand`. Those are
state with no discrete input behind them, or already address the right thing.

New:

| action | payload | replays as |
|---|---|---|
| `click` | widget address, optional relative position | `QTest::mouseClick` |
| `dclick` | item path, or widget address + position | plain click, then `QTest::mouseDClick` |
| `trigger` | action objectName | `QAction::trigger()` |
| `menu` | widget address + item path or position | `QContextMenuEvent` to the viewport |
| `key` | widget address + key sequence | `QTest::keyClick`/`keyClicks` |

Dropped: `details` (superseded by `dclick`/`trigger`), and every exposure reachable through the UI.

## Addressing

- Widgets: `CShotChapter::addressOf()` — objectName, else class and index below the nearest named
  ancestor. Unchanged.
- Workspace rows: `itemPathOf()` / `resolveItemPath()`. Unchanged.
- Actions: objectName. `CGisListWks` carries 52 of 52; every other menu owner needs the same audit
  before a recording can name its entries.

## Replay

`CShotRecorder::replay()` becomes a queue: each step schedules the next from the event loop instead
of a `for` loop calling them in sequence. That is what lets a step enter a modal dialog, a popup
menu or a nested progress loop while the steps after it still run, and it is what makes the whole
exposure catalog unnecessary.

Between steps: `QTest::qWaitFor` on a condition, plus the existing `CShotWriter::settle()` and
`settleStable()` for a canvas.

## Recording

The event filter already sees every press and release. It stops diffing afterwards and instead
resolves the press target to the nearest addressable widget, with the per-class adapters above
turning it into an item path, an action name or a tab name where one exists.

## What goes

- `inputsOf()`, `captureChanges()`'s input and details diffs.
- Most of `CShotExposures.cpp`. An exposure survives only for a widget that never appears through
  the UI — a progress dialog caught mid-operation, an error report.
- `CGisItemTrk::hasDlgDetails()` / `IGisProject::hasDlgDetails()` and
  `CMainWindow::closeWidgetTab()` are only needed while the `details` action exists.

## Menu owners whose actions are still unnamed

Actions declared in a `.ui` file are named by uic. Actions built in code are not, and
`QMenu::addAction(tr(...))` returns one that never can be. Counted over `src/qmapshack`:

| owner | unnamed | what it is |
|---|---|---|
| `CGisListDB` | 14 | database tree context menu |
| `CMouseNormal` | 10 | map context menu and screen options |
| `CSearchLineEdit` | 8 | search field menu |
| `IPlot` | 6 | profile and graph context menu |
| `CGeoSearch` | 3 | geo search results |
| `CTableTrkInfo`, `CHistoryListWidget`, `CWptIconManager`, `CActivityTrk`, `CGeoSearchWeb` | 2 each | |
| `CTextEditWidget`, `CTemplateWidget`, `CPlotProfile`, `IGisItem` | 1 each | |

About 55 sites in 14 files. `CGisListDB`, `CMouseNormal` and `IPlot` are the three a chapter is
likely to need. Same treatment as `CGisListWks`: name the action after the member it is assigned
to, and give the local `addAction` helpers an id parameter where one exists.

## Adapters

A surface with a name of its own is recorded by that name instead of a position. None of these is a
prerequisite: the recorder lands with none of them and every click still records, verified. An
adapter only upgrades a step from a position to a name.

| surface | adapter | state |
|---|---|---|
| canvas | `click` with lat/lon | exists |
| plot | distance or time along the track — `setMouseFocusByDistance`/`ByTime`, `idxTotal` | vocabulary exists, not wired |
| icon grid | `sigIconName` / `sigSelectedIcon` | vocabulary exists, not wired |
| delegate buttons | extract a `buttonAt()` returning the role from `mousePressProject/Device/GeoSearch` | 3–4 functions |

## Open

- Noise filtering: the filter sees hovers and scrolls that are not steps.
- `--shoot`/`--doc` are still parsed in `src/common/setup/`, where the switch does not reach. They
  do nothing without the subsystem; gating the parsing too would touch qmaptool's options as well.
