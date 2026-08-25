/**********************************************************************************************
    Copyright (C) 2014 Oliver Eichler <oliver.eichler@gmx.de>

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

#include <QNetworkProxyFactory>
#include <QtPlugin>
#include <QtWidgets>
#include <optional>

#include "CMainWindow.h"
#include "CSingleInstanceProxy.h"
#include "gis/CGisListWks.h"
#include "helpers/CSettings.h"
#include "map/CMapDraw.h"
#include "setup/CAppOpts.h"
#include "setup/IAppSetup.h"
#include "shoot/CShotDocMode.h"
#include "shoot/CShotRunner.h"
#include "theme/CQmsStyle.h"
#include "theme/CUiTheme.h"
#include "version.h"

// Link in the static ".svgt" icon engine. See svgticon/CSvgtIconEnginePlugin.h.
Q_IMPORT_PLUGIN(CSvgtIconEnginePlugin)

namespace {
/// @brief A workspace database of its own, so a run never reads or writes the user's open projects
QString scratchWorkspace(const QString& dir, const QString& name) {
  QDir(dir).mkpath(".");
  return QDir(dir).absoluteFilePath(name + "-workspace.db");
}
}  // namespace

int main(int argc, char** argv) {
  // preserve "original" argument list
  int argCnt = argc;
  char** argVal = new char*[argCnt];
  for (int i = 0; i < argCnt; i++) {
    argVal[i] = argv[i];
  }

  QApplication app(argc, argv);
  CQmsStyle::install();
  CUiTheme::installThemeRefresh();

  QCoreApplication::setApplicationName("QMapShack");
  QCoreApplication::setOrganizationName("QLandkarte");
  QCoreApplication::setOrganizationDomain("qlandkarte.org");
  QCoreApplication::setApplicationVersion(VER_STR);
  QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);
  QCoreApplication::setAttribute(Qt::AA_DontShowShortcutsInContextMenus, false);

  IAppSetup* env = IAppSetup::getPlatformInstance();
  env->processArguments();
  env->initLogHandler();

  // useful debug info
  {
    qDebug().nospace() << "Qt versions: " << "build=" << QT_VERSION_STR << ", runtime=" << qVersion();
    const QProxyStyle* qmsStyle = (QProxyStyle*)qApp->style();
    qDebug() << "Qt style:" << qmsStyle->baseStyle()->name();
    QString argList("");
    for (int i = 1; i < argCnt; i++) {
      argList += " \"" % QString::fromUtf8(argVal[i]) % "\"";
    }
    qDebug() << "Executable path:" << QFileInfo(argVal[0]).absoluteFilePath();
    qDebug().noquote().nospace() << "Argument list:" << argList;
    SETTINGS;
    qDebug() << "Configuration path:" << cfg.fileName();
  }
  delete[] argVal;

  env->initQMapShack();

  // setup default proxy
  QNetworkProxyFactory::setUseSystemConfiguration(true);

  const bool shooting = !qlOpts->shootDir.isEmpty();
  const bool documenting = !qlOpts->docDir.isEmpty();

  if (shooting) {
    // CDiskCache::cleanupRemovedMaps() deletes the cache directory of every map the current
    // configuration does not know about, so a shoot run reading a scratch config would wipe the
    // user's real tile cache. Point the cache root somewhere harmless before anything reads it.
    CMapDraw::setCacheRoot(QDir(qlOpts->shootDir).absoluteFilePath("_cache"));
    // Named after the chapter, so two chapters never share one.
    const QString& chapter = QFileInfo(qlOpts->shootTarget).completeBaseName();
    CGisListWks::setDatabasePath(
        scratchWorkspace(QDir(qlOpts->shootDir).absoluteFilePath("_cache"), chapter.isEmpty() ? "shoot" : chapter));
  }

  if (documenting) {
    // Same reason as the shoot run: documentation mode reads a scratch configuration that knows no
    // maps, and loading the map list would then prune the user's tile cache.
    CMapDraw::setCacheRoot(QDir(qlOpts->docDir).absoluteFilePath("doc/shots/_cache"));
    CGisListWks::setDatabasePath(
        scratchWorkspace(QDir(qlOpts->docDir).absoluteFilePath("doc/shots/_cache"), qlOpts->docChapter));
  }

  // CSingleInstanceProxy hands the arguments to an already running QMapShack and exits, which would
  // make a shoot or documentation run fail for a reason that has nothing to do with the
  // documentation. It has to outlive the window, so it stays a scoped object.
  std::optional<CSingleInstanceProxy> singleInstance;
  if (!shooting && !documenting) {
    singleInstance.emplace(qlOpts->arguments);
  }

  QPointer<QSplashScreen> splash = nullptr;
  if (!qlOpts->nosplash && !shooting && !documenting) {
    QPixmap pic(":/pics/splash.png");
    QPainter p(&pic);
    QFont f = p.font();
    f.setBold(true);

    p.setPen(Qt::white);
    p.setFont(f);
    p.drawText(550, 395, "V " VER_STR);

    splash = new QSplashScreen(pic);
    splash->setWindowFlags(splash->windowFlags() | Qt::WindowStaysOnTopHint);
    splash->setFixedSize(pic.size());
#ifdef Q_OS_MAC
    // remove the splash screen flag on OS-X as workaround for the reported bug
    // https://bugreports.qt.io/browse/QTBUG-49576
    splash->setWindowFlags(splash->windowFlags() & (~Qt::SplashScreen));
#endif
    splash->show();
  }

  CMainWindow w;
  w.show();

  if (shooting) {
    // Queued, never inline: the main window defers part of its own initialization by a timer and
    // the canvases start their draw threads asynchronously.
    CShotRunner* runner = new CShotRunner(CShotRunner::taskFromName(qlOpts->shootTask), QDir(qlOpts->shootDir),
                                          qlOpts->shootOnly, qlOpts->shootTarget, qlOpts->shootScenario, &w);
    runner->start();
    app.exec();
    return runner->failures();
  }

  if (documenting) {
    CShotDocMode* doc = new CShotDocMode(QDir(qlOpts->docDir), qlOpts->docChapter, &w);
    doc->start();
  }

  if (nullptr != splash) {
    QTimer::singleShot(1500, splash, [splash, &w]() {
      if (!splash.isNull()) {
        splash->finish(&w);
        delete splash;
      }
    });
  }

  return app.exec();
}
