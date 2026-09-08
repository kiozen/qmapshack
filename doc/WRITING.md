# Writing QMapShack documentation

You write a page and mark where pictures go. You take each picture once. The build takes them
again later, after the program changes or on another machine, without you clicking through
anything a second time.

## What you need

Python 3, and a build with the documentation subsystem switched on:

```
cmake -S . -B build -DQMS_DOC_MODE=ON
cmake --build build --target qmapshack
```

It is off by default and is never in a released binary. Without it every command below is
rejected.

`shots.py` looks for the program in `build/bin/`. If yours is elsewhere:

```
doc/tools/shots.py --binary path/to/qmapshack doc
```

**Windows needs one file copied by hand.** Pictures are rendered without a window, which needs
Qt's `qoffscreen` platform plugin. The packaging scripts do not copy it. Take
`platforms\qoffscreen.dll` from your Qt installation and put it beside the `qwindows.dll` you
already have.

---

## The two windows

```
doc/tools/shots.py doc load-a-track
```

opens:

- **Documentation mode** — the panel. Pictures, scenarios, buttons. Never appears in a picture.
- **QMapShack** — the application in one state. This is what is photographed.

Changing state restarts the application. It takes about seven seconds and the panel says so.
Restarting is what makes the pictures reproducible: no state is ever undone, only built again
from nothing.

Closing either window ends the session.

---

## What a picture is made of

| | |
|---|---|
| **name** | your page asks for it; nothing else can create a picture |
| **scenario** | the state the application is in when the picture is taken |
| **subject** | the widget you point at, or a rectangle you drag |

The panel lists one row per picture: its name, its state, and the scenario it is taken in.

Most pictures are taken in **(base)** — the application as it starts. A new row already says
`(base)`, so a picture of a docker or a dialog needs no preparation. A picture that only exists
after you have done something needs a scenario, which you record first.

---

## Setting the base up

```
doc/tools/shots.py doc
```

Arrange the dockers, size the window, set the units and the paths. Select **(base)** in the
scenario list and press **Save config**.

Every chapter starts from this. You should not have to arrange anything again.

### What is fixed for you

A picture must come out identical on every machine, so these are pinned and your desktop cannot
change them:

| | |
|---|---|
| colour scheme | light |
| font | DejaVu Sans 10, shipped inside QMapShack |
| language | English |
| pixels | one image pixel per screen pixel; a HiDPI screen gives the same size |
| style | Fusion |
| time zone | UTC |

---

## Making a page

**1. Write the page and mark the pictures.**

```markdown
<!-- doc/pages/load-a-track.md -->
# Load a track and look at it

The left side is the **Workspace**.

![](../images/load-a-track/workspace.png)
```

**2. Open it.**

```
doc/tools/shots.py doc load-a-track
```

**3. Set each picture's scenario.** Rows start at `(base)`. Leave it there unless the picture
needs a state; then pick a scenario, recording it first if it does not exist.

**4. Take the picture.** Click the row — the application goes into that state. Point at what the
reader should see and press **Ctrl+Shift+F9**. You are asked which part you mean (the list, the
docker around it, the whole window) and which picture it is. Check the result and press **Keep**.

**5. Press "Take all again"** when nothing is outstanding. It runs the build — one process per
scenario, not your session — and reports how many pictures came out different. None means the
page is finished.

**6. Commit the page and the pictures together.**

---

## Scenarios

A scenario is a state, recorded by putting the application into it once.

### Recording

1. Press **Record...**. The application restarts in the base and recording begins.
2. Do what the state is.
3. Press **Stop recording** and name it.

Pick that name in a picture's **Taken in** box, point, press Ctrl+Shift+F9. Several pictures can
share one scenario. A recording always starts from the base, so it is complete in itself.

### What a recording stores

| You did | It stores |
|---|---|
| arranged the window | the arrangement, including every splitter |
| zoomed or moved the map | the centre and the zoom level |
| set maps, elevation data, POIs, units, fonts | those, in the scenario's own settings file |
| selected an item, opened a project | the item, by name |
| clicked the map | the geographic point, and what was under it |
| clicked a button, a tab, a row | that button, tab or row |
| picked a menu entry | the entry, by what it does — never by its text |
| opened a context menu | that menu, on the thing you opened it on |
| clicked a graph | the position on the x axis: a distance along the track, or a time |
| clicked an icon in a grid | the icon, by name |
| changed a box, tick or slider | the control and its value |
| typed into a field | what you typed |

Nothing is stored as a position on your screen. A click is stored as what it landed on. When the
picture is taken again the application looks for the same thing; if it is not there, the build
stops and names the step instead of photographing something else. That is what makes a recording
survive a rebuild, another machine and another window size.

**Not stored:** a hover highlight, a tooltip, an unfinished drag, and a click on something that
does nothing by itself — a splitter handle, a scroll bar, the empty space under the last row. If
you did only those, the panel tells you nothing was recorded.

A control the recorder has not been taught about records nothing at all. You find out because the
picture does not come out, not because the recording quietly did something else.

### Changing one

Click a scenario, or a picture, and the application restarts in that state and stays there.

| Button | Effect |
|---|---|
| **Rename** | costs nothing |
| **Save config** | writes the arrangement, size, maps and settings you have now into the state you are in. On **(base)** it asks first, because every chapter starts from it. |
| **Delete** | removes the scenario. Its pictures have to be taken again. |

Changing a picture's **Taken in** box does the same for that one picture. Both ask first and name
what is lost: a widget you pointed at and a rectangle you dragged mean something else in another
state.

The application always starts fresh in the scenario you picked, with that scenario's settings. If
you then change something — units, a map, the window size — the next picture uses what is on
screen, not what the scenario stores, and the panel says so after the picture. **Save config**
writes the current state into the scenario and brings the two back into line.

---

## Photographing part of a window

For something that is not one widget — a docker and the map beside it, one corner of a dialog:

1. Press **Take a region...**
2. Pick which picture you are taking.
3. Drag a rectangle. Escape cancels.

The rectangle is measured against the window at the size it had while you dragged. Rearrange the
window afterwards and you must press **Save config** and take the region again.

---

## What cannot be photographed

A progress bar halfway through an import, a hover highlight, a menu the program builds and throws
away. Ask for these in three lines:

> Page: load-a-track. The reader must see the bar while a long track is imported.
> By hand: import the demo track and photograph it halfway.

If a dialog says **"Cannot photograph this yet"**, press **Copy**, paste it into a ticket and carry
on. It is one line of code, after which that window works for everyone.

---

## Reference

### Picture states in the panel

| State | Meaning | What to do |
|---|---|---|
| taken | the shot exists and so does its image | nothing |
| not taken | your page asks for it; there is no shot and no file | take it |
| not registered | your page asks for it and a file exists, but no shot of this chapter took it — a hand-made picture, or one from an older version | nothing, unless you want the shooter to own it |
| no image | the chapter has the shot, the file is gone | take it again |
| not used | the file exists, no page asks for it | **Remove unused** deletes it |

Ctrl+Shift+F9 is the only key. Everything else is a button, because the mouse is busy pointing.

### Picture names

You choose them in your page. The panel offers exactly those, so you cannot mistype one.

```
<chapter>/<subject>[-<variant>]
```

`<chapter>` is the page's file name without `.md`. `<subject>` is what the reader sees, lower case
with hyphens — `track-details`, not a class name. `<variant>` is for the same subject in another
state — `track-details-graphs`.

Renaming later means editing the page and the chapter file, so choose once.

### Files

```
doc/pages/load-a-track.md            your text — this is what says a picture exists
doc/shots/load-a-track.json          the pictures and the scenarios
doc/shots/load-a-track/<name>.ini    one scenario's settings
doc/images/load-a-track/*.png        the pictures
doc/shots/fixture/shots.ini          the base
```

Everything is named after the page. You write the first file; QMapShack writes the rest.

The example data — one set of projects and one map — is shared by every chapter. If it does not
suit your page, say so to whoever maintains the documentation setup.

### Commands

| Command | Does |
|---|---|
| `shots.py doc [CHAPTER]` | open QMapShack and take pictures |
| `shots.py chapter [NAME]` | take one page's pictures again, without a window |
| `shots.py build [--only GLOB]` | take every page's pictures again |
| `shots.py reap [--delete]` | list, or remove, pictures no page uses |

`chapter` and `build` name every picture they take. A picture that fails is printed under its
page with the reason, the rest of the page is still taken, and the run ends with a count and a
non-zero exit code. `build` carries on to the next page.

`inspect` and `explore` are for working on the shooter itself, not on a page.

---

## Not built yet

- animations — stills only
- elevation data — no DEM in the example data
- adding your own example data to a chapter
- recording a hover, a tooltip or a drag
- dark mode — the pictures are light, and only light
- other languages — the pictures are English, and only English

The map is OpenStreetMap, online. The first run needs a network; the tiles are cached afterwards.
An empty map is the network, not your page. Published pictures carry
*© OpenStreetMap contributors*.
