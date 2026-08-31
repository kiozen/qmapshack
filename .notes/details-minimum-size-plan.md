# The track details page pins the main window to 1648x717

Filed as https://github.com/Maproom/qmapshack/issues/1234 (QMS-1234).

Not a documentation problem. While the details page is open the main window cannot be made smaller,
because Qt enforces `minimumSizeHint()` on a window with a layout. On a 1366x768 laptop that forces
the window wider than the screen. Wants its own branch.

## Measured

Track details open, one track, offscreen run at dpr 1:

| widget | minimumSizeHint |
|---|---|
| `CMainWindow` | **1648 x 717** |
| `CDetailsTrk` | 1337 x 599 |
| `splitter1` | 1331 x 593 |
| `splitter1/framePlot` | 33 x **348** |
| `splitter1/splitter2` | **1331** x 241 |
| `splitter2/frameInfo` -> `labelInfo` | 457 / **449** x 144 |
| `splitter2/tabWidget` -> stacked -> `tabFilter` | 756 / 752 / **752** x 70 |

`main ~= splitter2_min + 317` horizontally, `+118` vertically - the difference is the workspace
docker and the window's own chrome.

## Where it comes from

**Width: `QLabel`s without `wordWrap`.** A label that cannot wrap reports its whole unwrapped string
as a minimum, and it propagates filter widget -> `treeFilter` item -> `tabFilter` -> the stacked
widget (which takes the *maximum* over its pages) -> `tabWidget` -> `splitter2` -> the window.

- `gis/trk/filter/*.ui`: 26 labels over 25 characters, **none** with `wordWrap`.
  `IFilterDouglasPeuker/label` is 529 px on its own, `IFilterReplaceElevation/label` 514,
  `IFilterMedian/label` 482.
- `IDetailsTrk.ui`'s `labelInfo` is 449 px. Its text is the track summary, so it is **translated**:
  the floor, and therefore every picture's size, moves with the language.

**Height: three plots at a fixed minimum.** `CDetailsTrk.cpp`, `plot->setMinimumSize(QSize(0, 100))`
for each of `plot1`/`plot2`/`plot3` - 300 of `framePlot`'s 348.

No explicit `minimumSize` in `IDetailsTrk.ui` is responsible; the only ones there are
`widgetColorLabel` 100x0 and two `QSvgWidget` maxima.

## Options, with what each buys

1. `wordWrap` on the filter labels. `tabFilter` stops dominating the stack - the next page is
   `tab_3` at 404 - so `splitter2` drops 1331 -> ~871 and the window 1648 -> **~1182**.
2. `wordWrap` on `labelInfo`, or an explicit modest `minimumWidth` on `frameInfo` and let it elide.
   `splitter2` -> ~614, window -> **~931**. Also removes the language dependence.
3. Lower the plots' minimum height 100 -> ~60. `framePlot` 348 -> 228, window 717 -> **~597**.
4. Instead of 1: cap the filter widgets' minimum width and let `treeFilter` scroll horizontally,
   leaving the labels alone.

1 to 3 are user-visible: wrapping a label makes its row taller, and the filter rows are already
dense. That is a design call, not a mechanical fix.

## Why it surfaced

The screenshot framework made it measurable. A writer who opens the details page has the window
forced to >= 1648 wide and it stays there, so a scenario recorded afterwards carries that width -
which is how `test/track-scropt` came to have a rectangle dragged on a 1666 px window while its own
scenario had been recorded at 1119. That half is fixed: the window size now has one record, the
shot's `size`. See `shot-input-replay-plan.md`.
