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

## Menu owners

Actions declared in a `.ui` file are named by uic. Actions built in code are named where they are
built, after the member they are assigned to; a menu built from data takes a stable prefix plus an
untranslated key - `actionActivity_<act20_e>`, `actionColor_<GPX colour name>`,
`actionWptIcon_<sym>`, `actionSearchWeb_<index>`, `actionAddPoi_<POI name>`. Never the text: it is
translated and is therefore no address.

Done: `CGisListWks` (52), `CGisListDB` (14), `CMouseNormal` (10), `CSearchLineEdit` (8),
`CGeoSearch` (7), `IPlot` (6), `CGeoSearchWeb`, `CActivityTrk`, `CTableTrkInfo`,
`CHistoryListWidget`, `CWptIconManager` (3), `CPlotProfile`, `CTemplateWidget`, `CTextEditWidget`,
`IGisItem`.

## Adapters

A surface with a name of its own is recorded by that name instead of a position. For the plot and
the icon grid that is an upgrade - both recorded as a position before. For a workspace row's tool
buttons it is the only way there is: they are painted into the row, carry no widget, and the press
never reaches the filter as anything but a point in the viewport, so before the adapter such a
click was recorded as the selection it also produced and the toggle was silently lost.

| surface | adapter | state |
|---|---|---|
| canvas | `click` with lat/lon | exists |
| plot | `click` with `x`, the axis' own value - metres on a linear axis, seconds on a time one | done |
| icon grid | `click` with `icon`, the `<sym>` name | done |
| workspace row buttons | `click` with `item` + `button`, the delegate's own button names | done |

## State

Done and regression tested against `doc/shots/test.json`, which comes out byte-identical:

- The replay queue, and the picture taken as the scenario's last step.
- `click`, `dclick`, `trigger`, `menu`, `key`, with a position stored as a fraction of an addressed
  widget and never acted on without the `hit` it carries.
- Recording: a menu entry becomes `trigger`, a context menu request becomes `menu`, and a press the
  state diff had nothing to say about becomes `click`/`dclick`. Verified interactively by the writer.
- A picture of the main window must carry an explicit `size`, else it is a counted failure.
- Deleted: the `details` step, `hasDlgDetails()`, `CMainWindow::closeWidgetTab()`, and the six
  `Details*` exposures.
- A window a scenario opened is tagged as `widget: ""` plus `window: <class>`, and replay refuses
  when another window is on top.
- `IPlot` sets no `objectName` of its own, so a plot built in code is addressed positionally
  (`framePlot/CPlotProfile#0`) and one placed by a `.ui` keeps its uic name. The instance tag the
  track compares its focus owner against is `IPlot::ownerTag`.
- Selecting a workspace item puts a hint on the canvas telling the user to click the map. It is a
  leftover of the selection, so `clear()` takes it back with the rest of them - and a scenario that
  only wants an item's options on the map should click the map, not the workspace row.
- Every menu owner names its actions.
- The three adapters, each verified end to end against a throwaway chapter: a row's visibility
  button greys the project out, a plot click at 2400 lands on index 60 at 2.40 km, and an icon grid
  click by name changes the waypoint's symbol in the tree.
- Noise filtering: `pressIsStep()` records a press only when what it landed on acts on a click - a
  button, a tab bar, a combo box, a header, or an item view row that exists. A splitter handle, a
  dock title, a scroll bar, a menu bar, a label and the empty space under the last row are not
  steps. It is a whitelist: a widget nobody has taught the recorder about records nothing, which is
  recoverable, while a wrong click is a picture of the wrong state.
- `--shoot`/`--doc` and their five companions are parsed under `QMS_DOC_MODE` only, so a user's
  binary rejects them as unknown instead of accepting a switch that does nothing. The values stay on
  `CAppOpts`, empty, so no reader of them needs a branch. qmaptool has its own copy of
  `setup/` and never had them.
- **One record of the window size.** `layout` no longer carries `saveGeometry()`; the size is the
  shot's `size` and nothing else. `shootOne()` puts the window there before the scenario runs, and
  `restoreState()` distributes the dock extents into it - the order is not optional, because
  `saveState()` stores those extents in pixels. A shot the main window sizes and that says nothing
  about how big it was is a counted failure; an exposure is exempt, being built free of the layout.
  Doc mode records the window's size for every live shot, not only for one of the whole window.
  `doc/shots/test.json` came out with zero failures for the first time.

**Migration:** a scenario recorded with the old `details` step fails with *the scenario asks for
"details" which this build does not know*. Record it again through the context menu. A `layout` step
that still carries a `geometry` warns and is ignored; take the key out and give every shot of that
scenario the size the geometry used to restore - `doc/shots/test.json` was migrated that way, and
its pictures came out byte-identical apart from `track-scropt`, which had been failing.

## Open

- **`track-select` does not replay.** Both its geographic clicks fail with *"has nothing to show at
  that point"*, on a single replay, not only a repeated one. The scenario triggers `actionRangeTrk`
  first, and a `click` carrying a lat/lon resolves an item through the normal mouse handling, which
  is not what is on the canvas in range mode. Either the geographic click has to reach the range
  delegate, or range selection needs a vocabulary of its own - a distance along the track, as the
  plot has. Nothing photographs the scenario, so nothing fails because of it yet.

Everything else is in: the vocabulary, the adapters, the noise filter, the option gating, one record
of the window size, and a test chapter that reproduces byte-identically with no failures.
