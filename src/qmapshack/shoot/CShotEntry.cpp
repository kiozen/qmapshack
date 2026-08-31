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

#include "CMainWindow.h"
#include "map/CMapDraw.h"
#include "gis/CGisListWks.h"
#include "setup/CAppOpts.h"
#include "shoot/CShotDocMode.h"
#include "shoot/CShotRunner.h"

namespace {
/// @brief A workspace database of its own, so a run never reads or writes the user's open projects
QString scratchWorkspace(const QString& dir, const QString& name) {
  QDir(dir).mkpath(".");
  return QDir(dir).absoluteFilePath(name + "-workspace.db");
}
}  // namespace

bool CShotEntry::isDocRun(const CAppOpts& opts) { return !opts.shootDir.isEmpty() || !opts.docDir.isEmpty(); }

void CShotEntry::prepare(const CAppOpts& opts) {
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
    CShotDocMode* doc = new CShotDocMode(QDir(opts.docDir), opts.docChapter, &window);
    doc->start();
  }
  return std::nullopt;
}
