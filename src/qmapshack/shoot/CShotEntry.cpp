/**********************************************************************************************
    Copyright (C) 2026 Oliver Eichler <oliver.eichler@gmx.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

**********************************************************************************************/

#include "shoot/CShotEntry.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>

#include "CMainWindow.h"
#include "gis/CGisListWks.h"
#include "map/CMapDraw.h"
#include "setup/CAppOpts.h"
#include "shoot/CShotDocLauncher.h"
#include "shoot/CShotDocMode.h"
#include "shoot/CShotRunner.h"
#include "theme/CQmsStyle.h"

namespace {
/// @brief A workspace database of its own, so a run never reads or writes the user's open projects
QString scratchWorkspace(const QString& dir, const QString& name) {
  QDir(dir).mkpath(".");
  return QDir(dir).absoluteFilePath(name + "-workspace.db");
}
}  // namespace

bool CShotEntry::isDocRun(const CAppOpts& opts) { return !opts.shootDir.isEmpty() || !opts.docDir.isEmpty(); }

bool CShotEntry::showsMainWindow(const CAppOpts& opts) {
  // The launcher needs a CMainWindow - it is what initialises the singletons the panel's data goes
  // through - but showing it would put an application window in front of the writer that no
  // scenario describes.
  return opts.docDir.isEmpty() || !opts.docScenario.isEmpty();
}

void CShotEntry::prepare(const CAppOpts& opts) {
  // A picture must not depend on whether it was taken on screen or off it, and the platform theme
  // is the difference: the offscreen platform has none, a desktop always has one. Both runs are
  // pinned here, so the writer's session and the build that replays it answer the same.
  CQmsStyle::pinThemeIndependentHints();
  QIcon::setThemeName(QString());
  QIcon::setFallbackThemeName(QString());

  if (!opts.shootDir.isEmpty()) {
    const QString& cache = QDir(opts.shootDir).absoluteFilePath("_cache");
    CMapDraw::setCacheRoot(cache);
    // Named after the chapter, so two chapters never share one.
    const QString& chapter = QFileInfo(opts.shootTarget).completeBaseName();
    CGisListWks::setDatabasePath(scratchWorkspace(cache, chapter.isEmpty() ? "shoot" : chapter));
    return;
  }

  if (!opts.docDir.isEmpty()) {
    const QString& cache = QDir(opts.docDir).absoluteFilePath("doc/shots/_cache");
    CMapDraw::setCacheRoot(cache);
    CGisListWks::setDatabasePath(scratchWorkspace(cache, opts.docChapter));
  }
}

std::optional<int> CShotEntry::run(const CAppOpts& opts, CMainWindow& window) {
  if (!opts.shootDir.isEmpty()) {
    // Queued, never inline: the main window defers part of its own initialization by a timer and
    // the canvases start their draw threads asynchronously.
    CShotRunner* runner = new CShotRunner(CShotRunner::taskFromName(opts.shootTask), QDir(opts.shootDir),
                                          opts.shootOnly, opts.shootTarget, opts.shootScenario, &window);
    runner->start();
    qApp->exec();
    return runner->failures();
  }

  if (!opts.docDir.isEmpty()) {
    // Two processes: the one the writer works in is the one that was given a state.
    if (opts.docScenario.isEmpty()) {
      CShotDocLauncher* launcher = new CShotDocLauncher(QDir(opts.docDir), opts.docChapter, &window);
      launcher->start();
    } else {
      CShotDocMode* doc = new CShotDocMode(QDir(opts.docDir), opts.docChapter, opts.docScenario, &window);
      doc->start();
    }
  }
  return std::nullopt;
}
