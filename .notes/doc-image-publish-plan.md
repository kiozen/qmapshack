# PLAN — publish only the pictures that really changed

**Status:** implemented — `shots.py publish` and the panel's *Publish* button. Measured below.

**Scope:** `doc/tools/shots.py` (a `publish` command), `src/qmapshack/shoot/CShotDocPanel.cpp`
(a fourth button), `CShotDocLauncher` (it already runs `shots.py` and owns the file operations).

---

## The defect

`shots.py chapter|build` writes every picture of a chapter on every run. The bytes depend on the
machine, so every writer's run rewrites every PNG and git records a change that is not one. Over a
few hundred pictures that grows the repository without a single documented thing having changed.

Measured on this checkout, chapter `test`, 9 pictures:

| comparison | result |
|---|---|
| two runs, same machine, same environment | **byte-identical**, all 9 |
| a run against the committed `doc/images/test/` | **byte-identical**, all 9 |
| a run with `FONTCONFIG_FILE` swapped for one with hinting/AA off | **all 9 differ** |

```
delagte.png                     2540 /  88324 px  ( 2.88%)  maxdelta 255
menu-setup-workspace.png        2737 /  18424 px  (14.86%)  maxdelta 255
track-details.png              26359 / 640668 px  ( 4.11%)  maxdelta 255
track-energy.png               27545 / 361513 px  ( 7.62%)  maxdelta 255
track-range.png                18571 / 535108 px  ( 3.47%)  maxdelta 255
track-scropt.png               10343 / 159600 px  ( 6.48%)  maxdelta 239
workspace-setup-database.png   10986 / 292020 px  ( 3.76%)  maxdelta 253
workspace-setup-general.png    17418 / 292020 px  ( 5.96%)  maxdelta 255
workspace-setup-workspace.png  16009 / 292020 px  ( 5.48%)  maxdelta 253
```

Two facts to build on. A run is byte-deterministic on one machine, so the noise is purely a
cross-environment term and never a per-run one. And fontconfig reaches the offscreen render:
bundling `src/fonts/` pins the glyph outlines, their rasterization still comes from the desktop.

`--font-family`, `QT_QPA_PLATFORMTHEME=generic`, the scaling family, `TZ` and `LC_ALL` are already
pinned in `pinned_env()`; fontconfig is not. Pinning it would remove that one term, but it does not
solve the problem — a different Qt or FreeType still renders differently, tested on two machines.

## What does not work

- **A pixel tolerance.** The noise above is 3–15 % of pixels at full black↔white swing on glyph
  edges; a real change is often one glyph or one badge. The two overlap, so no threshold separates
  them.
- **A blessed renderer** (container or VM). Rejected: too much overhead for the project, and two
  machines with different Linux and Qt were already measured not to agree.
- **A stored per-shot baseline.** It has to be taken *before* the edit, and no convention makes
  every contributor do that.

## The mechanism

A writer's edit touches only tracked text — `doc/pages/*.md`, `doc/shots/*.json`,
`doc/shots/<ch>/*.ini`, `doc/shots/fixture/shots.ini`. The binary does not change. So the *before*
state is recoverable from git at any time and can be rendered on demand, on the writer's own
machine, with the same binary they have been using.

Compare that against their working-tree render. Machine, Qt and code appear in both passes and
cancel; the only surviving difference is the recipe.

```
shots.py publish
  1. git diff --name-only HEAD -- doc/pages doc/shots   → which chapters were touched
  2. git archive HEAD doc | tar -x -C <tmp>             → HEAD's recipe (~750K)
  3. render those chapters from <tmp>                   → before
  4. render those chapters from the working tree        → after
  5. per image: bytes equal   → git checkout -- <image>
                bytes differ → keep the working-tree one
```

Granularity is per image, not per chapter: a chapter of 30 pictures where one scenario changed
restores the other 29 and keeps only what moved.

Step 3 must run **the temp tree's own** `shots.py` with an absolute `--binary`. `REPO` resolves
from `__file__`, so the working tree's copy would render the working tree's recipe twice and always
conclude nothing changed.

## What it cannot see

A code change that repaints something with no recipe change — by construction, since the same
binary renders both passes. That case belongs to the developer who made it, not the writer:
`publish --all` on the chapters they know they touched, or a CI job that builds base and PR on one
machine and renders both. Neither is part of this plan.

## The writer's side

Nothing before writing, no git, no discipline. The loop stays as it is: work in the doc panel,
render as often as wanted, look at the pages. One button at the end.

A fourth button beside *Take a region…* / *Take all again* / *Remove unused* in
`CShotDocPanel.cpp:195`: **Publish**. It reports how many pictures changed and lists them.
`CShotDocLauncher` runs it — it already owns every file operation and already runs `shots.py`
through `--doc-python`, which is what a Windows session needs (`python3` there is normally the
store's app execution alias).

## Measured

Another machine was simulated with `FONTCONFIG_FILE` — the same swap that moves 3–15 % of pixels
above — so every picture rendered differently from the committed one while meaning the same.

| case | renders | wall | outcome |
|---|---|---|---|
| clean tree | 0 | 0.12 s | — |
| 9 pictures dirty, no recipe change | 0 | 0.16 s | all 9 put back |
| one shot's `size` changed, whole chapter dirty | 2 | 13.8 s | 1 kept, 8 put back |
| the same, baseline cached | 1 | 6.9 s | 1 kept, 8 put back |

A control says the compare is not simply answering "unchanged" to everything: `Units/type` 0 → 1 in
`track-range.ini` moves no picture in this chapter, confirmed by rendering both recipes by hand, and
`publish` reports none. Changing a shot's `size` does move one, and it reports that one.

## The tool is not part of the recipe

`head_tree()` puts the **working tree's** `doc/tools/` into the extracted checkout, keeping only
HEAD's `doc/shots/`. Rendering HEAD's own `shots.py` compares two versions of the shooter as well as
two recipes, and HEAD's - not knowing `QMS_SHOTS_CACHE` - renders against the empty tile cache of a
throwaway checkout, which reported every map picture as changed. That was a live false positive
during development, not a hypothetical.

## Left to do

1. Optional, independent: pin fontconfig in `pinned_env()` the way the platform theme is pinned. It
   removes one noise term and makes a writer's session and the build agree on one machine. Nothing
   above needs it.
2. A CI check would catch the case `publish` cannot see - a code change that repaints something -
   by rendering base and PR on one machine. Not viable here: a free GitHub runner would have to
   build Qt6, GDAL, PROJ and Routino in documentation mode first.

## The work area

`doc/images/` is written by `publish` and by nothing else. Every render - a writer's session,
`chapter`, `build` - lands in `doc/images/_work/`, which is git-ignored, and `publish` empties it
once it has taken what it wants.

That is what makes forgetting harmless. Rendering into the tracked directory leaves a changed file
for every picture taken, so a writer who never publishes commits noise and nobody notices. Rendering
beside it leaves nothing to commit at all: forgetting costs a picture that was not updated, which is
visible, instead of a change that was not made, which is not.

`CShotChapter::imagePath()` resolves the work copy first and the published one after, so the panel
shows the writer their own picture where they have one and the project's where they have not.
`hasUnpublishedImages()` is one file in the work area, which is exactly "taken since the last
publish" because `publish` empties it. That alone is not enough to ask the closing question with:
taking a chapter's pictures again without editing anything leaves nine of them there and can
produce nothing, because an untouched chapter is only ever put back the way it was. So
`wouldPublishAnything()` adds the git half through `shots.py publish --check`, which answers
"is any chapter's recipe different from the last commit" in 0.06 s without taking a picture or
starting the application.

## Verification

- Two `publish` runs with no edits in between must leave the tree clean.
- Edit one scenario's `.ini`; only that scenario's pictures may survive the compare.
- Render everything by hand first so every PNG is dirty, then `publish` — the tree must come back
  to only the genuinely changed ones.
