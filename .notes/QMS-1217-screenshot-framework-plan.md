# QMS-1217 — documentation images

Issue: Maproom/qmapshack#1217. Related: discussion #1209 (documentation rework).

Goal: every image in the user documentation is build output, regenerable by one command on Linux,
Windows and macOS, per language.

State: implemented on branch `QMS-1217_demo` as a throwaway demo. It renders a real chapter and is
usable by a writer, and a scenario is recorded rather than registered (§7); the fixture,
determinism and the language loop are not built (§8).

---

## 1. The unit is a chapter

A chapter is one page, one JSON shot file, one configuration and one directory of images:

```
doc/pages/<chapter>.md              the page, MyST, references ../images/<chapter>/<name>.png
doc/shots/<chapter>.json            the pictures and the scenarios, written by the application
doc/shots/<chapter>/<scenario>.ini  one scenario's settings: what differs from the base
doc/shots/fixture/shots.ini         base configuration, shared by every chapter
doc/images/<chapter>/               the pictures
doc/shots/_cache/                   per-run tile cache and workspace database, git-ignored
```

The chapter is the unit because the application a user sees is not the application at first start:
dockers get rearranged, a writer tunes the layout to make a picture focused, and a setup chapter's
configuration changes *during* the chapter. Inside a chapter the unit is the **scenario** (§7): a
picture is taken in one, and a scenario owns the arrangement, the map and the settings it is taken
with. Its pictures are a sequence of states, not independent stills.

**One base, a scenario may only add.** Maps and DEM are shared and nothing changes them; example
data is one base project a chapter may add a file *on top of*; configuration is the base `.ini` plus
the scenario's patch. Data that needs to *change* the base means the base is wrong — fix the base.
Storing a scenario's setup is an explicit button, never automatic, or the setups drift with every
session.

## 2. A shot is data

`doc/shots/<chapter>.json`, written by documentation mode through `CShotChapter::store()`:

```json
{
  "shots": [
    { "id": "test/menu-setup-workspace", "widget": "menuProject" },
    { "id": "test/workspace-setup-database", "exposure": "SetupWorkspace",
      "set": { "tabWidget.currentIndex": 2 }, "size": [620, 471] },
    { "id": "test/track-scropt", "scenario": "track-screen-option", "widget": "",
      "size": [1224, 751], "rect": [423, 214, 634, 232] }
  ]
}
```

| Key | Means |
|---|---|
| `id` | file stem, `<chapter>/<name>`; also what the page references |
| `widget` | address in the running application; empty is the main window itself |
| `exposure` | build a fresh instance from the exposure catalog instead |
| `scenario` | the recording this picture is taken in; absent means `(base)`, the plain start (§7) |
| `set` | properties to drive, `child.property` or a bare property of the shot's own widget |
| `select`, `expand` | workspace item name paths |
| `docks`, `canvas` | the window arrangement this picture needs |
| `size` | explicit render size |
| `note` | JSON has no comments |

JSON, not YAML: `QJsonDocument` is in Qt and `json` is in the Python standard library, and
`QJsonObject` sorts its keys, so a machine-written file diffs stably.

**Addressing.** `dockWorkspace` for anything a `.ui` file named. A widget built in code has no
`objectName` and is addressed relative to its nearest named ancestor by class and position,
`IMapList/QMenu#0`. `CShotChapter::addressOf()` and `resolve()` are the two halves and must stay
symmetric.

**State is data, never replayed input.** A selection is a name path, `Shoot Demo/trk:Demo Track` —
not an `IGisItem::key_t`, whose `genKey()` is an MD5 of the serialized item (`IGisItem.cpp:183`) and
dies on any edit to the fixture, and not a row index, which sorting changes. Expansion falls out of
the path; every ancestor on the way is expanded. A path that does not resolve fails the run with the
file and the shot id printed — never a silently wrong picture. Event injection is needed only for
hover, drag and mid-operation state, which are C++ scenarios.

## 3. Two drivers, one render path

Documentation mode (`--doc`) and the headless run (`--shoot`) load the same configuration and the
same fixture and go through the same `CShotChapter::shootOne()` and `CShotWriter`, so the picture a
writer accepts is what a later replay reproduces.

```
src/qmapshack/shoot/
  CShotRegistry        the exposed widget classes; SHOT_EXPOSE
  CShotContext         the live application and the fixture by role; shot() and frame()
  CShotWriter          settle, resize, grab, PNG
  CShotChapter         the JSON chapter: addressing, item paths, store, shootOne, run
  CShotRecorder        records what the writer performs, and replays it (§7)
  CShotFixture         the example project, synthesized in memory
  CShotRunner          --shoot tasks; writes a JSON report beside the images
  CShotDocMode         the writer's key handling, dialogs and chapter bookkeeping
  CShotDocPanel        the writer's panel
```

Everything renders through `QWidget::grab()` under `-platform offscreen`, at
`resize(size().expandedTo(sizeHint()))` — grow to the hint, never below the size the `.ui` file
already chose. `CCanvas::print()` and `IPlot::save()` are unused: the map arrives inside a
main-window grab and a plot is a grabbed `CPlotProfile`.

**Command line.** `--shoot <dir>` with `--shoot-task build|list|inspect|explore|chapter`,
`--shoot-target <id-or-file>` and `--only <glob>`; `--doc <repo>` with `--doc-chapter <name>`.
Either mode skips the splash and `CSingleInstanceProxy`, builds a real `CMainWindow`, and starts
from a queued invocation because the window defers part of its initialization by a timer and the
canvases start their draw threads asynchronously. `CCanvas::waitForDrawContexts()` is the readiness
signal, not a delay.

### Still C++, and only this — the exposure catalog

- The **exposure catalog** (`CShotExposures.cpp`, 58 entries), because a constructor's arguments
  cannot be data. Only 9 of the 61 dialogs take nothing but a parent; the rest want a fixture item,
  a singleton alive inside `CMainWindow`, or a result the dialog writes back through a reference —
  the last kind gets a `scratch<T>()` static that outlives the shot, and a dock or configuration
  object is addressed by its class through `findChild`. An entry is one line and is paid once per
  class, never per image. Exposures are keyed by `typeid`, not the meta object: three exposed
  dialogs have no `Q_OBJECT` and would all report their base class name.

  56 of the 61 dialogs are exposed, plus the two detail panels that are not dialogs. The five left
  out: `CExportDatabase` and `CSearchDatabase` need a live `QSqlDatabase` with content,
  `CRangeToolSetup` exists only while the range mouse mode is active, and `CTemplateWidget` and
  `CShotDocPanel` are not user-facing.
There are no scenario recipes. `IShotRecipe`, `RecipesChapter.cpp` and the registry's recipe table
are gone: a recipe was written per picture, so the catalog grew with the documentation and had to be
read to be used (§7). Of the eight that existed, four were replaced by a recording, two — the two
menus — were already redundant because a pulled-down menu is a live widget `activeTarget()` returns,
and `TrackProfile`, a bare `CPlotProfile` with no window around it, is an exposure if it is wanted.

A rename is caught by `shots.py chapter` failing loudly, not by the compiler — `"dockWorkspace"` and
`"Shoot Demo/trk:Demo Track"` are strings either way, so keeping shots in C++ would protect nothing.

## 4. The writer's loop

`shots.py doc <chapter>` opens QMapShack on the chapter's configuration. `doc/WRITING.md` is the
writer's guide; the mechanics are:

**A chapter is made in two steps, and the panel is two sections.** Scenarios are recorded, renamed
and deleted at the top. The pictures the page asks for are listed below, each row carrying the
scenario it is taken in, in a combo box of its own. There is one selection anywhere, so nothing has
to be resolved between "which scenario" and "which picture": a picture is taken in the scenario its
own row names, and a name that has no row yet is taken in the selected one.

**A picture always has a state**, and the first row of every *Taken in* box is `(base)`: the
application as the configuration starts it, with nothing replayed on top. It is the default for a
picture the page has just asked for, so nothing has to be answered before it can be taken.

**Ctrl+Shift+F9 is the only key** — take a picture of what the mouse points at. Everything else is
a button, because the mouse is busy pointing:

| Section | Button | Does |
|---|---|---|
| Scenarios | Record... | perform the state a picture needs; stop and it becomes the chapter's own (§7) |
| | Rename... | another name for the selected scenario; no picture is invalidated |
| | Delete | throw it away, and with it every picture taken in it |
| Pictures | Take a region... | drag a rectangle over the window in the picture's own scenario |
| | Update | into the selected scenario, or into the base when `(base)` is selected |
| | Take all again | take every picture of the chapter again, here, and report which came out different |
| | Remove unused | delete the pictures no page references |

**Losing a scenario loses the pictures taken in it**, and both ways of doing it ask first and name
what dies. A widget address and a rectangle frame something else in another state, so nothing of a
shot's definition survives: the entry is reduced to its bare name and the image is deleted. The entry
stays because the page still asks for the picture — the `.md` is what says a shot exists, the chapter
file only says how it is taken.

A region is stored as the scenario plus the rectangle plus the window size it was dragged at, never
as pixels off the writer's screen, so it regenerates like any other shot.

F9 starts at the widget under the mouse and offers every step up to the whole window that a shot
can find again, then asks which picture this is out of **the names the page asks for and has not
got, and only those** — a picture is wanted by the text, never invented at the window, and an id
typed here would become a row no page references — renders a **fresh** instance
through the headless path, and shows it for the writer to keep or throw away. The tagged widget only
identifies the class. A class with no exposure prints the one line a developer has to add.

The panel lists what the page asks for against what exists: *taken*, *missing*, *no image*,
*not used*. A retake reports which pictures came out different, which is the only signal
that a replay no longer reproduces what the writer accepted.

## 5. shots.py

`doc/tools/shots.py`. Python because #1209 already requires every writer to have it and the three
platforms rule out a shell script.

| Command | Does |
|---|---|
| `doc [CHAPTER]` | open the application so a writer can take pictures |
| `chapter [NAME]` | shoot one chapter file, no window, one process per scenario |
| `build [--only GLOB]` | take every chapter's pictures again, one process each |
| `list` | the exposed widget classes, which is all a build still carries |
| `inspect <id>` | a widget's children, types and settable properties |
| `explore <id>` | which controls actually change the widget |
| `reap [--delete]` | pictures no page references any more |

It composes the run's configuration into a scratch copy — the scenario's own file, or the base when
it has none — so a writer's session cannot drift what a build renders, and injects what the tool owns
rather than the writer: the fixture's `Canvas/{mapPath,demPaths,poiPaths}` and
`Database/saveOnExit=false`, without which a run saves its workspace and the next one loads the demo
project twice. It pins `-style Fusion`, `--font-family DejaVu Sans`, `--font-size 10`,
`--color-scheme light`, `TZ=UTC` and an unset `QT_SCALE_FACTOR`, and prints which configuration
file it read. `list`, `inspect` and
`explore` need the running application as much as `build` does; each writes a JSON report beside the
images which `shots.py` renders, so nothing parses stdout.

`diff` and `update` are deliberately absent: they mean nothing until the output is byte-stable.

## 6. Rules that hold

- **An exposure never calls `exec()`.** Construct, polish, size, render. That one rule is what makes
  menus, message boxes and modal dialogs tractable at all; `exec()` in a runner driven from the
  event loop is a nested loop waiting for input that never comes.
- **Drive inputs, never outputs.** Set `comboColorSource->setCurrentIndex(2)` and let the
  application's own signal chain produce the state. Setting a visibility directly can produce a
  state no user can reach and bakes today's logic into the shot; driving the input means the shot
  encodes the scenario, so when the logic changes the shot does not become wrong, it renders
  differently.
- **Never reload the configuration in a running application.** 152 `SETTINGS` reads — 40 in
  constructors, 27 in destructors, 26 in slots, 9 in `loadSettings()`; the dock layout is
  `restoreGeometry`/`restoreState` in the `CMainWindow` constructor (`CMainWindow.cpp:160-166`).
  **One process per distinct configuration state** instead: startup is 1.3 s, so ~18 s for a
  ten-picture page and ~20 min for 690, trivially parallel. `shots.py chapter` runs one process when
  every shot shares the chapter's configuration, one per shot when they do not.
- **The platform argument carries nothing.** It pinned a screen through the offscreen plugin's
  `configfile=`, which a Windows run does not accept in any spelling — reported from the field, and
  the reason every task but `doc` failed to start there. Measured 2026-09-02: the seven `test`
  images come out byte-identical with plain `offscreen`, because `shootOne()` resizes every window
  it photographs and `resize()` is not clamped to the screen the way `restoreGeometry()` is.
- **The run must stay hermetic.** Two paths escape `--config` and both were found by stepping on
  them: `CDiskCache::cleanupRemovedMaps()` deletes the cache directory of every map the current
  configuration does not know, so `CMapDraw::setCacheRoot()` is called before anything reads the map
  list; and `CGisListWks` opened the real `userDataPath()/workspace.db` and emptied it on exit, so
  `CGisListWks::setDatabasePath()` points it at `_cache/<chapter>-workspace.db` and the base
  configuration carries `[Database] saveOnExit=false`. Both must be set before `CMainWindow` is
  constructed. The count of hardcoded `~/.QMapShack` paths is unknown; each is found by a crash.
- **Output naming is Sphinx's.** `figure_language_filename` defaults to `'{root}.{language}{ext}'`,
  so a translated build wants `<id>.<lang>.<ext>` in one tree with the English name unsuffixed. The
  current layout is `doc/images/<chapter>/<name>.png` with no language suffix — §8.
- **The pictures are repository content, not build output.** A checked-out branch has to render its
  documentation with no command run first, so every picture is committed and a PR carries the binary
  churn of the ones a change moved. `shots.py` is what a writer or developer runs when the
  documentation needs updating, never a build step. Only what a run leaves beside the pictures is
  ignored: `_cache/`, `_preview/` and the JSON reports.
- **Images ship inside the `.qch`**, so total image weight lands in what every user downloads. An
  argument for PNG discipline, against gratuitous full-window shots, and for keeping the fixture map
  small.

## 7. Scenarios are recorded, not registered

**The registry did not close.** A recipe was written per picture, so its catalog grew with the
documentation — towards the ~690 pictures §6 counts — and a writer had to *read* it to know whether
an entry fitted. Every gap was a ticket, a PR and a rebuild before the writer could carry on. §3 says
what the eight that existed were and why nothing replaces them.

**A writer performs a scenario instead of picking one.** *Record…* puts the application into the
scenario the writer has selected and starts from there, so only what is still missing is performed;
*Stop recording* asks for a name. The starting scenario's steps are **copied** into the new one,
never referenced, so deleting what it started from cannot change it afterwards. A scenario is named
in the *Taken in* box of every picture taken in it (§4); there is no global list to browse.

**`(base)` is a row, not a scenario.** It heads the scenario list and every *Taken in* box, is stored
nowhere, and a picture taken in it simply has no `scenario` key. A per-chapter copy of the start
state can go stale; the start state cannot. *Update* on it writes the base configuration.
`--shoot-scenario -` is how a build asks for that group, and `-` is refused as a scenario name.

### What a recording holds

`CShotRecorder` watches through one event filter and, on each mouse release, diffs the state against
the last one. When the writer stops, the two things that are not a difference are taken whole and put
in front:

| | The action | What it holds |
|---|---|---|
| taken at Stop | `layout` | `saveGeometry()`, `saveState()` and the central tab index |
| taken at Stop | `view` | the centre in degrees and the zoom level |
| selected a workspace item | `select` | the item's name path |
| opened a project | `expand` | the same |
| clicked an item on the map | `click` | the geographic point plus what was under it |
| operated a control | `set` | the address `addressOf()` computes and the value driven in |

That is the replay order too: the arrangement decides how big the canvas is, the view decides what it
looks at, and only then does a geographic point mean the pixel the writer clicked.

**What is recorded is meaning, never input.** A name path, a geographic point, a widget address, a
driven value and an area in degrees — no pixel, zoom level or device pixel ratio in any of them.
`layout` is the one exception, an opaque `saveState()` blob, and it earns it: which dockers are
visible is expressible in the open, but *where they sit and how wide they are* is not, and a docker's
width is what decides a picture's width.

**Nothing watches for a button press**, and that is the design. A press is input; what it produced is
state, and the diff finds the state. A control the writer operates *is* an input and is recorded as
one (§6). A button with no state behind it is not recordable, and the recorder says so rather than
storing a click that means something else the next time the dialog is laid out differently.

**The view is the exact view**, `getPosFocus()` and `getZoomIndex()`, replayed with `zoom(index)` and
a `moveMap()` that carries the recorded point to the middle of the canvas. A visible *rectangle* is
not enough: `zoomTo()` refits it to the canvas aspect and snaps it to a level, so the centre and the
scale both drift, everything on the map lands elsewhere, and a stored crop frames the wrong thing —
which is exactly how it failed. A recording made before the level was stored still carries `area` and
is replayed the old, approximate way.

**The view is only the view.** Which maps, DEM, POI and grid are on belongs to the settings. Handing a
whole `CCanvas::saveConfig()` to a *running* canvas is not something `CMainWindow` does either — both
of its `loadConfig()` callers apply it to a canvas they have just created — because
`CMapDraw::loadMapList()` clears and rebuilds the map list and prunes the tile cache with the draw
threads live. `zoomTo()` is what a live canvas takes.

**The diff needs a fixed thing to differ from.** A recording resets first (`CShotRecorder::reset()` —
screen options closed, selection cleared, projects collapsed), then replays its starting scenario, and
`start(base)` seeds the action list with that scenario's own steps so the difference is appended to
them. `layout` and `view` are dropped from the seed because both are taken whole again at Stop.

### How it is stored

```
doc/shots/<chapter>.json          the shots, and the scenarios' action lists
doc/shots/<chapter>/<name>.ini    one scenario's whole configuration
doc/shots/fixture/shots.ini       the base a chapter opens on
```

```json
"scenarios": {
  "track-clicked": [
    { "do": "layout", "geometry": "…", "state": "…", "tab": 0 },
    { "do": "view", "area": [11.19, 47.29, 11.24, 47.33] },
    { "do": "click", "item": "Shoot Demo/trk:Demo Track", "lat": 47.31, "lon": 11.21 }
  ]
}
```

A shot names one through the **`scenario` key it already had**, and `shootOne()` resolves it against
the chapter's own recordings — there is nowhere else to look. A scenario only puts the application
into the state; the shot still says what is photographed, through its `widget`, its `exposure`, or the
whole window by default. So one recording carries as many pictures as the chapter wants, and a shot
naming a scenario the chapter has not got fails with the name printed.

**The settings are a file, not an action.** §6's rule stands — 152 `SETTINGS` reads, 40 in
constructors — so a running application cannot be made to re-read them, and `shots.py` runs **one
process per scenario**, `--shoot-scenario <name>` telling it which shots are its own.

**A scenario's file is a whole configuration, and the base is a starting point, not a layer.**
`compose_config()` uses the scenario's file alone and falls back to the base only when there is none.
A file holding a difference would move whenever the base moved — the coupling the recording already
avoids by copying rather than referencing — and a picture already taken would silently stop
reproducing. So storing a base changes what a chapter opens on and what the next recording copies,
and nothing else. Machine-specific keys are in neither: `shots.py` injects `Canvas/mapPath`,
`Canvas/demPaths`, `Canvas/poiPaths` and `Database/saveOnExit` when it composes a run.

**A scenario owns the state**, so a shot writes no `select` or `expand` of its own: a second
description of the same thing is applied after the replay and quietly undoes part of it.
`shootOne()` still honours both keys for shots written before this. Its `size` is applied **before**
the replay, because resizing the window resizes the canvas, and the canvas size decides which piece of
the world the view covers and where the item sits that a click anchored its options to.

`CShotChapter` owns the file operations: `storeScenario`, `renameScenario` (invalidates no picture),
`deleteScenario` and `rebindShot` (both reduce every affected entry to its bare `id`), `shotsUsing`
and `scenarioOf`. The recordings reach a shot through `CShotContext::setScenarios()`, and
`scenario_cleanup_t` in `CShotChapter.cpp` takes the state back down after the picture unless the
context is holding. Nothing prunes a scenario on its own: one with no picture is one the writer has
not used yet, and *Remove unused* touches only images.

**Qt has no recorder.** It has event synthesis (`QTest`, not linked here) and `QJSEngine`, already in
the binary — `Qt6::Qml` is linked (`src/qmapshack/CMakeLists.txt:1083`), used by `CMapTMS.cpp` and
`CRouterBRouterSetup.cpp` — so a script engine costs no new dependency should replay ever want one.
The recorder is a plain event filter, and replay re-issues the actions.

**This is what bounds the developer's part.** A recipe was asked for per *picture*, an open-ended
list. A recorder is asked for per *interaction kind*, and that list is §8.4's table: eight rows. The
click on a map item was the first of them and is now data, through
`CMouseNormal::showScreenOption()` — one entry point serving every future picture.

### Where a recording stops

- **A hover, a tooltip, a half-finished drag.** Not state: the hover an overlay needs is computed
  while the canvas paints, so there is nothing for a diff to find.
- **A menu.** Pulling one down is photographed by pointing at it; the eight built as locals ending in
  `exec()` still need the `buildMenuXxx(QMenu&)` split of §8.4.
- **A dialog opened during a recording.** A dialog is a window of its own and is photographed through
  the exposure catalog, not through a state the main window holds.
- **Seeing another scenario's settings without restarting.** They are read when the application
  starts, so a picture taken in documentation mode carries the settings on screen. `settingsDrift()`
  counts the difference and the panel reports it after every shot; fixing it means restarting per
  scenario in the writer's session too.
- **A control class the diff does not know.** The list is closed — combo, tab, spin, slider, line
  edit, checkable button, checkable group box. An input outside it shows up as a picture that does
  not come out, never as a recording that silently lost a step.

**Open.** Whether a recording should be re-recordable in place, since today a changed scenario is a
new recording under the same name.

**`settleStable()` is stable on an empty map.** It waits for two identical renders, and a blank canvas
is identical to itself, so a build with a cold cache and no network produces blank maps quickly and
quietly. What it catches is a map still arriving, not one that never does. A check that the map area
is not one flat colour would close it.

## 8. What is not built

1. **The fixture is half built.** The GIS data is synthesized in memory (`CShotFixture`): a
   `CQmsProject` with one track, waypoint, route and area, at a fixed epoch. Committed is only the
   map: `doc/shots/fixture/maps/osm.tms`, activated by `CShotFixture::build()` because a canvas with
   no active map covers itself with the welcome help. `shots.py` injects `Canvas/mapPath`,
   `Canvas/demPaths` and `Canvas/poiPaths` from `doc/shots/fixture/` at compose time — absolute, so
   they cannot be committed — and the map, DEM and POI lists rebuild themselves from them. A DEM is
   therefore a matter of putting files in `doc/shots/fixture/dem/`, not of touching code.

   Still missing: a committed example project, so a chapter can add its own data. The *empty*
   application state is answered: it is `(base)` (§7), which stores nothing and so cannot go stale.

   **This is what puts a developer in the writer's loop.** Writing a page is iterative in its data as
   much as its text, and today every change to the example data is C++: a ticket, a PR and a rebuild
   before the writer can carry on. The committed project is not a convenience — it is what makes the
   loop the writer's own.

   **The map is online, and that is a compromise**, not the offline raster #1209 asked for: a run
   needs a network the first time, the tiles land in the run's own cache, and byte-stability across
   machines now also depends on the tile server. It buys a real map today at the cost of §10's
   "no online maps" rule; an offline extract of the fixture area replaces it without touching a
   line of code. `CShotWriter::settleStable()` is what makes it usable — it renders until two consecutive
   pictures match, because tiles arrive long after the draw threads are idle.
2. **Determinism.** The two halves that failed in the field are fixed; byte-stability across
   machines is still unproven.

   **The font is the application's own.** `src/fonts/DejaVuSans{,-Bold}.ttf` are in
   `resources.qrc` and `IAppSetup::processArguments()` registers them for a `--shoot` or `--doc`
   run. Not a nicety: on Windows the offscreen platform's font database is `QFreeTypeFontDatabase`
   (`qoffscreenintegration.cpp:71`), which populates itself from `QLibraryInfo::LibrariesPath +
   "/fonts"` — a directory Qt no longer ships — so a headless Windows run had **no font at all**
   and rendered every glyph as an empty box, while the same picture taken in `--doc` came out
   right. `--font-family` only names a family; it cannot supply one.

   **The device pixel ratio is out of the picture.** `CShotWriter` renders into a `QImage` carrying
   a ratio of 1 rather than through `QWidget::grab()`, which renders at the widget's own. Qt 6
   offers no way to put a real screen back to 1 and the writer's session runs on one, so the ratio
   is taken out of the paint device instead of the screen and doc mode and a headless build produce
   pictures of the same size on any machine. The modal warning it replaces is gone. Exact for a
   dialog, a menu or a docker; a canvas is the exception, because its layers are drawn into buffers
   sized by the widget's own ratio, so a HiDPI writer's map lands in the picture downscaled rather
   than rendered at 1 — same size and layout, resampled tiles. A headless run is at 1 throughout.

   **Measured 2026-09-02:** two runs of `shots.py chapter test` are byte-identical to each other and
   to all seven committed images, so on one machine a picture is a function of the configuration
   alone. Across machines it is still unmeasured, which is what `diff`, `update` and any CI wait on.
3. **The language loop.** English only, but pinned rather than inherited: `shots.py` passes
   `--locale en` and sets `LC_ALL`/`LANG`, and `--locale` now calls `QLocale::setDefault()` as well
   as choosing the catalogs. Without both, `prepareTranslator()` falls back to
   `QLocale::system().name()` and English text comes out with German dialog buttons and dates. A
   writer on a German system got German pictures before those pins existed — they are what fixes it,
   and the language is still not something the writer chooses.

   Left to build: `prepareTranslator()` reads a filesystem path that is empty in a build tree,
   while all nine `.qm` files are already embedded under `:/locale` — one fallback lookup makes both
   this and any uninstalled run translatable. Then one process per language (retranslating a live
   `CMainWindow` is not worth attempting) and the `<id>.<lang>.<ext>` naming of §6.
4. **Five mechanisms, each covering a whole bucket:**

   | Subject | Count | State |
   |---|---|---|
   | Dialogs | 61 | 56 exposed; 5 left out, see §3 |
   | `QWidget` panels with a designed form | 40 of 57 | mostly works |
   | Menus named in a `.ui`, or a member built in code | 6 + 24 | works, live address |
   | Menus built as a stack local ending in `exec()` | 8 | **no** — needs the `buildMenuXxx(QMenu&)` split, done once for `CGisListWks::buildMenuItemTrk` |
   | `QMessageBox::warning/information/critical/question` | 147 sites, 61 files | **no** — no object to expose or address |
   | Canvas overlays (`IScrOpt` subclasses) | 13 | the 4 item overlays are reached by recording a click (§7); the rest need their mouse mode active |
   | Progress dialogs mid-operation (`PROGRESS_SETUP`) | 18 | **no** — exposable, the state is not |
   | WebEngine content (`CWebPage`, `QWebEngineView`) | 10 + 4 | **no** — `grab()` does not render it |

   Counted 2026-08-27, except the dialog and panel rows (2026-08-25). `doc/pages/test.md` is the
   worked example: five pictures, four in `(base)` and one - the options over a clicked track - a
   region taken in a recorded scenario. An overlay is reached without driving the mouse, but it has to go through the delegate: the
   bubble around the options is painted by `CMouseNormal::draw()` in `eStateShowItemOptions`, so an
   overlay constructed directly renders as a bare widget with no bubble. `CMouseNormal::
   showScreenOption(pt, item)` / `clearScreenOption()` expose the half of the click path that a
   shot cannot reach — the hover state a real click needs is computed while the canvas paints.
   The canvas must be zoomed and drawn first (`CScrOptTrk` anchors itself with `getPointCloseBy()`),
   and the pair leaves two marks: a click focus on the item, and `~IScrOpt()` calling
   `CGisWorkspace::slotWksItemSelectionReset()`.

5. **Animations.** The keys take stills only. Capture is easy — start/stop bracketing frames through
   the existing `CShotWriter` numbering, stitched by an `ffmpeg` step that must resolve through
   `IAppSetup::getPlatformInstance()->findExecutable()` rather than becoming a dependency. Replay is
   the unproven half: a widget-level animation is a property sequence and replays like a still, a
   canvas interaction drives `IMouseEditLine` and its `ILineOp` states, the most re-entrancy-
   sensitive code in the project. Until replay is shown, a recorded animation is an asset that does
   not survive a GUI change. Keep `CShotContext` frame-sequence-shaped so waiting costs nothing.
6. **The writer's half is untested end to end.** There is no Sphinx project in this repo. The pages
   under `doc/pages/` are MyST by shape only; nothing has built them, and `figure_language_filename`
   is a claim from the documentation, not a result.
7. **The writer is shown addresses, not names.** `chooseLivePart` labels every step
   `windowTitle-or-className (address)` (`CShotDocMode.cpp:633`) and `addressOf` hands back Qt's own
   object names verbatim, so the choice reads `qt_scrollarea_vcontainer` against `CTableTrk` — class
   names and Qt internals, to someone who is not a developer. The address is the stored identity and
   stays; the *label* wants what the writer sees — a docker's caption, a tab's text, a group box's
   title — and a `qt_`-prefixed internal should not be offered as a step at all.

8. **`-platform offscreen` does not deploy on Windows.** `msvc_64/copyfiles.bat:75` and
   `CopyFilesGis.bat:57` copy `qwindows.dll` alone. `qoffscreen.dll` is a platform plugin, not a
   link-time dependency of `qmapshack.exe`, so nothing pulls it in and `--shoot` cannot start against
   a packaged tree.

9. **The colour scheme is pinned, light.** `--color-scheme light|dark` (`CUiTheme::pinColorScheme`)
   replaces the application palette and sets the style hint, so a writer's dark desktop reaches
   nothing — the palette is what `paletteIsDark()` reads, and `CUiTheme`, `CQmsStyle` and the
   `.svgt` icon engine all follow from it. Two levers because neither is enough alone:
   `QPlatformTheme::requestColorScheme()` has an empty default implementation, so the style hint
   does nothing on X11, and `QStyle::standardPalette()` asks the platform theme for the very scheme
   being pinned away. The colours are Qt's own Fusion arms, both of them, so `dark` is one constant
   in `shots.py` away — what is still undecided is whether the documentation carries a dark set at
   all and, if it does, how the scheme enters the file name.

   **Open, and decide before the naming is fixed:** whether a picture of a message box is in scope
   at all; whether a picture can be
   deliberately scaled down on the way out — asked for by a writer whose pictures were too large,
   and a different knob from the device pixel ratio of §8.2, which only makes two machines agree.

## 9. Ground truth

Measured on this checkout, not assumed:

- **`-platform offscreen` runs the whole application.** Map, DEM and POI paths load, canvases are
  created, draw threads start. No display, no window manager, no Xvfb.
- **Transient widgets render unshown.** A `QMenu` that was never popped up grabs complete with
  labels, separators, check marks and submenu arrows, and without a drop shadow — which is what
  documentation wants. `popup()` works too but logs offscreen warnings, so the never-shown path is
  preferred. Tooltips work only by fishing the private `QTipLabel` out of `topLevelWidgets()`.
  Static convenience dialogs (`QFileDialog::getOpenFileName`, `QMessageBox::critical`) block and
  cannot be shot; a constructed instance can.
- **The determinism knobs already exist** — `--config`, `--locale`, `--font-family`, `--font-size`,
  Qt's `--style`. Only the bundled font is missing.
- **`--config <file>` makes the settings hermetic**: `CSettings` switches to that file and the
  destructor's write-back lands in the scratch copy.
- **State installs through existing public API**: `CMapDraw::setupMapPath()`,
  `CDemDraw::setupDemPath()`, `CPoiDraw::setupPoiPath()`, `CGisWorkspace::loadGisProject()`,
  `CCanvas::loadConfig()` fed from a `.view` INI in the exact format *File ▸ Store View* writes.
- **A dialog inherits its form privately.** `private Ui::IX` → `protected Ui::IX` is the whole cost
  of reaching a widget from a subclass, one keyword, and only on the classes that need it.
  `findChild()` by name is the failure mode this framework exists to remove — but see §3: a live
  address is a string too, and what catches a rename there is the run failing.
- **A live `CMainWindow` is the fixture, not a workaround.** 67 files call `CMainWindow::self()`,
  and its constructor is what initialises `IUnit`, `CSearch`, `IPoiFile`, `IGisItem`,
  `CWptIconManager`, `CActivityTrk`, `CGisWorkspace`, `CGisDatabase`, `CToolBarConfig`,
  `CShortcutConfig` and `CGeoSearchConfig`.
- **Cost, on the demo:** framework ~3000 lines, `shots.py` ~400, the exposure catalog one line per
  class as predicted, changes to existing classes one keyword or one accessor each plus the menu
  split. One picture 1.3 s, fourteen 1.7–2.0 s cold, ~33 ms marginal — the cost is startup, not
  rendering. Two consecutive runs on one machine byte-identical.

## 10. Non-goals

- The documentation toolchain, page structure and hosting (#1209), beyond the constraints Sphinx
  imposes here.
- A CI job. The ticket asks for one; what exists is the developer and reviewer script, and CI is a
  small step once determinism holds.
- Pictures of online map *services* as documentation subjects — WMS/WMTS examples and the like.
  The fixture map is itself online today (§8.1), which is a compromise on this rule, not an
  exception to it.
- Retranslating a live `CMainWindow`.
- Making ffmpeg a build dependency.
- Icons. Inline icon glyphs in the documentation are `.svgt` renders, not screenshots — a small
  exporter, a separate ticket.
