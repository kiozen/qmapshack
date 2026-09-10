# Writing QMapShack documentation

You write a page and mark where the pictures go. You take each picture once, by pointing at it.
QMapShack takes them all again later — after the program changes, on another machine — without you
clicking through anything a second time.

One thing before the first time: the pictures need a build with documentation mode switched on.
[Setting up](#setting-up) is four lines long.

---

## The loop

```
  ┌─────────────────────────────────────────────────────────────┐
  │ [0]  doc/tools/shots.py doc <page>                          │
  │                                                             │
  │        two windows: the panel, and QMapShack in one state   │
  │        the panel lists one row per picture the page wants   │
  │                                                             │
  │        reopen only after you rebuild QMapShack              │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ [1]  write the page, mark where the pictures go             │
  │                                                             │
  │        ![](.../name.png)  ->  one row per picture           │
  │                                                             │
  │        changed only text?          the panel is right       │
  │        added or removed an image?  press Reload page        │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ [2]  pick the next row that is not taken                    │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │      does what it shows depend on something being           │
  │      selected, loaded or opened?                            │
  │                                                             │
  │        no   ->  a docker, the map, a setup dialog           │
  │        yes  ->  a track's details, profile, options         │
  │                 => record that state once as a scenario,    │
  │                    and pick it for the row                  │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ [3]  take it                                                │
  │                                                             │
  │        one widget      ->  point at it, Ctrl+Shift+F9       │
  │        not one widget  ->  Take a region..., pick the       │
  │                            picture, drag the rectangle      │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │      look at what came out - is it right?                   │
  │                                                             │
  │        yes .....................  go to [2]  next shot      │
  │        wrong part of the window   go to [3]  take again     │
  │        wrong state .............  go to [3]  after          │
  │                                   recording or fixing       │
  │                                   the scenario              │
  │        the page was wrong ......  go to [1]  rewrite it     │
  └─────────────────────────────────────────────────────────────┘

    when no row says "not taken" any more:

  ┌─────────────────────────────────────────────────────────────┐
  │      Take all again                                         │
  │        proves every picture rebuilds without you            │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │      Publish                                                │
  │        only what really changed goes into the project       │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │      commit                                                 │
  └─────────────────────────────────────────────────────────────┘

    later, the program has changed:
    Take all again -> look at what came out different
                   -> go to [3]
```

| | step | more, if you need it |
|---|---|---|
| `[0]` | open the session | [The panel and QMapShack](#the-panel-and-qmapshack) |
| `[1]` | write the page | [Writing the page](#writing-the-page), [Picture names](#picture-names) |
| `[2]` | pick a row | [What the rows mean](#what-the-rows-mean) |
| | it needs a state | [Scenarios](#scenarios) |
| `[3]` | point at it | [Taking a picture](#taking-a-picture) |
| `[3]` | drag a rectangle | [Photographing part of a window](#photographing-part-of-a-window) |
| | take all again | [Take all again](#take-all-again) |
| | publish | [Publishing](#publishing) |
| | it will not photograph | [What cannot be photographed](#what-cannot-be-photographed) |

Everything below is that detail. You should not need it to start.

---

## Setting up

Python 3, and a build with the documentation subsystem switched on:

```
cmake -S . -B build -DQMS_DOC_MODE=ON
cmake --build build --target qmapshack
```

It is off by default and is never in a released binary. Without it every command here is rejected.

`shots.py` looks for the program in `build/bin/`. If yours is elsewhere, pass it:

```
doc/tools/shots.py --binary path/to/qmapshack doc
```

**Windows needs one file copied by hand.** Pictures are rendered without a window, which needs Qt's
`qoffscreen` platform plugin, and the packaging scripts do not copy it. Take
`platforms\qoffscreen.dll` from your Qt installation and put it beside the `qwindows.dll` you
already have.

---

## The panel and QMapShack

```
doc/tools/shots.py doc load-a-track
```

opens two windows:

- **the panel** — your pictures, your scenarios, the buttons. Never appears in a picture.
- **QMapShack** — the program in one state. This is what is photographed.

Changing state restarts QMapShack. It takes about seven seconds and the panel says so. Restarting is
what makes the pictures reproducible: no state is ever undone, only built again from nothing.

Closing either window ends the session.

Ctrl+Shift+F9 is the only key. Everything else is a button, because the mouse is busy pointing.

---

## Writing the page

The page is the order form. Nothing else can create a picture: the panel lists one row per `![]()`
line and offers exactly those names, so you cannot mistype one.

```markdown
<!-- doc/pages/load-a-track.md -->
# Load a track and look at it

The left side is the **Workspace**.

![](../images/load-a-track/workspace.png)
```

The row list is read when the panel opens and when you press **Reload page**. Editing your prose
changes nothing in the panel; adding or removing an image line does, so press Reload after that.

### Setting the base up

The base is QMapShack as the configuration starts it, and every chapter opens on it. Most pictures
are taken in it, so a new row already says `(base)` and needs no preparation.

To change it: `doc/tools/shots.py doc`, arrange the dockers, size the window, set the units and the
paths, select **(base)** in the scenario list and press **Save config**. You should not have to
arrange anything again.

---

## Scenarios

**A picture needs a scenario when what it shows depends on an input that is not always there.** A
track's details, its elevation profile, its screen options are empty or absent until a track is
selected. A docker, the map, a setup dialog are not — they look the same whatever is loaded.

You do not have to work this out in advance. Take the picture; if what came out is empty or is the
wrong thing, that is your answer.

### Recording one

1. Press **Record...**. QMapShack restarts in the base and recording begins.
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
picture is taken again QMapShack looks for the same thing; if it is not there, the run stops and
names the step instead of photographing something else. That is what makes a recording survive a
rebuild, another machine and another window size.

**Not stored:** a hover highlight, a tooltip, an unfinished drag, and a click on something that does
nothing by itself — a splitter handle, a scroll bar, the empty space under the last row. If you did
only those, the panel tells you nothing was recorded.

A control the recorder has not been taught about records nothing at all. You find out because the
picture does not come out, not because the recording quietly did something else.

### Changing one

Click a scenario, or a picture, and QMapShack restarts in that state and stays there.

| Button | Effect |
|---|---|
| **Rename** | costs nothing |
| **Save config** | writes the arrangement, size, maps and settings you have now into the state you are in. On **(base)** it asks first, because every chapter starts from it. |
| **Delete** | removes the scenario. Its pictures have to be taken again. |

Changing a picture's **Taken in** box does the same for that one picture. Both ask first and name
what is lost: a widget you pointed at and a rectangle you dragged mean something else in another
state.

QMapShack always starts fresh in the scenario you picked, with that scenario's settings. If you then
change something — units, a map, the window size — the next picture uses what is on screen, not what
the scenario stores, and the panel says so after the picture. **Save config** writes the current
state into the scenario and brings the two back into line.

---

## Taking a picture

Click the row: QMapShack goes into that state. Point at what the reader should see and press
**Ctrl+Shift+F9**. You are asked which part you mean — the list, the docker around it, the whole
window — and which picture it is. Check the result and press **Keep**.

---

## Photographing part of a window

For something that is not one widget — a docker and the map beside it, one corner of a dialog:

1. Press **Take a region...**
2. Pick which picture you are taking.
3. Drag a rectangle. Escape cancels.

The rectangle is measured against the window at the size it had while you dragged. Rearrange or
resize the window afterwards and that picture has to be dragged again — a widget survives that, a
rectangle does not.

---

## Take all again

Press it when no row says *not taken*. It runs the build — one process per scenario, not your
session — and reports how many pictures came out different.

None means every picture can be taken again from what was recorded, which is what the page needs. If
one differs, it depended on something its shot does not record; look at it, and record that as a
scenario of its own.

---

## Publishing

Press **Publish** when you are done, before you commit.

The pictures you take are kept aside, not put straight into the project. Publish is what puts them
in — and only the ones you really changed.

It takes your chapter's pictures twice, both times on your own machine: once from the page as it
already is in the project, and once from the page as you have it now. A picture that comes out the
same both times was not changed by you, and the project keeps the one it has. A picture that comes
out different was changed by you, and yours goes in.

What is left to commit is then just your work — usually two or three pictures, not four hundred.

That is also why nothing bad happens if you forget. A picture you never published is simply not in
the project, so you cannot commit one by accident. The panel asks anyway when you close it, but only
when there is something to publish:

> You have taken pictures that are not published yet. Publish them now?

Taking pictures and changing nothing else is not something to publish, so that does not ask.

| What you did | What Publish costs |
|---|---|
| changed only text | nothing to take again, done at once |
| took pictures, changed no state | nothing goes in, done at once |
| recorded or changed a scenario | that chapter's pictures are taken twice, a few seconds each |

**One thing it cannot spot.** If a programmer changes the way QMapShack draws something, your
pictures are out of date — but Publish uses the same QMapShack for both halves of its comparison, so
it sees nothing different. Those have to be taken again on purpose, and that is not your job.

If you prefer the command line:

```
doc/tools/shots.py publish --dry-run     lists what it would do, changes nothing
doc/tools/shots.py publish
```

---

## What cannot be photographed

A progress bar halfway through an import, a hover highlight, a menu the program builds and throws
away. Ask for these in three lines:

> Page: load-a-track. The reader must see the bar while a long track is imported.
> By hand: import the demo track and photograph it halfway.

If a dialog says **"Cannot photograph this yet"**, press **Copy**, paste it into a ticket and carry
on. It is one line of code, after which that window works for everyone.

---

## What the rows mean

| State | Meaning | What to do |
|---|---|---|
| taken | the shot exists and so does its image | nothing |
| not taken | your page asks for it; there is no shot and no file | take it |
| not registered | your page asks for it and a file exists, but no shot of this chapter took it — a hand-made picture, or one from an older version | nothing, unless you want the shooter to own it |
| no image | the chapter has the shot, the file is gone | take it again |
| not used | the file exists, no page asks for it | **Remove unused** deletes it |

---

## Picture names

You choose them in your page, and the panel offers exactly those.

```
<chapter>/<subject>[-<variant>]
```

`<chapter>` is the page's file name without `.md`. `<subject>` is what the reader sees, lower case
with hyphens — `track-details`, not a class name. `<variant>` is for the same subject in another
state — `track-details-graphs`.

Renaming later means editing the page and the chapter file, so choose once.

---

## What is fixed for you

These are pinned so your desktop cannot change what a picture shows:

| | |
|---|---|
| colour scheme | light |
| font | DejaVu Sans 10, shipped inside QMapShack |
| language | English |
| pixels | one image pixel per screen pixel; a HiDPI screen gives the same size |
| style | Fusion |
| time zone | UTC |

What is **not** fixed is the last detail of the pixels. Every system draws letters a little
differently, so the same picture taken on two machines is never quite the same file, even though it
looks the same. That is normal, it is nobody's mistake, and it is the whole reason
[Publish](#publishing) exists. You do not have to think about it.

---

## Files

```
doc/pages/load-a-track.md            your text — this is what says a picture exists
doc/shots/load-a-track.json          the pictures and the scenarios
doc/shots/load-a-track/<name>.ini    one scenario's settings
doc/images/load-a-track/*.png        the pictures, as the project has them
doc/shots/fixture/shots.ini          the base
doc/images/_work/                    the pictures you have taken but not published
doc/images/_baseline/                Publish's workings
```

Everything is named after the page. You write the first file; QMapShack writes the rest.

The last two are not in git and you can delete either at any time. The panel always shows you your
own picture when you have one, and the project's when you have not.

The example data — one set of projects and one map — is shared by every chapter. If it does not suit
your page, say so to whoever maintains the documentation setup.

---

## Commands

| Command | Does |
|---|---|
| `shots.py doc [CHAPTER]` | open QMapShack and take pictures |
| `shots.py chapter [NAME]` | take one page's pictures again, without a window |
| `shots.py build [--only GLOB]` | take every page's pictures again |
| `shots.py reap [--delete]` | list, or remove, pictures no page uses |
| `shots.py publish [--dry-run]` | put in only the pictures your work really changed |

`chapter` and `build` name every picture they take. A picture that fails is printed under its page
with the reason, the rest of the page is still taken, and the run ends with a count and a non-zero
exit code. `build` carries on to the next page.

`inspect` and `explore` are for working on the shooter itself, not on a page.

---

## Not built yet

- animations — stills only
- elevation data — no DEM in the example data
- adding your own example data to a chapter
- recording a hover, a tooltip or a drag
- dark mode — the pictures are light, and only light
- other languages — the pictures are English, and only English

The map is OpenStreetMap, online. The first run needs a network; the tiles are cached afterwards. An
empty map is the network, not your page. Published pictures carry
*© OpenStreetMap contributors*.
