# Writing QMapShack documentation

You write a page and say where you want pictures. QMapShack takes them, and takes them again
later — after the program changes, on another machine — without you clicking through anything a
second time.

## What you need

Python 3 and your own build with the documentation subsystem on:

```
cmake -S . -B build -DQMS_DOC_MODE=ON
cmake --build build --target qmapshack
```

It is off by default and never in a released binary. A build without it ignores every command here.

`shots.py` looks for the program in `build/bin/`. If yours is elsewhere, say so:

```
doc/tools/shots.py --binary path/to/qmapshack doc
```

**On Windows, copy one file by hand.** Pictures are taken without a window, which needs Qt's
`qoffscreen` platform plugin. The packaging scripts do not copy it. Take
`platforms\qoffscreen.dll` from your Qt installation and put it next to the `qwindows.dll` you
already have.

---

## Two windows

```
doc/tools/shots.py doc load-a-track
```

opens two windows:

- **Documentation mode** — the panel. Your pictures, your scenarios, all the buttons.
- **QMapShack** — the application, in one state. This is what gets photographed.

The application window is thrown away and started again every time you change state. That is why
the pictures always match: nothing is ever undone, only started fresh. It takes a few seconds and
a box tells you to wait.

The panel stays. It never appears in a picture.

**Closing either window ends the session.**

---

## What a picture is made of

Three things, and you decide all three:

| | |
|---|---|
| **its name** | your page asks for it. Nothing else can create a picture. |
| **its scenario** | the state the application is in when the picture is taken |
| **its subject** | the widget you point at, or a rectangle you drag |

The panel shows one row per picture: the name, whether it has been taken, and a **Taken in** box
holding its scenario.

**Most pictures want `(base)`** — the application as it starts. That is what a new row already
says, so a picture of a docker or a dialog needs no set-up. A picture that only exists after you
have done something — a track selected, the map zoomed, an item clicked open — needs a scenario
you record first.

---

## Set the base up once

```
doc/tools/shots.py doc
```

Arrange the dockers, size the window, set the units and the paths. Then select **(base)** in the
scenario list and press **Save config**. Confirm.

Every chapter opens like this from now on. You should not have to arrange anything again.

### What the tool decides for you

A picture has to come out the same on every machine, so these are pinned:

| | |
|---|---|
| colour scheme | always light, whatever your desktop is |
| font | DejaVu Sans 10, shipped inside QMapShack |
| language | English |
| screen | one image pixel per screen pixel — a HiDPI screen does not make bigger pictures |
| style | Fusion |
| time zone | UTC |

A dark desktop, a high-resolution laptop or a German system changes nothing.

---

## Making a page

**1. Write the page, with the pictures you want.**

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

**3. Check the Taken in box.** Every row starts at **(base)**. Leave it there for a picture that
needs no set-up. Pick a scenario for one that does, and record it first if you have not got it.

**4. Take each picture.** Click the picture's row — the application goes into its state. Point the
mouse at what the reader should see and press **Ctrl+Shift+F9**. You are asked which part you mean
— the list, the docker around it, the whole window — and which picture it is. Look at the result
and press **Keep**.

**5. Press "Take all again"** when nothing is missing. It runs the build - `shots.py chapter`, one
process per scenario, not your session - and reports how many pictures came out different. None
means your page is done.

**6. Commit the page and the pictures together.**

---

## Scenarios

A scenario is a state you record by getting the application into it once.

### Recording one

1. Press **Record...**. The application starts again in the base and begins recording.
2. Do what the state is.
3. Press **Stop recording** and give it a name.

Then pick that name in a picture's **Taken in** box, point, and press Ctrl+Shift+F9. Several
pictures can share one scenario.

A recording always starts from the base, so what you record is complete in itself.

### What a recording keeps

| You did | It keeps |
|---|---|
| arranged the window | that arrangement — it decides how big the picture is |
| zoomed or moved the map | the area you ended up looking at |
| set maps, elevation data, POIs, units, fonts | those, in the scenario's own settings file |
| selected an item, opened a project | that item, by name |
| clicked an item on the map | where you clicked, and what was there |
| changed a box, tab, tick or slider | that control and its value |

Nothing is kept as pixels or screen positions. That is why a recording still works after the
program is rebuilt or on another computer.

It keeps nothing that leaves nothing behind: a button press that just did something, a tooltip, a
hover highlight, a half-finished drag. Stop after only those and it tells you it recorded nothing.

### Changing one

Click a scenario, or click a picture, and the application starts again in that state and stays
there. That is how you look at it.

| Button | Effect |
|---|---|
| **Rename** | costs nothing |
| **Save config** | saves the arrangement, the size, the map and the settings you have now into the state you are in. On **(base)** it asks first, because every chapter starts from it. |
| **Delete** | removes the scenario. The pictures taken in it have to be taken again. |

Changing a picture's **Taken in** box does the same to that one picture. Both ask first and name
what will be lost. The reason: a widget you pointed at and a rectangle you dragged mean something
else in another state.

Units, fonts and window size are read when QMapShack starts. A picture you take now uses what is
on screen, not what the scenario has stored. The panel says when they differ; **Save config**
brings them in line.

---

## A part of the window

For something that is not one widget — a docker and the map beside it, one corner of a dialog:

1. Press **Take a region...**
2. Pick which picture you are taking.
3. Drag a rectangle. Escape cancels.

The rectangle is measured against the window at the size it had while you dragged. If you
rearrange the window afterwards, press **Save config** and take the region again.

---

## Things you cannot photograph

A progress bar halfway through an import, a hover highlight, a menu QMapShack builds and throws
away. Ask for these in three lines:

> Page: load-a-track. The reader has to see the bar while a long track is being imported.
> By hand: import the demo track and photograph it halfway.

If a dialog says **"Cannot photograph this yet"**, press **Copy**, paste it into a ticket and carry
on. It is one line of code, and then that window works for everyone.

---

## Reference

### The panel

Scenarios on top, the pictures your page asks for below.

| Picture says | Meaning |
|---|---|
| taken | the picture exists and your page uses it |
| **missing** | your page uses it, nobody has taken it |
| **no image** | it is known, the file is gone — take it again |
| not used | the file exists, no page uses it — **Remove unused** clears it |

Ctrl+Shift+F9 is the only key. The mouse is busy pointing, so everything else is a button.

### Picture names

You choose them in your page. The panel offers you exactly those, so you cannot mistype one.

```
<chapter>/<subject>[-<variant>]
```

`<chapter>` is your page's file name without `.md`. `<subject>` is what the reader sees, in lower
case with hyphens — `track-details`, not a class name. `<variant>` only for the same thing in
another state — `track-details-graphs`.

Renaming later means editing the page and the chapter file, so choose once.

### Files

```
doc/pages/load-a-track.md            your text — this is what says a picture exists
doc/shots/load-a-track.json          the pictures and the scenarios
doc/shots/load-a-track/<name>.ini    one scenario's settings
doc/images/load-a-track/*.png        the pictures
doc/shots/fixture/shots.ini          the base
```

Everything is named after your page. You write the first file; QMapShack writes the rest.

The example data — one set of projects and one map — is shared by every chapter. If it does not
suit your page, say so to whoever maintains the documentation setup.

### Commands

| Command | Does |
|---|---|
| `shots.py doc [CHAPTER]` | open QMapShack to take pictures |
| `shots.py chapter [NAME]` | take one chapter's pictures again, without a window |
| `shots.py build` | take every chapter's pictures again |
| `shots.py reap [--delete]` | list, or remove, pictures no page uses |
| `shots.py list` | windows QMapShack can photograph without you opening them |

---

## Not built yet

- animations — the key takes stills only
- elevation data — no DEM in the example data
- adding your own example data to a chapter
- recording anything that leaves no state behind
- a second set of pictures for dark mode — the pictures are light, and only light
- other languages — the pictures are English, and only English

The map is OpenStreetMap, online. The first run needs a network; the tiles are cached afterwards.
If the map stays empty, that is the network, not your page. Published pictures carry
*© OpenStreetMap contributors*.
