#!/usr/bin/env python3
"""Render, browse and explore the QMapShack documentation images.

The application photographs itself: every image is the output of a recipe compiled into the
binary, so renaming a widget breaks the build rather than silently producing a stale image.

  shots.py doc [CHAPTER]         run the app so a writer can tag shots with F9
  shots.py reap [--delete]       images no page references any more
  shots.py chapter [NAME]        shoot one chapter file (default: scratch)
  shots.py build [--only GLOB]   regenerate the image set
  shots.py list                  every available shot id, with a preview
  shots.py inspect <id>          dump a widget's children and their settable properties
  shots.py explore <id>          report which controls actually change the widget

`diff` and `update` are deliberately absent: they mean nothing until the output is byte-stable,
which is the determinism stage of the plan.
"""

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Pinned so two runs on any machine match. The family is the application's own - src/fonts/ is in
# resources.qrc and a shoot or doc run registers it - because a headless run has no font database
# to fall back on: the offscreen platform uses freetype, whose font directory Qt no longer ships,
# so an unregistered family renders every glyph as an empty box.
STYLE = "Fusion"
FONT_FAMILY = "DejaVu Sans"
FONT_SIZE = "10"
# Pinned, not followed: half the writers run a dark desktop and half a light one, and the scheme
# reaches everything - the palette, the .svgt icons, every CUiTheme role. Whether the documentation
# should also carry a dark set is open; it is this constant plus a name for the file.
COLOR_SCHEME = "light"
# The language of every picture. --locale drives Qt's own catalogs and QLocale, the environment
# drives everything that asks the system instead - without both, English text comes out with
# German dialog buttons and dates. The language loop of the plan varies this.
LOCALE = "en"
SYSTEM_LOCALE = "en_US.UTF-8"
# The screen a headless run pretends to have. The offscreen platform defaults to 800x600, and
# QWidget::restoreGeometry() clamps to the screen - a chapter's stored window would silently come
# out smaller than the writer arranged it. Pinned, so it is the same on every machine.
SCREEN = {"name": "shots", "x": 0, "y": 0, "width": 1920, "height": 1200,
          "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}
# Named relative to the working directory, which `run()` points at the scratch directory. Qt splits
# the platform string on ':', so a Windows absolute path cuts `configfile=C:\...` into
# `configfile=C` plus a second argument - and a config file the plugin cannot open is a qFatal,
# which is the 3221226505 a Windows run exits with.
SCREEN_FILE = "screen.json"

REPO = Path(__file__).resolve().parents[2]
DEFAULT_OUT = REPO / "doc" / "images"
# Copied to a scratch file for every run, so a writer's session cannot drift the settings a build
# renders with. Both `doc` and `build` start from this one file.
FIXTURE_INI = REPO / "doc" / "shots" / "fixture" / "shots.ini"
# What every shot is taken against. Absolute, so these are injected here rather than committed into
# the configuration, where they would only be right on one machine. A directory that is not there
# is not injected, which is how a fixture without elevation data or POIs stays valid.
FIXTURE_DIR = REPO / "doc" / "shots" / "fixture"
MAPS_DIR = FIXTURE_DIR / "maps"
# Shared by every run, writer's session and build alike, and git-ignored.
CACHE_DIR = REPO / "doc" / "shots" / "_cache"
DEM_DIR = FIXTURE_DIR / "dem"
POI_DIR = FIXTURE_DIR / "poi"
SHOTS_DIR = REPO / "doc" / "shots"
PAGES_DIR = REPO / "doc" / "pages"


def read_ini(path):
    """Qt INI: the first path component is the section, the rest stays in the key."""
    data, section = {}, ""
    if not path.is_file():
        return data
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif "=" in line and not line.startswith((";", "#")):
            key, value = line.split("=", 1)
            data[f"{section}/{key.strip()}" if section else key.strip()] = value
    return data


def write_ini(path, data):
    sections = {}
    for key, value in data.items():
        section, _, rest = key.partition("/")
        if not rest:
            section, rest = "General", section
        sections.setdefault(section, []).append((rest, value))
    out = []
    for section in sorted(sections):
        out.append(f"[{section}]")
        out += [f"{k}={v}" for k, v in sorted(sections[section])]
        out.append("")
    path.write_text("\n".join(out))


# What --shoot-scenario is given for the pictures that name no scenario: the application as the
# base configuration starts it. Not a scenario, and nothing stores it.
BASE_SCENARIO = "-"


# compose_config(scenario=None) means the base, which is a real answer and not "not given".
NOT_GIVEN = object()


def scenario_ini(chapter, scenario):
    """A scenario keeps its own settings: doc/shots/<chapter>/<scenario>.ini."""
    return SHOTS_DIR / chapter / f"{scenario}.ini"


def compose_config(scratch, chapter=None, scenario=None):
    """The scenario's own configuration, or the base when it has none yet.

    Not one merged over the other: a scenario stores a whole configuration, so that changing the
    base cannot move a picture that has already been taken. The base is what a chapter opens on and
    what a newly recorded scenario copies.
    """
    own = scenario_ini(chapter, scenario) if chapter and scenario else None
    source = own if own and own.is_file() else FIXTURE_INI
    print(f"configuration: {source.relative_to(REPO)}"
          + ("" if source == FIXTURE_INI else f"  (own, the base is not read)"), file=sys.stderr)
    data = read_ini(source)
    # What the tool owns, not the writer: a run that saved its workspace would load the demo project
    # again next time and add a second one to it. Injected here rather than stored, so no
    # configuration file - however it was written - can get it wrong.
    data["Database/saveOnExit"] = "false"

    # One tile cache for the writer's session and the build. They used to have one each - the
    # session's beside the fixture, the build's under the output directory - so a build re-downloaded
    # every tile the writer had already fetched, and drew an empty map without a network.
    # CMainWindow reads this after main.cpp has set its own root, so this wins.
    data["Canvas/cachePath"] = str(CACHE_DIR)

    data["Canvas/mapPath"] = str(MAPS_DIR)
    if DEM_DIR.is_dir():
        data["Canvas/demPaths"] = str(DEM_DIR)
    if POI_DIR.is_dir():
        data["Canvas/poiPaths"] = str(POI_DIR)
    config = Path(scratch) / "shots.ini"
    write_ini(config, data)
    return config


def chapter_name(value):
    """A chapter is named `test`. Accept `test.md`, `test.json` or a path to either."""
    path = Path(value)
    return path.stem if path.suffix in (".md", ".json", ".ini") else path.name


def load_chapter(name):
    path = SHOTS_DIR / f"{name}.json"
    if not path.is_file():
        sys.exit(f"no such chapter: {path}")
    return path, json.loads(path.read_text())


def find_binary(explicit):
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            sys.exit(f"no such binary: {path}")
        # Absolute: a shoot run works in a scratch directory of its own, so a relative `.\qmapshack.exe`
        # would not resolve there.
        return path.resolve()

    # This checkout's own build, never an installed QMapShack: the pictures have to show the code
    # the pages are written against, and PATH says nothing about which that is.
    for candidate in (REPO / "build" / "bin" / "qmapshack", REPO / "build" / "bin" / "qmapshack.exe"):
        if candidate.is_file():
            return candidate.resolve()

    sys.exit(f"no QMapShack in {REPO / 'build' / 'bin'}; build this checkout, or pass --binary")


def screen_config(scratch):
    """The offscreen platform argument that gives the run a screen of its own.

    The file is named relative to the working directory - see SCREEN_FILE for why it cannot be the
    absolute path it sits at.
    """
    (Path(scratch) / SCREEN_FILE).write_text(json.dumps({"screens": [SCREEN]}))
    return f"offscreen:configfile={SCREEN_FILE}"


def run(binary, out_dir, task, target=None, only=None, verbose=False, chapter=None, scenario=None,
        config_of=NOT_GIVEN):
    """Run one shoot task and return the report the application wrote."""
    # Absolute, because the run works in the scratch directory: every path handed to the
    # application has to mean the same there as it does here.
    out_dir = Path(out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    # A scratch config keeps the run out of the user's real settings. The tile cache is pinned by
    # the application itself, because a run with a config that knows no maps would otherwise
    # delete the cache directory of every map the user has.
    with tempfile.TemporaryDirectory(prefix="qms-shots-") as scratch:
        config = compose_config(scratch, chapter, scenario if config_of is NOT_GIVEN else config_of)

        platform = screen_config(scratch)
        cmd = [
            str(binary),
            "-platform", platform,
            "-style", STYLE,
            "--no-splash",
            "--config", str(config),
            "--font-family", FONT_FAMILY,
            "--font-size", FONT_SIZE,
            "--color-scheme", COLOR_SCHEME,
            "--locale", LOCALE,
            "--shoot", str(out_dir),
            "--shoot-task", task,
        ]
        if target:
            cmd += ["--shoot-target", target]
        if scenario:
            cmd += ["--shoot-scenario", scenario]
        if only:
            cmd += ["--only", only]

        env = dict(os.environ)
        # QT_SCALE_FACTOR in the environment would silently double every image.
        env.pop("QT_SCALE_FACTOR", None)
        env.pop("QT_SCALE_FACTOR_ROUNDING_POLICY", None)
        env["QT_QPA_PLATFORM"] = platform
        env["TZ"] = "UTC"
        env["LC_ALL"] = env["LANG"] = SYSTEM_LOCALE

        if verbose:
            print(" ".join(cmd), file=sys.stderr)

        # In the scratch directory, so the platform argument can name the screen file relatively.
        result = subprocess.run(cmd, env=env, cwd=scratch, capture_output=not verbose, text=True)
        if result.returncode != 0:
            if not verbose and result.stderr:
                print(result.stderr, file=sys.stderr)
            sys.exit(f"{task} failed with {result.returncode} failures")

    report = {"list": "list.json", "inspect": "inspect.json", "explore": "explore.json"}.get(task)
    if report is None:
        return {}
    path = out_dir / report
    if not path.is_file():
        sys.exit(f"the run wrote no {report}")
    return json.loads(path.read_text())


def cmd_doc(args):
    """Run the application interactively so a writer can tag shots with F9."""
    args.chapter = chapter_name(args.chapter)
    binary = find_binary(args.binary)

    page = PAGES_DIR / f"{args.chapter}.md"
    if not page.is_file():
        print(f"warning: there is no {page.relative_to(REPO)} yet.")
        print("         Write the page first; its image lines are what tells you which")
        print("         pictures to take.")
    with tempfile.TemporaryDirectory(prefix="qms-doc-") as scratch:
        # The base: a session opens on it, and every scenario is recorded starting from it.
        config = compose_config(scratch, args.chapter)

        cmd = [
            str(binary),
            "-style", STYLE,
            "--no-splash",
            "--config", str(config),
            "--font-family", FONT_FAMILY,
            "--font-size", FONT_SIZE,
            "--color-scheme", COLOR_SCHEME,
            "--locale", LOCALE,
            "--doc", str(REPO),
            "--doc-chapter", args.chapter,
        ]
        env = dict(os.environ)
        # Same pins as a build, or what the writer accepts is not what the build reproduces.
        env.pop("QT_SCALE_FACTOR", None)
        env.pop("QT_SCALE_FACTOR_ROUNDING_POLICY", None)
        env["TZ"] = "UTC"
        env["LC_ALL"] = env["LANG"] = SYSTEM_LOCALE

        print(f"Documentation mode, chapter \"{args.chapter}\".")
        print("Ctrl+Shift+F9 takes a picture of what you point at. The panel does the rest.")
        subprocess.run(cmd, env=env)


def shoot_chapter(binary, out, path, name, verbose):
    """Every picture of one chapter, one process per scenario.

    Not one process per picture and not one for the lot: a scenario carries its own configuration,
    and the settings in it are read in constructors all over the application, so they can only take
    effect in a process started with them.
    """
    chapter = json.loads(path.read_text())
    shots = chapter.get("shots", [])

    # The pictures taken in no scenario are a group of their own: the base configuration, nothing
    # replayed on top. They come first, because that is the order a page reads in.
    groups = []
    if any(not s.get("scenario") for s in shots):
        groups.append(BASE_SCENARIO)
    groups += sorted({s["scenario"] for s in shots if s.get("scenario")})

    taken = 0
    for group in groups:
        scenario = None if group == BASE_SCENARIO else group
        run(binary, out, "chapter", target=str(path), scenario=group, config_of=scenario,
            verbose=verbose, chapter=name)
        for shot in shots:
            if (shot.get("scenario") or BASE_SCENARIO) == group:
                print(f"    {shot['id']:<40} {group}")
                taken += 1

    return taken, len(groups), 0


def cmd_chapter(args):
    args.name = chapter_name(args.name)
    path, chapter = load_chapter(args.name)
    if not chapter.get("shots"):
        sys.exit(f"{args.name} has no shots")

    taken, runs, _ = shoot_chapter(find_binary(args.binary), Path(args.out), path, args.name, args.verbose)
    print(f"\n{taken} images from {args.name} in {runs} run(s)")


def page_references():
    used = set()
    for page in PAGES_DIR.rglob("*.md"):
        for match in re.finditer(r"images/([\w./-]+)\.png", page.read_text(errors="replace")):
            used.add(match.group(1))
    return used


def cmd_reap(args):
    """Images no page references any more."""
    used = page_references()
    dead = []
    for path in sorted(SHOTS_DIR.glob("*.json")):
        chapter = json.loads(path.read_text())
        for shot in chapter.get("shots", []):
            if shot["id"] not in used:
                dead.append((path, chapter, shot))

    if not dead:
        print("every shot is referenced by a page")
        return

    for path, _, shot in dead:
        print(f"  {shot['id']:<32} {path.stem}")
    if not args.delete:
        print(f"\n{len(dead)} unused. Re-run with --delete to remove them.")
        return

    for path, chapter, shot in dead:
        image = Path(args.out) / f"{shot['id']}.png"
        if image.is_file():
            image.unlink()
        chapter["shots"] = [s for s in chapter["shots"] if s["id"] != shot["id"]]
        path.write_text(json.dumps(chapter, indent=4) + "\n")

    # The scenarios are left alone: one with no picture is one the writer has not used yet, and
    # throwing a recording away is a decision the panel asks about, never a side effect of a sweep.
    print(f"\nremoved {len(dead)}")


def cmd_build(args):
    """Every chapter, one after the other, one process per scenario within each.

    There is nothing else to build: a picture belongs to a chapter and a scenario is recorded in
    that chapter's file.
    """
    chapters = sorted(SHOTS_DIR.glob("*.json"))
    if not chapters:
        sys.exit(f"no chapter files in {SHOTS_DIR}")

    binary = find_binary(args.binary)
    total = 0
    for path in chapters:
        if args.only and not fnmatch.fnmatch(path.stem, args.only):
            continue
        print(f"{path.stem}")
        taken, _, _ = shoot_chapter(binary, Path(args.out), path, path.stem, args.verbose)
        total += taken
    print(f"\n{total} images in {args.out}")


def cmd_list(args):
    report = run(find_binary(args.binary), Path(args.out), "list", verbose=args.verbose)

    print("Exposed widgets — a shot of one of these needs no new C++, just an id\n")
    for exposure in report["exposures"]:
        print(f"  {exposure['id']:<28} {exposure['description']}")


def cmd_inspect(args):
    out = Path(args.out)
    report = run(find_binary(args.binary), out, "inspect", target=args.target, verbose=args.verbose)

    print(f"{report['target']} ({report['type']})\n")
    for child in report["children"]:
        props = ", ".join(f"{k}={v!r}" for k, v in child["properties"].items())
        print(f"  {child['name']:<28} {child['type']:<20} {props}")
    print(f"\npreview: {out / report['preview']}")


def cmd_explore(args):
    out = Path(args.out)
    report = run(find_binary(args.binary), out, "explore", target=args.target, verbose=args.verbose)

    print(f"{report['target']} ({report['type']})")
    print(f"baseline: {out / report['baseline']}\n")

    skipped = [c for c in report["controls"] if c["effect"].startswith("rebuilt while")]
    inert = [c for c in report["controls"] if c["effect"] == "no visible change"]
    changed = [c for c in report["controls"] if c not in skipped and c not in inert]

    for control in changed:
        print(f"  {control['name']:<28} -> {control['effect']}")
        if control.get("drove"):
            print(f"  {'':<28}    drove {control['drove']}")
        if control.get("rebuilt"):
            print(f"  {'':<28}    rebuilds {control['rebuilt']} other widgets of the panel")
        if control.get("preview"):
            print(f"  {'':<28}    {out / control['preview']}")

    if inert:
        print("\nno visible change: " + ", ".join(c["name"] for c in inert))

    if skipped:
        print("\nrebuilt by another control, not explored: " + ", ".join(c["name"] for c in skipped))

    print("\nSingle controls only — combinations, multi-step flows and states that depend on the")
    print("fixture data are not covered. This is a discovery aid, not a completeness proof.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", help="path to the qmapshack executable")
    parser.add_argument("-o", "--out", default=str(DEFAULT_OUT), help="output directory")
    parser.add_argument("-v", "--verbose", action="store_true", help="show the application's own output")
    sub = parser.add_subparsers(dest="command", required=True)

    doc = sub.add_parser("doc", help="run interactively and tag shots with F9")
    doc.add_argument("chapter", nargs="?", default="scratch")
    doc.set_defaults(func=cmd_doc)

    chapter = sub.add_parser("chapter", help="shoot one chapter file")
    chapter.add_argument("name", nargs="?", default="scratch")
    chapter.set_defaults(func=cmd_chapter)

    reap = sub.add_parser("reap", help="images no page references any more")
    reap.add_argument("--delete", action="store_true", help="actually remove them")
    reap.set_defaults(func=cmd_reap)

    build = sub.add_parser("build", help="take every chapter's pictures again")
    build.add_argument("--only", help="glob filtering the chapter names")
    build.set_defaults(func=cmd_build)

    listing = sub.add_parser("list", help="the widget classes a shot can build from nothing")
    listing.set_defaults(func=cmd_list)

    inspect = sub.add_parser("inspect", help="dump a widget's children and properties")
    inspect.add_argument("target")
    inspect.set_defaults(func=cmd_inspect)

    explore = sub.add_parser("explore", help="report which controls change the widget")
    explore.add_argument("target")
    explore.set_defaults(func=cmd_explore)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
