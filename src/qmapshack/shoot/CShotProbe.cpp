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

#include "shoot/CShotProbe.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFontInfo>
#include <QJsonArray>
#include <QLocale>
#include <QMouseEvent>
#include <QScreen>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "gis/CGisListWks.h"
#include "gis/CGisWorkspace.h"
#include "shoot/CShotChapter.h"
#include "shoot/CShotWriter.h"
#include "theme/CUiTheme.h"

namespace {
/// The fixture's own items. The probe drives the two that behave differently: a track opens a page
/// of the window, a waypoint opens a modal dialog.
const QString kTrackPath = QStringLiteral("Shoot Demo/trk:Demo Track");
const QString kWaypointPath = QStringLiteral("Shoot Demo/wpt:Demo Waypoint");

/// The context menu entry a writer clicks to open an item's details, by objectName - the only
/// address that survives another system language.
const QString kEditAction = QStringLiteral("actionEditDetails");

/// How long a step may wait for the application to answer an input
constexpr int kWaitMs = 5000;

QJsonObject step(const QString& what, bool ok, const QString& detail = QString()) {
  QJsonObject entry;
  entry["step"] = what;
  entry["ok"] = ok;
  if (!detail.isEmpty()) {
    entry["detail"] = detail;
  }
  qInfo().noquote() << QString("probe: %1 %2 %3").arg(ok ? "PASS" : "FAIL", what, detail);
  return entry;
}

/**
   @brief Everything a picture has to come out the same despite.

   A writer runs Windows or Linux, a HiDPI screen or not, a dark desktop, a German system. If the
   probe passes here and the report differs there, the report says which axis moved - which is the
   only way a cross platform promise is ever kept.
 */
QJsonObject environment() {
  QJsonObject env;
  env["platform"] = QGuiApplication::platformName();
  env["style"] = QApplication::style()->objectName();
  env["locale"] = QLocale().name();
  env["paletteIsDark"] = CUiTheme::isDark();

  const QScreen* screen = QGuiApplication::primaryScreen();
  env["screenDpr"] = (nullptr == screen) ? 0.0 : screen->devicePixelRatio();
  env["screenSize"] =
      (nullptr == screen) ? QString() : QString("%1x%2").arg(screen->size().width()).arg(screen->size().height());
  env["screenLogicalDpi"] = (nullptr == screen) ? 0.0 : screen->logicalDotsPerInch();

  // What the font machinery actually resolved, not what was asked for: a headless run with no font
  // database draws every glyph as an empty box and says nothing about it.
  const QFontInfo info(QApplication::font());
  env["fontRequested"] = QApplication::font().family();
  env["fontResolved"] = info.family();
  env["fontPointSize"] = info.pointSize();
  env["fontExactMatch"] = info.exactMatch();

  if (!CMainWindow::isNull()) {
    env["windowDpr"] = CMainWindow::self().devicePixelRatioF();
    env["windowSize"] = QString("%1x%2").arg(CMainWindow::self().width()).arg(CMainWindow::self().height());
  }
  return env;
}

/// @brief The central tab widget the canvases are pages of. No API for it; walk up from a canvas.
QTabWidget* centralTabs() {
  if (CMainWindow::isNull()) {
    return nullptr;
  }
  const QList<CCanvas*>& canvases = CMainWindow::self().getCanvas();
  if (canvases.isEmpty()) {
    return nullptr;
  }
  for (QWidget* w = canvases.first()->parentWidget(); nullptr != w; w = w->parentWidget()) {
    if (QTabWidget* tabs = qobject_cast<QTabWidget*>(w); nullptr != tabs) {
      return tabs;
    }
  }
  return nullptr;
}

/// @return true once a page that is not a canvas is in the central tab widget
bool hasDetailsPage() {
  QTabWidget* tabs = centralTabs();
  if (nullptr == tabs) {
    return false;
  }
  for (int i = 0; i < tabs->count(); i++) {
    if (nullptr == qobject_cast<CCanvas*>(tabs->widget(i))) {
      return true;
    }
  }
  return false;
}

/**
   @brief Put a workspace row on screen and hand back where to click it.

   @return An invalid rect when the row cannot be reached
 */
QRect rowRect(CGisListWks* list, QTreeWidgetItem* item) {
  for (QTreeWidgetItem* up = item->parent(); nullptr != up; up = up->parent()) {
    up->setExpanded(true);
  }
  list->scrollToItem(item);
  QApplication::processEvents();
  return list->visualItemRect(item);
}

/// @return A visible QMenu anywhere in the application, or nullptr
QWidget* visibleMenu() {
  const QList<QWidget*>& tops = QApplication::topLevelWidgets();
  for (QWidget* top : tops) {
    if (nullptr != qobject_cast<QMenu*>(top) && top->isVisible()) {
      return top;
    }
  }
  return nullptr;
}

/**
   @brief Ask for the context menu the way the window system does.

   A synthesized right click is not enough: the list runs on Qt::CustomContextMenu, and
   customContextMenuRequested is raised from a QContextMenuEvent, which the platform sends
   separately and QTest::mouseClick does not produce.
 */
void requestContextMenu(QWidget* target, const QPoint& pos) {
  QContextMenuEvent event(QContextMenuEvent::Mouse, pos, target->mapToGlobal(pos));
  QApplication::sendEvent(target, &event);
}
}  // namespace

QJsonObject CShotProbe::run(const QDir& outDir) {
  QJsonObject report;
  QJsonArray steps;
  report["environment"] = environment();

  CShotWriter writer(outDir, "en");
  CGisListWks* list = &CGisWorkspace::self().getWksList();
  // A row inside a hidden docker has no place on screen, so a click would land nowhere and the
  // failure would read as "the application ignored it".
  steps.append(step("the workspace list is on screen", list->isVisible()));

  // How addressable the context menu is at all. addAction(icon, text, ...) names nothing, so an
  // action carries only its translated text - which is what a recording would have to store.
  QJsonArray actions;
  int named = 0;
  const QList<QAction*>& all = list->findChildren<QAction*>();
  for (const QAction* action : all) {
    QJsonObject entry;
    entry["objectName"] = action->objectName();
    entry["text"] = action->text();
    named += action->objectName().isEmpty() ? 0 : 1;
    actions.append(entry);
  }
  report["workspaceActions"] = actions;
  steps.append(step("the workspace menu actions carry an objectName", named == all.size(),
                    QString("%1 of %2 named").arg(named).arg(all.size())));

  // --- can a context menu be popped and photographed at all? -------------------------------------

  QTreeWidgetItem* track = CShotChapter::resolveItemPath(list, kTrackPath);
  steps.append(step("resolve the track's row", nullptr != track, kTrackPath));
  if (nullptr == track) {
    report["steps"] = steps;
    report["failures"] = 1;
    report["ok"] = false;
    return report;
  }

  const QRect rect = rowRect(list, track);
  steps.append(step("the row has a place to click", rect.isValid(),
                    QString("viewport %1x%2, row %3,%4 %5x%6")
                        .arg(list->viewport()->width())
                        .arg(list->viewport()->height())
                        .arg(rect.x())
                        .arg(rect.y())
                        .arg(rect.width())
                        .arg(rect.height())));

  list->setFocus();
  QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, rect.center());
  steps.append(step("a synthesized click selects it", list->currentItem() == track));

  int contextRequests = 0;
  QObject::connect(list, &QWidget::customContextMenuRequested, list, [&contextRequests]() { contextRequests++; });

  QJsonObject menuStep;
  QTimer::singleShot(0, [&]() {
    // activePopupWidget() is one way in and it needs window activation, which an offscreen run has
    // none of. A visible QMenu among the top level widgets is the fact underneath it.
    const bool up = QTest::qWaitFor([]() { return nullptr != visibleMenu(); }, kWaitMs);
    QWidget* popup = visibleMenu();
    if (!up || nullptr == popup) {
      menuStep = step("a context menu request pops the menu", false,
                      QString("customContextMenuRequested fired %1x, activePopupWidget %2, policy %3")
                          .arg(contextRequests)
                          .arg(nullptr == QApplication::activePopupWidget() ? "none" : "set")
                          .arg(int(list->contextMenuPolicy())));
      return;
    }
    CShotWriter::settle(popup);
    const QImage img = CShotWriter::render(popup, popup->size());
    const QString& path = writer.write(img, "probe-context-menu");
    menuStep = step("the context menu pops and renders", !path.isEmpty(),
                    QString("%1 entries, %2x%3, activePopupWidget %4")
                        .arg(popup->actions().size())
                        .arg(img.width())
                        .arg(img.height())
                        .arg(nullptr == QApplication::activePopupWidget() ? "none" : "set"));
    popup->close();
  });
  // The window system delivers a context menu event to the widget under the pointer, which is the
  // viewport; the scroll area is where the policy sits. Which of the two has to be handed the event
  // is a fact, so both are tried and the one that raises the signal is reported.
  requestContextMenu(list->viewport(), rect.center());
  const int afterViewport = contextRequests;
  if (0 == contextRequests) {
    requestContextMenu(list, list->mapFromGlobal(list->viewport()->mapToGlobal(rect.center())));
  }
  report["contextMenuTarget"] = (afterViewport > 0)     ? "viewport"
                                : (contextRequests > 0) ? "the scroll area itself"
                                                        : "neither";
  const bool menuAnswered = QTest::qWaitFor([&menuStep]() { return !menuStep.isEmpty(); }, kWaitMs + 1000);
  steps.append(menuAnswered ? menuStep : step("the context menu step ran", false, "the queued step never fired"));

  // --- the track: the application answers with a page of the main window -------------------------

  // Not edit(): the point is whether the application can be driven through its own UI. This is what
  // the writer's click on "Edit..." reaches, addressed the only way the action can be addressed.
  QAction* edit = list->findChild<QAction*>(kEditAction);
  steps.append(step("find the Edit action by objectName", nullptr != edit, kEditAction));

  if (nullptr != edit) {
    edit->trigger();
    const bool opened = QTest::qWaitFor([]() { return hasDetailsPage(); }, kWaitMs);
    steps.append(step("triggering it opens the details page", opened));

    if (opened && !CMainWindow::isNull()) {
      CShotWriter::settle(&CMainWindow::self());
      const QImage img = CShotWriter::render(&CMainWindow::self(), CMainWindow::self().size());
      const QString& path = writer.write(img, "probe-track-details");
      steps.append(step("the page renders at dpr 1", !path.isEmpty(),
                        QString("%1x%2 dpr %3").arg(img.width()).arg(img.height()).arg(img.devicePixelRatio())));
    }
  }

  // --- the waypoint: the application answers with a modal dialog ---------------------------------

  QTreeWidgetItem* waypoint = CShotChapter::resolveItemPath(list, kWaypointPath);
  steps.append(step("resolve the waypoint's row", nullptr != waypoint, kWaypointPath));

  if (nullptr != waypoint && nullptr != edit) {
    const QRect wptRect = rowRect(list, waypoint);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, wptRect.center());
    steps.append(step("a synthesized click selects the waypoint", list->currentItem() == waypoint));

    // Queued before the trigger that blocks: edit() runs exec() and does not return, so the step
    // that photographs the dialog has to be one the nested event loop delivers.
    QJsonObject modalStep;
    QTimer::singleShot(0, [&]() {
      const bool up = QTest::qWaitFor([]() { return nullptr != QApplication::activeModalWidget(); }, kWaitMs);
      QWidget* modal = QApplication::activeModalWidget();
      if (!up || nullptr == modal) {
        modalStep = step("the waypoint's dialog opens", false);
        return;
      }
      CShotWriter::settle(modal);
      const QImage img = CShotWriter::render(modal, modal->size());
      const QString& path = writer.write(img, "probe-wpt-modal");
      modalStep = step("the modal dialog is reachable and renders from inside exec()", !path.isEmpty(),
                       QString("%1, %2x%3").arg(modal->metaObject()->className()).arg(img.width()).arg(img.height()));
      modal->close();
    });
    edit->trigger();
    const bool answered = QTest::qWaitFor([&modalStep]() { return !modalStep.isEmpty(); }, kWaitMs + 1000);
    steps.append(answered ? modalStep : step("the modal dialog step ran", false, "the queued step never fired"));
    steps.append(step("the application is back out of the modal loop", nullptr == QApplication::activeModalWidget()));
  }

  int failures = 0;
  for (const QJsonValue& value : std::as_const(steps)) {
    failures += value.toObject()["ok"].toBool() ? 0 : 1;
  }
  report["steps"] = steps;
  report["failures"] = failures;
  report["ok"] = (0 == failures);
  return report;
}
