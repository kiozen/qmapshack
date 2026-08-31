# Writing QMapShack documentation

You write a page and say where you want pictures. QMapShack takes them, and can take them
again later — after the program changes, on another machine — without you clicking through
anything a second time.

You need Python 3 and your own build of this checkout, configured with the documentation
subsystem switched on:

```
cmake -S . -B build -DQMS_DOC_MODE=ON
cmake --build build --target qmapshack
```

It is off by default and never in a released binary: it drives the program through synthesized
input and links Qt's test library. A build without it starts normally and ignores every command
below.

`shots.py` looks for the program in `build/bin/`. If your build puts it somewhere else — the
usual case on Windows — name it:

```
doc/tools/shots.py --binary path/to/qmapshack doc
```

**On Windows, one file has to be copied by hand.** Pictures are taken without a window, which
needs Qt's `qoffscreen` platform plugin, and the packaging scripts do not copy it. Take
`platforms\qoffscreen.dll` from your Qt installation and put it beside the `qwindows.dll` you
already have. Without it every command except `doc` stops with a message about the platform
plugin.

---

## What a picture is made of

Every picture in the documentation is three things, and you decide all three:

| | |
|---|---|
| **its name** | your page asks for it. Nothing else can create a picture. |
| **its scenario** | the state the application is in when the picture is taken |
| **its subject** | the widget you point at, or a rectangle you drag |

The panel shows one row per picture: the name, whether it has been taken, and a **Taken in**
box holding its scenario. A picture is taken by putting a scenario in that box, pointing at
the subject and pressing **Ctrl+Shift+F9**.

**Every picture has a scenario, and most of them want `(base)`** — the application as it
starts, with nothing done to it. That is what a new row already says, so a picture of a
docker or a dialog needs no set-up at all. A picture that only exists after you have done
something — a track selected, the map zoomed in, an item clicked open — needs a scenario you
record first.

Change the name, the scenario or the subject, and the picture is taken again. That is the
whole loop.

---

## Before you start

Open documentation mode once and set the application up the way your pages should look:

```
doc/tools/shots.py doc
```

Arrange the dockers, size the window, set the units and the paths. Then select **(base)** at
the top of the scenario list and press **Update**. Confirm.

Every chapter opens like this from now on. Do it once; you should not have to arrange
anything again.

### What the tool decides for you

Some things are not yours to set, because a picture has to come out the same on every
machine. `shots.py` pins them for both documentation mode and a headless run:

| | |
|---|---|
| colour scheme | always light, whatever your desktop is set to |
| font | DejaVu Sans 10, shipped inside QMapShack rather than taken from your system |
| language | English |
| screen | 1920x1200, one image pixel per screen pixel — a HiDPI screen does not make bigger pictures |
| style | Fusion |
| time zone | UTC |

So a dark desktop, a high-resolution laptop or a German system changes nothing about what
you get.

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

**3. Check the Taken in box.** Every row starts at **(base)**. Leave it there for a picture
that needs no set-up; pick a scenario for one that does, and record it first if you have not
got it — see below.

**4. Take each picture.** Point the mouse at what the reader should see and press
**Ctrl+Shift+F9**. You are asked which part you mean — the list, the docker around it, the
whole window — and which picture it is. Look at the result, press **Keep**.

**5. Press "Take all again"** when nothing is missing. It reports how many pictures came out
different. None means your page is done.

**6. Commit the page and the pictures together.** They belong to the repository; nobody has
to run anything to see your documentation.

---

## Scenarios

A scenario is a state you record by getting the application into it once.

### Recording one

1. Select the scenario to start from, or **(base)** for the plain application.
2. Press **Record...**. The application goes into that state.
3. Do what is still missing.
4. Press **Stop recording** and give it a name.

Then pick that name in the **Taken in** box of a picture's row, point, and press
Ctrl+Shift+F9 as usual. Several pictures can share one scenario.

You do not have to build the same state twice: start from the closest scenario you already
have. What you record is complete in itself, so deleting the one you started from later
changes nothing.

### What a recording keeps

| You did | It keeps |
|---|---|
| arranged the window | that arrangement — it decides how big the picture is |
| zoomed or moved the map | the map area you ended up looking at |
| set maps, elevation data, POIs, units, fonts | those, in the scenario's own settings file |
| selected an item, opened a project | that item, by name |
| clicked an item on the map | the position you clicked, and what was there |
| changed a box, tab, tick or slider | that control and its value |

Nothing is stored in pixels or screen positions, which is why a recording still works after
the program is rebuilt or on another computer.

It does **not** keep anything that leaves nothing behind: a button press that just did
something, a tooltip, a hover highlight, a half-finished drag. Stop after only those and it
tells you it recorded nothing.

### Changing one

Click a scenario, or click a picture, and the application goes into that state and stays
there. That is how you look at it.

| Button | Effect |
|---|---|
| **Rename** | costs nothing |
| **Update** | replaces the scenario's arrangement, map and settings with what is on screen |
| **Delete** | removes it, and the pictures taken in it have to be taken again |

Changing a picture's **Taken in** box has the same effect as Delete for that one
picture. Both
ask first and name what will be lost. The reason: a widget you pointed at and a
rectangle you
dragged mean something else in a different state.

One thing you cannot see immediately: units, fonts and colours are read when QMapShack
starts. A picture you take now uses the settings on screen, not the ones the scenario has
stored. The panel tells you when they differ; **Update** brings them in line.

---

## A part of the window

For something that is not one widget — a docker and the map beside it, one corner of a
dialog:

1. Press **Take a region...**
2. Pick which picture you are taking. Its scenario is built and left standing.
3. Drag a rectangle. Escape cancels.

The rectangle is measured against the window at the size it had while you dragged. If you
rearrange the window afterwards, press **Update** and take the region again.

---

## Things you cannot photograph

A progress bar halfway through an import, a hover highlight, a menu QMapShack builds and
throws away. Ask for these, in three lines:

> Page: load-a-track. The reader has to see the bar while a long track is being imported.
> By hand: import the demo track and photograph it halfway.

If a dialog says **"Cannot photograph this yet"**, press **Copy**, paste it into a
ticket and
carry on. It is one line of code, and then that window works for everyone.

---

## Reference

### The panel

The top half is the scenarios, the bottom half the pictures your page asks for.

| Picture says | Meaning |
|---|---|
| taken | the picture exists and your page uses it |
| **missing** | your page uses it, nobody has taken it |
| **no image** | it is known, but the file is gone — take it again |
| not used | the file exists, no page uses it — **Remove unused** clears it |

Ctrl+Shift+F9 is the only key. The mouse is busy pointing, so everything else is a button.

### Picture names

You choose them in your page; the panel offers you exactly those, so you cannot mistype one.

```
<chapter>/<subject>[-<variant>]
```

`<chapter>` is your page's file name without `.md`. `<subject>` is what the reader sees, in
lower case with hyphens — `track-details`, not a class name. `<variant>` only for the same
thing in another state — `track-details-graphs`.

Renaming later means editing the page and the chapter file, so choose once.

To point an existing name at something else: select it, point at the new thing, press
Ctrl+Shift+F9 and take the name offered first.

### Files

```
doc/pages/load-a-track.md            your text — this is what says a picture exists
doc/shots/load-a-track.json          the pictures and the scenarios
doc/shots/load-a-track/<name>.ini    one scenario's settings
doc/images/load-a-track/*.png        the pictures
doc/shots/fixture/shots.ini          the base
```

Everything is named after your page. You write the first file; QMapShack writes the rest.

The example data — one set of projects and one map — is shared by every chapter. If it does
not suit your page, say so to whoever maintains the documentation setup; you cannot add
to it
yourself yet.

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

The map is OpenStreetMap, online. The first run needs a network; the tiles are cached
afterwards. If the map stays empty, that is the network, not your page. Published pictures
carry *© OpenStreetMap contributors*.
