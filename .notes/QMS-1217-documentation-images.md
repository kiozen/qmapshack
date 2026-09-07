# QMS-1217 — documentation images

Issue: Maproom/qmapshack#1217. Related: discussion #1209 (documentation rework).

Goal: every image in the user documentation is build output, regenerable by one command on Linux,
Windows and macOS, per language.

Status: a throwaway demo on branch `QMS-1217_demo`. It renders a real chapter, a writer can use it,
and a scenario is recorded rather than registered. The fixture, cross-machine determinism and the
language loop are not built.

This file replaces `QMS-1217-screenshot-framework-plan.md`,
`QMS-1217-doc-mode-two-process-plan.md` and `shot-input-replay-plan.md`. They were three layers of
one plan, each superseding a section of the one before, and the layers had begun to contradict each
other and the code.

**How to read it.** §1-§7 are the design as it now stands - what the demo showed works. §8 are the
rules that fall out of it. §9 is evidence, measured on this checkout; do not re-derive it and do not
doubt it without a new measurement. §10 is what the demo does not do. §11 is the feature branch: the
demo is thrown away and built again as sub-tickets, each one reviewable on its own. §12 is out of
scope.

---

## 1. The units

A **chapter** is one page, one JSON shot file, one directory of images:

```
doc/pages/<chapter>.md              the page, MyST, references ../images/<chapter>/<name>.png
doc/shots/<chapter>.json            the pictures and the scenarios, written by the application
doc/shots/<chapter>/<scenario>.ini  one scenario's whole configuration
doc/shots/fixture/shots.ini         the base every chapter opens on
doc/images/<chapter>/               the pictures
doc/shots/_cache/                   per-run tile cache and workspace database, git-ignored
```

The chapter is the unit because the application a user sees is not the application at first start:
dockers get rearranged, a writer tunes the layout to make a picture focused, and a setup chapter's
configuration changes *during* the chapter.

Inside a chapter the unit is the **scenario**: a picture is taken in one, and a scenario owns the
arrangement, the map and the settings it is taken with. Its pictures are a sequence of states, not
independent stills.

**`(base)` is a row, not a scenario.** It heads the scenario list and every *Taken in* box, is
stored nowhere, and a picture taken in it simply has no `scenario` key. A per-chapter copy of the
start state can go stale; the start state cannot. `--shoot-scenario -` is how a build asks for that
group, and `-` is refused as a scenario name.

**One base, a scenario may only add.** Maps and DEM are shared and nothing changes them; example
data is one base project a chapter may add a file *on top of*. Data that needs to *change* the base
means the base is wrong - fix the base. Storing a scenario's setup is an explicit button, never
automatic, or the setups drift with every session.

**A scenario's `.ini` is a whole configuration, never a patch on the base.** A file holding a
difference would move whenever the base moved, and a picture already taken would silently stop
reproducing. Storing a base changes what a chapter opens on and what the next recording starts
from, and nothing else.

## 2. A shot is data

`doc/shots/<chapter>.json`, written by documentation mode through `CShotChapter::store()`:

```json
{
  "shots": [
    { "id": "test/menu-setup-workspace", "widget": "menuProject" },
    { "id": "test/workspace-setup-database", "exposure": "SetupWorkspace",
      "set": { "tabWidget.currentIndex": 2 }, "size": [620, 471] },
    { "id": "test/track-scropt", "scenario": "track-screen-option", "widget": "",
      "size": [1666, 741], "rect": [552, 144, 600, 266] }
  ],
  "scenarios": {
    "track-range": [
      { "do": "layout", "state": "…", "tab": 0 },
      { "do": "view", "lat": 47.5, "lon": 11.0099, "zoom": 11 },
      { "do": "select", "item": "Shoot Demo/trk:Demo Track" },
      { "do": "click", "item": "Shoot Demo/trk:Demo Track", "lat": 47.5048, "lon": 11.0085 },
      { "do": "click", "widget": "toolRange", "hit": "toolRange", "at": [0.54, 0.56] },
      { "do": "click", "lat": 47.4990, "lon": 10.9930 }
    ]
  }
}
```

| Key | Means |
|---|---|
| `id` | file stem, `<chapter>/<name>`; also what the page references |
| `widget` | address in the running application; empty is whatever is on top, else the main window |
| `window` | the class a scenario put on top, so a scenario that stops opening it fails loudly |
| `exposure` | build a fresh instance from the exposure catalog instead |
| `scenario` | the recording this picture is taken in; absent means `(base)` |
| `set` | properties to drive, `child.property` or a bare property of the shot's own widget |
| `size` | explicit render size - the window's, whatever part of it is photographed |
| `rect` | the part of the result that is kept |
| `note` | JSON has no comments |

JSON, not YAML: `QJsonDocument` is in Qt and `json` is in the Python standard library, and
`QJsonObject` sorts its keys, so a machine-written file diffs stably.

**The `.md` is what says a picture exists.** The chapter file only says how it is taken. F9 offers
the names the page asks for and has not got, and only those.

**The window's size has exactly one record: the shot's `size`.** `layout` carries `saveState()` and
the central tab index, never `saveGeometry()`. `shootOne()` resizes the window *before* the scenario
runs and `restoreState()` distributes the dock extents into it - that order is required, because
those extents are pixels. Everything the scenario produces is measured against that size: where the
map is centred, where an item's options are anchored, what a stored rectangle frames. A shot the
main window sizes and that says nothing about how big it was is a counted failure; an exposure is
exempt, being built free of the layout.

**A rectangle is not a kind of shot**, it is what is kept of one. Whatever produced the picture -
a widget, an exposure, a scenario - the rectangle is cut out of the result, which is what makes a
region of something dynamic possible at all.

**Losing or changing a shot's scenario reduces the entry to its bare `id` and deletes the image.**
A widget address and a rectangle frame something else in another state.

## 3. Architecture

```
src/qmapshack/shoot/
  CShotRegistry     the exposed widget classes; SHOT_EXPOSE
  CShotContext      the live application and the fixture by role; shot() and frame()
  CShotWriter       settle, resize, grab, PNG
  CShotChapter      the JSON chapter: addressing, item paths, store, shootOne, run
  CShotRecorder     records what the writer performs, and replays it
  CShotFixture      the example project
  CShotRunner       --shoot tasks; writes a JSON report beside the images
  CShotDocLauncher  the writer's session: the panel and every file operation
  CShotDocMode      the state process: F9, the shot dialogs, the channel
  CShotDocPanel     the writer's panel
```

**Developer-only.** `shoot/` is compiled and `Qt6::Test` linked only under `-DQMS_DOC_MODE=ON`, and
`main.cpp` compiles its two call sites out with the same define, so `--shoot` and `--doc` are inert
in a user's binary. The switches are parsed under the same define, so a user's binary rejects them
as unknown instead of accepting a switch that does nothing; the values stay on `CAppOpts`, empty, so
no reader of them needs a branch.

**Two drivers, one render path.** Documentation mode (`--doc`) and the headless run (`--shoot`) load
the same configuration and the same fixture and go through the same `CShotChapter::shootOne()` and
`CShotWriter`, so the picture a writer accepts is what a later replay reproduces.

**Documentation mode is two processes.**

```
qmapshack --doc <repo> --doc-chapter <ch>                 the launcher: the panel, nothing else
    └── qmapshack --doc … --doc-scenario <name|-> …       the state: one scenario, thrown away
```

- The launcher owns the session - which chapter, which scenario, which picture, what a retake
  changed - and does every operation that is a file operation: the base question, the recording's
  name, delete, rebind, reap. Its main window is constructed and never shown; `CMainWindow::self()`
  is what initialises `IUnit`, `CWptIconManager`, `CGisWorkspace` and eight more singletons the
  panel's data goes through.
- The state process is disposable. It comes up in one scenario, replays it, and stays for as long
  as the writer works in it. Picking another kills it and starts another. **Nothing is ever taken
  back down** - that is the guarantee, and it is what `reset()` could not give: the application's
  state is not enumerable, so a list of things to put back is never complete.
- The state process owns nothing but the window it is pointed at. `Ctrl+Shift+F9` stays there.
- They talk over a `QLocalSocket`, `--doc-channel`. Launcher to state: `region`, `update`, `retake`,
  `record`, `stop`, `sync`. State to launcher: `status`, `ready`, `tagged`, `changed`, `recording`,
  `recorded`.
- Closing either window ends the session. The state process quits when its window closes; the
  launcher quits when the state ends without it having asked (`killing`) and when its panel is
  closed. A state process whose channel drops quits too, so a killed launcher leaves no orphan.
- The launcher must never render: no fixture, no replay, no `CShotWriter`. A picture is always taken
  by the state process.

**The exposure catalog is the only C++ that grows.** `CShotExposures.cpp`, 52 entries, because a
constructor's arguments cannot be data. Only 9 of the 61 dialogs take nothing but a parent; the rest
want a fixture item, a singleton alive inside `CMainWindow`, or a result the dialog writes back
through a reference - the last kind gets a `scratch<T>()` static that outlives the shot. An entry is
one line and is paid once per class, never per image. Keyed by `typeid`, not the meta object: three
exposed dialogs have no `Q_OBJECT` and would all report their base class name.

All 52 entries are `QDialog` subclasses; 52 of the tree's 61 are exposed. The nine that are not:
`CDetailsGeoCache`, `CDetailsOvlArea`, `CDetailsRte` and `CDetailsWpt`, which a scenario reaches
through `dclick`/`trigger` and which therefore need no entry; `CExportDatabase` and
`CSearchDatabase`, which need a live `QSqlDatabase` with content; `CRangeToolSetup`, which exists
only while the range mouse mode is active; and `CTemplateWidget` and `CShotDocPanel`, which are not
user-facing.

There are no scenario recipes. `IShotRecipe` and `RecipesChapter.cpp` are gone: a recipe was written
per *picture*, so the catalog grew with the documentation and had to be read to be used. A recorder
is asked for per *interaction kind*, and that list is §10.4's table.

A rename is caught by `shots.py chapter` failing loudly, not by the compiler - `"dockWorkspace"` and
`"Shoot Demo/trk:Demo Track"` are strings either way, so keeping shots in C++ would protect nothing.

## 4. Recording

`CShotRecorder` watches through one event filter. What is stored is **addressed input**, not a state
diff: a click is a click whatever it lands on, so the vocabulary is indexed by Qt and not by
QMapShack's features. The adapters that keep a recording readable are a list of Qt classes and do
not grow with the application.

Writers cannot write code, so the recorder is not optional. Squish is not GPL, so it is not an
option either.

| action | payload | replays as |
|---|---|---|
| `layout` | `saveState()` + the central tab index | `restoreState()`, tab last |
| `view` | centre in degrees + zoom index | `zoom(index)` + `moveMap()` |
| `select`, `expand` | item name path | the workspace list |
| `set` | widget address, property, value | `driveProperty()` |
| `click` | widget address + `at` fraction + `hit`, or a vocabulary of its own | `QTest::mouseClick` |
| `dclick` | item path, or widget address + position | plain click, then `QTest::mouseDClick` |
| `trigger` | action objectName | `QAction::trigger()` |
| `menu` | widget address + item path or position | `QContextMenuEvent` to the viewport |
| `key` | widget address + key sequence | `QTest::keyClick`/`keyClicks` |

`layout` and `view` are taken whole when the writer stops, and put in front. That is the replay
order too: the arrangement decides how big the canvas is, the view decides what it looks at, and
only then does a geographic point mean the pixel the writer clicked.

**Address, never a global pixel.** A position is only ever stored relative to an addressed widget,
and only for surfaces with no vocabulary of their own.

**A position is never stored alone.** It carries what it hit, and replay verifies the hit before
acting; a miss is a counted failure, not a picture of the wrong state. A widget metric moving 2 px
between Linux and Windows is measured, and on a 17 px delegate button that is enough to miss.

**A surface with a name of its own is recorded by that name.**

| surface | recorded as |
|---|---|
| canvas | `click` with lat/lon, plus the item path when exactly one thing is under it |
| `IPlot` | `click` with `x`, the axis' own value - metres on a linear axis, seconds on a time one |
| `CIconGrid` | `click` with `icon`, the `<sym>` name |
| workspace row buttons | `click` with `item` + `button`, the delegate's own button names |

The row buttons are painted into the row and carry no widget, so the press never reaches the filter
as anything but a point in the viewport; before the adapter such a click was recorded as the
selection it also produced and the toggle was silently lost. A replay reaches `pressButton()`
directly rather than a point.

**An `IScrOpt` overlay is a child widget of the canvas, not the map.** Only `CCanvas` itself is a
geographic point; a press on `toolRange` or any other screen-option button is an ordinary addressed
widget press.

**A press is a step only when what it landed on acts on a click** (`pressIsStep()`): a button, a tab
bar, a combo box, a header, or an item view row that exists. A splitter handle, a dock title, a
scroll bar, a menu bar, a label and the empty space under the last row are not. It is a whitelist,
so a widget nobody has taught the recorder about records nothing - recoverable, where a wrong click
is a picture of the wrong state.

**A menu entry is addressed by its action's `objectName`, never its text**, which is translated.
Every menu owner names its actions after the member they are assigned to; a menu built from data
takes a stable prefix plus an untranslated key: `actionActivity_<act20_e>`,
`actionColor_<GPX colour name>`, `actionWptIcon_<sym>`, `actionSearchWeb_<index>`,
`actionAddPoi_<POI name>`. A new `addAction` needs the same.

Done: `CGisListWks` (52), `CGisListDB` (14), `CMouseNormal` (10), `CSearchLineEdit` (8),
`CGeoSearch` (7), `IPlot` (6), `CGeoSearchWeb`, `CActivityTrk`, `CTableTrkInfo`,
`CHistoryListWidget`, `CWptIconManager` (3), `CPlotProfile`, `CTemplateWidget`, `CTextEditWidget`,
`IGisItem`.

**A recording always starts from the base**, so a scenario is a whole state and never a difference
from another scenario that is not stored with it.

**Where a recording stops:**

- A hover, a tooltip, a half-finished drag.
- The eight menus built as stack locals ending in `exec()`; they need the `buildMenuXxx(QMenu&)`
  split, done once for `CGisListWks::buildMenuItemTrk`.
- A modal dialog opened during a recording. Replay refuses `details`-shaped steps so a headless run
  cannot sit in one; such a widget is photographed through the exposure catalog.
- Whether a recording should be re-recordable in place. Today a changed scenario is a new recording
  under the same name.

## 5. Replay

`CShotRecorder::replay()` is a queue: each step schedules the next from the event loop instead of a
`for` loop calling them in sequence. That is what lets a step enter a modal dialog, a popup menu or
a nested progress loop while the steps after it still run, and it is what made the exposure catalog
unnecessary for anything reachable through the UI.

- **The picture is taken as the scenario's last step, never after it.** A step that opened a modal
  dialog is still inside its `exec()` there; by the time `replay()` returns the dialog is gone and
  the picture would be of the window behind it.
- **A replay starts from nothing, not from what is on screen.** `replay()` calls `clear()` before
  its first step. A step is not idempotent - a second click on a selected range takes the range
  away - and documentation mode replays a scenario it is already holding: once for a picture, twice
  more for a rectangle.
- **`clear()` takes back what a replay leaves**: the screen options, the canvas' mouse delegate
  (`CCanvas::resetMouse()`, what a right button click does), a track's mouse focus, and the hint a
  selection puts on the map. It runs before every replay, and after a shot unless the context is
  holding the state up for a writer.
- **A recorded click is replayed as a click.** A geographic point reaches the canvas as a move, a
  press and a release, so whichever mouse delegate the scenario put there answers it - normal,
  range, edit, ruler - and a mode nobody has taught the framework about replays like every other.
  The point is approached from `kApproachPixels` away and the canvas repaints before the button goes
  down, because the hover an overlay needs is computed while the canvas paints.
- **The arrangement's `tab` index is applied after every other step**, because a page a step adds
  does not exist while `restoreState()` runs, and `QTabWidget` drops an index past its last page
  without a word.
- **Every step waits for a condition**, never for a duration: `CShotWriter::settle()`,
  `settleStable()`, `CCanvas::waitForDrawContexts()`.
- **`driveProperty()` reads the value back.** `setProperty()` answers whether the property exists,
  never whether the value took.

## 6. The writer's loop

`shots.py doc <chapter>` opens the launcher. `doc/WRITING.md` is the writer's guide.

**Two sections, one selection.** Scenarios are recorded, renamed and deleted at the top; the
pictures the page asks for are listed below, each row carrying the scenario it is taken in, in a
combo box of its own. A picture is taken in the scenario its own row names, and a name that has no
row yet is taken in the selected one.

**Ctrl+Shift+F9 is the only key** - photograph what the mouse points at. Everything else is a
button, because the mouse is busy pointing:

| Section | Button | Does |
|---|---|---|
| Scenarios | Record… | perform the state a picture needs; stop and it becomes the chapter's own |
| | Rename… | another name; no picture is invalidated |
| | Delete | throw it away, and with it every picture taken in it |
| | Save config | store the arrangement, the size, the map and the settings on screen into the selected scenario; on `(base)` it asks first |
| Pictures | Take a region… | drag a rectangle over the window in the picture's own scenario |
| | Take all again | take every picture of the chapter again, here, and report which came out different |
| | Remove unused | delete the pictures no page references |

F9 starts at the widget under the mouse and offers every step up to the whole window that a shot can
find again, renders a **fresh** instance through the headless path, and shows it to keep or throw
away. A class with no exposure prints the one line a developer has to add.

The panel lists what the page asks for against what exists: *taken*, *missing*, *no image*, *not
used*. A retake reports which pictures came out different, which is the only signal that a replay no
longer reproduces what the writer accepted.

**Starting a state takes about seven seconds**, because it is a whole application. The panel says so
and refuses input while it happens; without that the writer clicks again and the clicks queue up
behind a process that is still coming up.

**Never make the panel refuse to close.** It did, to keep the writer from losing it, and that turned
`qApp->quit()` - which `QGuiApplication` answers with `closeAllWindows()` - into a quit that never
happened.

## 7. shots.py

`doc/tools/shots.py`. Python because #1209 already requires every writer to have it and the three
platforms rule out a shell script.

| Command | Does |
|---|---|
| `doc [CHAPTER]` | open the launcher so a writer can take pictures |
| `compose <chapter> [--scenario <name>] --out <ini>` | the one composer of a run's configuration |
| `chapter [NAME]` | shoot one chapter file, no window, one process per scenario |
| `build [--only GLOB]` | take every chapter's pictures again |
| `list` | the exposed widget classes |
| `inspect <id>` | a widget's children, types and settable properties |
| `explore <id>` | which controls actually change the widget |
| `reap [--delete]` | pictures no page references any more |

It composes the run's configuration into a scratch copy - the scenario's own file, or the base when
it has none - so a writer's session cannot drift what a build renders. The launcher runs the same
`compose` before starting a state process, so the writer's session and the build cannot disagree.

**What the tool owns is injected per run, never stored**: `Canvas/{mapPath,demPaths,poiPaths}` from
`doc/shots/fixture/`, absolute and therefore uncommittable, and `Database/saveOnExit=false` -
without it a run saves its workspace and the next one loads the demo project twice.

It pins `-style Fusion`, `--font-family DejaVu Sans`, `--font-size 10`, `--color-scheme light`,
`--locale en` with `LC_ALL`/`LANG`, `-platform-theme generic`, `TZ=UTC` and an unset
`QT_SCALE_FACTOR`, and prints which configuration file it read. Every task writes a JSON report
beside the images, so nothing parses stdout.

`diff` and `update` are deliberately absent: they mean nothing until the output is byte-stable.

## 8. Rules that hold

- **An exposure never calls `exec()`.** Construct, polish, size, render. That one rule is what makes
  menus, message boxes and modal dialogs tractable at all.
- **Drive inputs, never outputs.** Set `comboColorSource->setCurrentIndex(2)` and let the
  application's own signal chain produce the state. Setting a visibility directly can produce a
  state no user can reach and bakes today's logic into the shot; driving the input means that when
  the logic changes the shot does not become wrong, it renders differently.
- **Never reload the configuration in a running application.** 168 `SETTINGS` uses; the survey of
  2026-08-25 put 40 of them in constructors, 27 in destructors, 26 in slots and 9 in
  `loadSettings()`, and the shape has not changed. The dock layout is
  `restoreGeometry`/`restoreState` in the `CMainWindow` constructor. **One process per distinct
  configuration state** instead: startup is 1.3 s, so ~18 s for a ten-picture page and ~20 min for
  690, trivially parallel.
- **A window-sized picture must carry an explicit `size`.** Never render at the size the window
  happens to have; widget metrics differ per platform.
- **The run must stay hermetic.** Two paths escape `--config` and both were found by stepping on
  them: `CDiskCache::cleanupRemovedMaps()` deletes the cache directory of every map the current
  configuration does not know, so `CMapDraw::setCacheRoot()` is called before anything reads the map
  list; and `CGisListWks` opened the real `userDataPath()/workspace.db` and emptied it on exit, so
  `CGisListWks::setDatabasePath()` points it at `_cache/<chapter>-workspace.db`. Both must be set
  before `CMainWindow` is constructed. The count of hardcoded `~/.QMapShack` paths is unknown; each
  is found by a crash.
- **The platform argument carries nothing.** It pinned a screen through the offscreen plugin's
  `configfile=`, which a Windows run does not accept in any spelling - the reason every task but
  `doc` failed to start there.
- **Output naming is Sphinx's.** `figure_language_filename` defaults to `'{root}.{language}{ext}'`,
  so a translated build wants `<id>.<lang>.<ext>` in one tree with the English name unsuffixed.
- **The pictures are repository content, not build output.** A checked-out branch has to render its
  documentation with no command run first, so every picture is committed and a PR carries the binary
  churn of the ones a change moved. Only `_cache/`, `_preview/` and the JSON reports are ignored.
- **Images ship inside the `.qch`**, so total image weight lands in what every user downloads. An
  argument for PNG discipline, against gratuitous full-window shots, and for keeping the fixture map
  small.

## 9. Measured

On this checkout, with a throwaway probe that drove the running application through its own UI, on
Linux and Windows, 13 of 13 checks passing on both. The probe is deleted; this section is what it
was for.

### How every number here was counted

Re-run these before trusting a number; they went stale once already. Last run 2026-09-07, from the
repository root.

```sh
grep -c "^SHOT_EXPOSE" src/qmapshack/shoot/CShotExposures.cpp             # 52 exposures
grep -rho "class [A-Za-z_]* *: *public *QDialog" src/qmapshack --include=*.h | wc -l   # 61 dialogs
grep -rho "\bSETTINGS\b" src/qmapshack --include=*.cpp | wc -l           # 168 SETTINGS uses
grep -rl "CMainWindow::self()" src/qmapshack --include=*.cpp --include=*.h | wc -l     # 71 files
grep -rho "QMessageBox::\(warning\|information\|critical\|question\)" \
     src/qmapshack --include=*.cpp | wc -l                                # 151 message box sites
grep -rn "public IScrOpt" src/qmapshack --include=*.h | wc -l             # 13 canvas overlays
grep -rn "QMenu menu" src/qmapshack --include=*.cpp | wc -l               # 9 stack-local menus,
                                                                          # one already split
```

`grep -c` counts *lines*, `grep -o | wc -l` counts *occurrences*; the two disagree wherever a line
carries the token twice, which is why each command above is written the way it is. The panel row of
§10.4 (40 of 57) has no command - it was a hand survey and is an estimate.

### The framework

- **`-platform offscreen` runs the whole application.** Map, DEM and POI paths load, canvases are
  created, draw threads start. No display, no window manager, no Xvfb.
- **Transient widgets render unshown.** A `QMenu` that was never popped up grabs complete with
  labels, separators, check marks and submenu arrows, and without a drop shadow - which is what
  documentation wants. `popup()` works too but logs offscreen warnings. Tooltips work only by
  fishing the private `QTipLabel` out of `topLevelWidgets()`. Static convenience dialogs
  (`QFileDialog::getOpenFileName`, `QMessageBox::critical`) block and cannot be shot; a constructed
  instance can.
- **`--config <file>` makes the settings hermetic**: `CSettings` switches to that file and the
  destructor's write-back lands in the scratch copy.
- **State installs through existing public API**: `CMapDraw::setupMapPath()`,
  `CDemDraw::setupDemPath()`, `CPoiDraw::setupPoiPath()`, `CGisWorkspace::loadGisProject()`,
  `CCanvas::loadConfig()` fed from a `.view` INI in the exact format *File ▸ Store View* writes.
- **A dialog inherits its form privately.** `private Ui::IX` → `protected Ui::IX` is the whole cost
  of reaching a widget from a subclass, one keyword, and only on the classes that need it.
- **A live `CMainWindow` is the fixture, not a workaround.** 71 files call `CMainWindow::self()`,
  and its constructor initialises `IUnit`, `CSearch`, `IPoiFile`, `IGisItem`, `CWptIconManager`,
  `CActivityTrk`, `CGisWorkspace`, `CGisDatabase`, `CToolBarConfig`, `CShortcutConfig` and
  `CGeoSearchConfig`.
- **Cost, on the demo:** framework ~3000 lines, `shots.py` ~400, the exposure catalog one line per
  class as predicted, changes to existing classes one keyword or one accessor each plus the menu
  split. One picture 1.3 s, fourteen 1.7-2.0 s cold, ~33 ms marginal - the cost is startup, not
  rendering.
- **`sizeHint`-driven pictures match across platforms exactly** (menu 234x372, dialog 550x473). A
  picture sized by whatever the window grew to does not: 1660x741 on Linux, 1482x741 on Windows.
- **`--dpr 2` changes nothing but text antialiasing.** Same image sizes, same layout, same icons.
- **Two runs of `shots.py chapter test` are byte-identical** to each other and to the committed
  images (2026-09-02), so on one machine a picture is a function of the configuration alone. Across
  machines it is still unmeasured.

### Qt behaviour that cost a day each

- **`QTest::mouseMove()` on a widget with no button down only calls `QCursor::setPos()`** and leaves
  the move to the window system (qtestmouse.h, Qt 6.10.2). The offscreen platform answers that at
  once and X11 does not, so a headless build saw the hover before the click and the writer's own
  session never did. A replayed move is sent as a `QMouseEvent` (`moveMouseTo()`).
- **`processEvents()` does not deliver `DeferredDelete`** - the event loop that posted it does, on
  its way out, and `settle()` never leaves one. Anything whose destructor is the state change has to
  ask: `sendPostedEvents(nullptr, QEvent::DeferredDelete)`. `CCanvas::resetMouse()` is such a case;
  `~CMouseRangeTrk` is what returns the track to `eModeNormal` and lets go of its mouse focus.
- **`restoreGeometry()` drops the whole record - size included - when the screen width it is
  replayed against differs by more than a quarter** (Qt 6.10.2, measured: `factor < 0.8 || factor >
  1.25` returns false and applies nothing). A width of 0 is what Qt 5.3 and earlier wrote and takes
  the branch that only rejects a window wider than one and a half screens, so `portableGeometry()`
  writes that instead and a base stored on one machine still sizes the window on the next.
  `QWidgetPrivate::checkRestoredGeometry()` then moves a window that would land off screen back onto
  it.
- **What `CMainWindow` restores in its constructor is a size the docks then grow past.**
  `MainWindow/geometry` has to be applied again once the layout is populated, or a base stored at
  1200x876 comes back 120 pixels taller and a writer cannot make a size stick.
- **On Windows the offscreen platform's font database is `QFreeTypeFontDatabase`**, which populates
  itself from `QLibraryInfo::LibrariesPath + "/fonts"`, a directory Qt no longer ships. A headless
  Windows run had no font at all and rendered every glyph as an empty box while the same picture in
  `--doc` came out right. `--font-family` only names a family; it cannot supply one, so
  `src/fonts/DejaVuSans{,-Bold}.ttf` are in `resources.qrc` and registered for a `--shoot`/`--doc`
  run.
- **The desktop's platform theme answers `QPlatformTheme::standardButtonText()` out of its own
  translations**, so a KDE session puts "Abbrechen" on a dialog whatever `--locale` says, and adds
  the mnemonics the generic theme leaves off. `-platform-theme generic` is the pin.
- **`QApplication::activePopupWidget()` needs window activation**, which an offscreen run has none
  of. A visible `QMenu` among `topLevelWidgets()` is the fact underneath it.
- **`QContextMenuEvent` must be sent to the viewport.** Sent to the scroll area,
  `customContextMenuRequested` does not fire even with `Qt::CustomContextMenu` set.
- **`QAbstractItemView` drops a double click whose index does not match one a press recorded.** A
  plain click has to precede it or nothing is emitted.
- **`QTest::mouseClick` fires `clicked()` synchronously** in a normal application, offscreen.
  `QAccessibleActionInterface::doAction("Press")` does not - `animateClick()` is behind it.
- **A step scheduled on the event loop runs *inside* `exec()`.** A modal dialog is addressable,
  renderable and closable from there.
- **A collapsed tree row's children are not in the accessibility tree**, so `QAccessible` alone
  cannot address the workspace. `itemPathOf()`/`resolveItemPath()` stay.
- **`saveGeometry()` records the screen the window was on and where it sat on the whole desktop**,
  so on a multi-screen desk a state process landed away from the panel. Where the window sits is the
  writer's screen (`--doc-screen`, `panel->screen()->name()`), not a stored position. Measured on
  three screens: asked for `DVI-I-1-1` / `DVI-I-2-2` / `HDMI-1`, the frame came out at x=2320 / 400
  / 4240, always 1119x769.
- **A decorated window belongs to the window manager until it is mapped.** The panel's geometry has
  to be applied one event loop after it is shown - this desktop answered the constructor's 460x760
  with 1200x996. Once only, so a writer's own resizing survives.

### QMapShack behaviour

- **`IPlot` sets no `objectName`.** The per-instance tag the track compares its mouse-focus owner
  against is `IPlot::ownerTag`; making it the objectName too made a plot's address depend on how
  many plots were built before it.
- **The view is the exact view**, `getPosFocus()` and `getZoomIndex()`. A visible *rectangle* is not
  enough: `zoomTo()` refits it to the canvas aspect and snaps it to a level, so the centre and the
  scale both drift and a stored crop frames the wrong thing.
- **The view is only the view.** Which maps, DEM, POI and grid are on belongs to the settings.
  `CMapDraw::loadMapList()` clears and rebuilds the map list and prunes the tile cache with the draw
  threads live, so a whole `CCanvas::saveConfig()` is not something a *running* canvas takes;
  `zoomTo()` is.
- **A click near an item is not a click on it.** `isCloseTo()` answers within 20 px and the range
  delegate takes one 200 px away (`MIN_DIST_FOCUS`), so a recorded point with nothing single under
  it is a step all the same and the point is all of it.
- **`CMouseAdapter` counts a move only past `minimalMouseMovingDistance`** (4 px) and drops a click
  held longer than `clickTimeout` (400 ms).
- **`settleStable()` is stable on an empty map.** It waits for two identical renders, and a blank
  canvas is identical to itself, so a build with a cold cache and no network produces blank maps
  quickly and quietly. What it catches is a map still arriving, not one that never does. A check
  that the map area is not one flat colour would close it.
- **The `.qm` catalogs are unreachable in a build tree.** `prepareTranslator()` resolves a filesystem
  path that is empty there, while all nine files are already embedded under `:/locale`.

## 10. What the demo does not do

1. **The fixture is half built.** The GIS data is synthesized in memory (`CShotFixture`): a
   `CQmsProject` with one track, waypoint, route and area, at a fixed epoch. Committed is only the
   map, `doc/shots/fixture/maps/osm.tms`, activated by `CShotFixture::build()` because a canvas with
   no active map covers itself with the welcome help. A DEM is a matter of putting files in
   `doc/shots/fixture/dem/`, not of touching code.

   Missing: a committed example project, so a chapter can add its own data. **This is what puts a
   developer in the writer's loop** - today every change to the example data is C++: a ticket, a PR
   and a rebuild before the writer can carry on.

   **The map is online, and that is a compromise**, not the offline raster #1209 asked for: a run
   needs a network the first time and byte-stability now also depends on the tile server. An offline
   extract of the fixture area replaces it without touching a line of code.
2. **Cross-machine determinism is unproven.** One machine is byte-stable. The two halves that failed
   in the field - the font and the device pixel ratio - are fixed. Nothing has compared two
   machines, which is what `diff`, `update` and any CI wait on.
3. **The language loop.** English only, but pinned rather than inherited. Left to build: a
   `:/locale` fallback in `prepareTranslator()`, one process per language, and the
   `<id>.<lang>.<ext>` naming.
4. **Five mechanisms, each covering a whole bucket:**

   | Subject | Count | State |
   |---|---|---|
   | Dialogs | 61 | 52 exposed; 4 `CDetails*` reached by `dclick`/`trigger` instead, 5 cannot be built - see §3 |
   | `QWidget` panels with a designed form | 40 of 57 | mostly works |
   | Menus named in a `.ui`, or a member built in code | 6 + 24 | works, live address |
   | Menus built as a stack local ending in `exec()` | 8 | **no** - needs the `buildMenuXxx(QMenu&)` split, done once for `CGisListWks::buildMenuItemTrk` |
   | `QMessageBox::warning/information/critical/question` | 151 sites, 62 files | **no** - no object to expose or address |
   | Canvas overlays (`IScrOpt` subclasses) | 13 | reached by recording a click: the 4 item overlays directly, the rest through the button that turns their mouse mode on |
   | Progress dialogs mid-operation (`PROGRESS_SETUP`) | 18 | **no** - exposable, the state is not |
   | WebEngine content (`CWebPage`, `QWebEngineView`) | 10 + 4 | **no** - `grab()` does not render it |

   Recounted 2026-09-07 with the commands in §9; the panel row is still the survey of 2026-08-25
   and has no command, so treat it as an estimate. `doc/pages/test.md` is the worked example: eight
   pictures, five in `(base)` and three in recorded scenarios, one of them a region.
5. **Animations.** Stills only. Capture is easy - start/stop bracketing frames through the existing
   `CShotWriter` numbering, stitched by an `ffmpeg` step that must resolve through
   `IAppSetup::getPlatformInstance()->findExecutable()` rather than becoming a dependency. Replay is
   the unproven half: a canvas interaction drives `IMouseEditLine` and its `ILineOp` states, the
   most re-entrancy-sensitive code in the project. Keep `CShotContext` frame-sequence-shaped so
   waiting costs nothing.
6. **The writer's half is untested end to end.** There is no Sphinx project in this repo. The pages
   under `doc/pages/` are MyST by shape only; `figure_language_filename` is a claim from the
   documentation, not a result.
7. **The writer is shown addresses, not names.** `chooseLivePart()` labels every step
   `windowTitle-or-className (address)` and `addressOf()` hands back Qt's own object names verbatim,
   so the choice reads `qt_scrollarea_vcontainer` against `CTableTrk`. The address is the stored
   identity and stays; the *label* wants what the writer sees - a docker's caption, a tab's text, a
   group box's title - and a `qt_`-prefixed internal should not be offered as a step at all.
8. **`-platform offscreen` does not deploy on Windows.** `msvc_64/copyfiles.bat:75` and
   `CopyFilesGis.bat:57` copy `qwindows.dll` alone. `qoffscreen.dll` is a platform plugin, not a
   link-time dependency, so nothing pulls it in and `--shoot` cannot start against a packaged tree.
9. **The colour scheme is pinned, light.** `--color-scheme light|dark` (`CUiTheme::pinColorScheme`)
   replaces the application palette and sets the style hint. Two levers because neither is enough
   alone: `QPlatformTheme::requestColorScheme()` has an empty default implementation, so the style
   hint does nothing on X11, and `QStyle::standardPalette()` asks the platform theme for the very
   scheme being pinned away. `dark` is one constant in `shots.py` away.

**Open, and to decide before the naming is fixed:** whether the documentation carries a dark set at
all and how the scheme enters the file name; whether a picture of a message box is in scope; whether
a picture can be deliberately scaled down on the way out - asked for by a writer whose pictures were
too large, and a different knob from the device pixel ratio, which only makes two machines agree.

## 11. The feature branch

The demo is a throwaway. It answered the design questions; it is not the code that ships. The branch
builds the same thing again as sub-tickets, each one reviewable and mergeable on its own, in this
order - every step leaves the tree working and every step has something that can be run to see it.

Cross-cutting requirements for every sub-ticket: the code compiles out under `QMS_DOC_MODE=OFF`; no
new dependency; nothing reaches outside `--config`; every failure is counted and printed with the
shot id and the offending name, never silently rendered.

| # | Sub-ticket | Scope | Done when |
|---|---|---|---|
| 1 | **Hermetic run** | `--shoot`/`--doc` switch parsing under `QMS_DOC_MODE`, `--config`, the bundled font, `--locale` + `QLocale::setDefault()`, `--color-scheme`, `-platform-theme`, cache root and workspace database redirection | the application starts headless against a scratch configuration and touches nothing under `~/.QMapShack` |
| 2 | **Render path** | `CShotWriter` (settle, settleStable, resize-to-hint, dpr 1, PNG), `CShotContext` | one widget renders to a file, twice, byte-identically |
| 3 | **Chapter file and `shootOne()`** | the JSON schema of §2, `addressOf()`/`resolve()` symmetric, `set`, `size`, `rect`, the failure counting | a chapter of plain widget shots renders; a renamed widget fails loudly |
| 4 | **Exposure catalog** | `CShotRegistry`, `SHOT_EXPOSE`, keyed by `typeid`; the `private`→`protected` form changes; the `scratch<T>()` pattern | every exposed dialog renders from `shots.py list` |
| 5 | **Fixture** | `CShotFixture` plus a **committed** example project and an **offline** map extract; DEM and POI directories | a run needs no network and no writer's data |
| 6 | **shots.py** | `compose`, `chapter`, `build`, `list`, `inspect`, `explore`, `reap`; one process per scenario; JSON reports | `shots.py build` regenerates every committed image |
| 7 | **Recorder: vocabulary** | the event filter, `pressIsStep()`, the step table of §4, `driveProperty()` | a recording of a menu, a control and a row is stored and reads as meaning |
| 8 | **Recorder: adapters** | canvas, `IPlot`, `CIconGrid`, workspace row buttons; the menu-owner `objectName` audit | each adapter verified end to end against a throwaway chapter |
| 9 | **Replay** | the queue, `clear()` before and after, the click path, hit verification, the `tab`-last rule | a chapter with a scenario reproduces byte-identically, three times in one process |
| 10 | **Documentation mode** | launcher and state process, the `QLocalSocket` channel, the panel, F9, the shot dialogs | a writer records a scenario and tags a picture without touching a file |
| 11 | **Writer-facing labels** | names, not addresses, in `chooseLivePart()`; `qt_`-prefixed internals not offered | the step list reads in the writer's words |
| 12 | **Menu split** | `buildMenuXxx(QMenu&)` for the eight `exec()` locals | all 38 menus are addressable |
| 13 | **Cross-machine determinism** | measure Linux/Windows/macOS on one chapter; then `diff` and `update` | a report says which pictures differ and by what |
| 14 | **CI** | one job running `shots.py build` and failing on a difference | depends on 13 |
| 15 | **Language loop** | the `:/locale` fallback, one process per language, `<id>.<lang>.<ext>` | one chapter renders in two languages |
| 16 | **Windows packaging** | `qoffscreen.dll` in `copyfiles.bat` and `CopyFilesGis.bat` | `--shoot` starts against a packaged tree |

**Review focus, per area.** What is worth a reviewer's attention is not spread evenly:

- **1, 5** - anything that writes outside the scratch tree. Both escapes so far were found by a
  crash, not by reading.
- **3, 7, 8** - `addressOf()` and `resolve()` must stay symmetric, and every new address kind needs
  both halves in the same commit.
- **9** - ordering and idempotence. The bugs here do not look like bugs: they look like a picture of
  a slightly different state.
- **10** - lifetime. Two processes, a socket, a window that may be gone; every path that can leave
  an orphan or a veto on quit.

**What a sub-ticket must carry:** the chapter it was verified against, the images it changed, and a
line in the repo's `CLAUDE.md` for any behaviour a reader could not have guessed. The demo's own
lessons are §9 - a fact that cost a day belongs there and not in a commit message nobody reads
again.

## 12. Non-goals

- The documentation toolchain, page structure and hosting (#1209), beyond the constraints Sphinx
  imposes here.
- Pictures of online map *services* as documentation subjects. The fixture map is itself online
  today, which is a compromise on this rule, not an exception to it.
- Retranslating a live `CMainWindow`.
- Making ffmpeg a build dependency.
- Icons. Inline icon glyphs in the documentation are `.svgt` renders, not screenshots - a small
  exporter, a separate ticket.
