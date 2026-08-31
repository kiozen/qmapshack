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

#include "shoot/CShotRecorder.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRect>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QWidget>
#include <utility>

#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "gis/CGisListWks.h"
#include "gis/CGisWorkspace.h"
#include "gis/IGisItem.h"
#include "gis/prj/IGisProject.h"
#include "gis/proj_x.h"
#include "gis/trk/CGisItemTrk.h"
#include "mouse/CMouseNormal.h"
#include "mouse/IScrOpt.h"
#include "shoot/CShotChapter.h"
#include "shoot/CShotContext.h"
#include "shoot/CShotWriter.h"
#include "units/IUnit.h"

namespace {
/// A press and release further apart than this moved the map; it was no click on what was under it
constexpr int kDragSlack = 4;

/**
   @brief Is the widget inside the other one?

   Not QWidget::isAncestorOf(): that stops at the first window boundary, so a combo box would not
   own the list that pops up out of it.
 */
bool isWithin(const QWidget* ancestor, const QWidget* widget) {
  for (const QWidget* w = widget; nullptr != w; w = w->parentWidget()) {
    if (w == ancestor) {
      return true;
    }
  }
  return false;
}

/**
   @brief Are an item's options on screen?

   dynamic_cast over the canvas' own children, not findChild<IScrOpt*>(): IScrOpt has no Q_OBJECT,
   so qobject_cast would match every QWidget instead of none.
 */
bool optionsShown(CCanvas* canvas) {
  if (nullptr == canvas) {
    return false;
  }
  const QList<QWidget*>& children = canvas->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (const QWidget* child : children) {
    const IScrOpt* option = dynamic_cast<const IScrOpt*>(child);
    if (nullptr != option && option->isVisible()) {
      return true;
    }
  }
  return false;
}

/// @brief The central tab widget the canvases are pages of. There is no API for it; walk up.
QTabWidget* centralTabs(CShotContext& ctx) {
  QWidget* canvas = ctx.canvas();
  for (QWidget* w = (nullptr == canvas) ? nullptr : canvas->parentWidget(); nullptr != w; w = w->parentWidget()) {
    if (QTabWidget* tabs = qobject_cast<QTabWidget*>(w); nullptr != tabs) {
      return tabs;
    }
  }
  return nullptr;
}

/// @return The canvas the widget belongs to, or nullptr when it belongs to none
CCanvas* canvasHolding(QWidget* widget) {
  for (QWidget* w = widget; nullptr != w; w = w->parentWidget()) {
    if (CCanvas* canvas = qobject_cast<CCanvas*>(w); nullptr != canvas) {
      return canvas;
    }
  }
  return nullptr;
}

/// @brief Collect the items below this one whose details are a page of the window
void collectDetails(const QTreeWidgetItem* item, QSet<QString>& paths) {
  const IGisProject* project = dynamic_cast<const IGisProject*>(item);
  const CGisItemTrk* trk = dynamic_cast<const CGisItemTrk*>(item);
  const bool open = (nullptr != project && project->hasDlgDetails()) || (nullptr != trk && trk->hasDlgDetails());
  if (open) {
    const QString& path = CShotChapter::itemPathOf(item);
    if (!path.isEmpty()) {
      paths << path;
    }
  }
  for (int i = 0; i < item->childCount(); i++) {
    collectDetails(item->child(i), paths);
  }
}

/**
   @brief Take every details page back out of the window.

   A canvas is the only page the window keeps on its own; everything else is a details page some
   step put there, and a build takes one picture after another in one application.
 */
void closeDetailPages(CShotContext& ctx) {
  CMainWindow* main = ctx.mainWindow();
  QTabWidget* tabs = centralTabs(ctx);
  if (nullptr == main || nullptr == tabs) {
    return;
  }
  for (int i = tabs->count() - 1; i >= 0; i--) {
    QWidget* page = tabs->widget(i);
    if (nullptr == qobject_cast<CCanvas*>(page)) {
      main->closeWidgetTab(page);
    }
  }
}

/**
   @brief The name path of the one item at this point of the map.

   Exactly one, because that is the only case a click opens an item's options in: with more than
   one under the cursor CMouseNormal shows the unclutter instead, which is a different picture.
 */
QString itemPathAt(const QPoint& pixel) {
  QList<IGisItem*> items;
  CGisWorkspace::self().getItemsByPos(QPointF(pixel), items);
  return (1 == items.size()) ? CShotChapter::itemPathOf(items.first()) : QString();
}

/// @brief Open an item's screen options at a geographic point, the way a click on it does
bool clickOnMap(const QJsonObject& action, CShotContext& ctx) {
  CCanvas* canvas = ctx.canvas();
  if (nullptr == canvas) {
    qWarning() << "shoot: there is no map to click on";
    return false;
  }

  // The item's screen line is built while the canvas paints, and the tiles of an online map arrive
  // long after the draw threads have gone idle.
  canvas->waitForDrawContexts();
  CShotWriter::settleStable(canvas);

  QPointF pos(action["lon"].toDouble(), action["lat"].toDouble());
  pos *= DEG_TO_RAD;
  canvas->convertRad2Px(pos);
  const QPoint& pixel = pos.toPoint();

  const QString& path = action["item"].toString();
  IGisItem* item = dynamic_cast<IGisItem*>(CShotChapter::resolveItemPath(ctx.wksList(), path));
  if (nullptr == item) {
    // The recording names the item, but what the click means is "whatever is at this place".
    QList<IGisItem*> items;
    CGisWorkspace::self().getItemsByPos(QPointF(pixel), items);
    item = items.value(0, nullptr);
  }
  if (nullptr == item) {
    qWarning() << "shoot: nothing is on the map at" << path;
    return false;
  }

  CMouseNormal* mouse = canvas->findChild<CMouseNormal*>();
  if (nullptr == mouse || !mouse->showScreenOption(pixel, item)) {
    qWarning() << "shoot:" << path << "has nothing to show at that point";
    return false;
  }
  return true;
}
}  // namespace

CShotRecorder::CShotRecorder(CShotContext& ctx, QObject* parent) : QObject(parent), ctx(ctx) {}

CShotRecorder::~CShotRecorder() {
  if (recording) {
    qApp->removeEventFilter(this);
  }
}

void CShotRecorder::start(const QJsonArray& base) {
  actions = QJsonArray();
  for (const QJsonValue& value : base) {
    const QString& what = value.toObject()["do"].toString();
    // Both are taken whole when the recording stops, so the base's are only in the way.
    if ("layout" == what || "view" == what) {
      continue;
    }
    actions.append(value);
  }

  pressedOnCanvas = false;
  pressItem.clear();
  snapshot();
  recording = true;
  qApp->installEventFilter(this);
}

QJsonArray CShotRecorder::stop() {
  if (!recording) {
    return actions;
  }
  qApp->removeEventFilter(this);
  // Whatever the last click produced and no release has answered for yet.
  captureChanges();
  recording = false;

  // The whole state first, in the order it has to be applied: the arrangement decides how big the
  // canvas is, the canvas configuration decides what it looks at, and only then do the steps the
  // writer performed mean what they meant.
  actions.prepend(viewOf(ctx));
  actions.prepend(layoutOf(ctx));
  return actions;
}

QJsonObject CShotRecorder::viewOf(CShotContext& ctx) {
  QJsonObject action;
  action["do"] = "view";

  CCanvas* canvas = ctx.canvas();
  if (nullptr == canvas) {
    return action;
  }
  const QPointF& focus = canvas->getPosFocus() * RAD_TO_DEG;
  action["lat"] = focus.y();
  action["lon"] = focus.x();
  action["zoom"] = canvas->getZoomIndex();
  return action;
}

QJsonObject CShotRecorder::layoutOf(CShotContext& ctx) {
  QJsonObject action;
  action["do"] = "layout";

  CMainWindow* main = ctx.mainWindow();
  if (nullptr == main) {
    return action;
  }
  action["geometry"] = QString::fromLatin1(main->saveGeometry().toBase64());
  action["state"] = QString::fromLatin1(main->saveState().toBase64());
  if (const QTabWidget* tabs = centralTabs(ctx); nullptr != tabs) {
    action["tab"] = tabs->currentIndex();
  }
  return action;
}

bool CShotRecorder::isIgnored(const QWidget* widget) const {
  if (nullptr == ignored) {
    return false;
  }
  for (const QWidget* w = widget; nullptr != w; w = w->parentWidget()) {
    if (w == ignored) {
      return true;
    }
  }
  return false;
}

QString CShotRecorder::selectionNow() const {
  CGisListWks* list = ctx.wksList();
  return (nullptr == list) ? QString() : CShotChapter::itemPathOf(list->currentItem());
}

QSet<QString> CShotRecorder::expandedNow() const {
  QSet<QString> paths;
  CGisListWks* list = ctx.wksList();
  if (nullptr == list) {
    return paths;
  }
  // The projects only: anything below is a folder, which has no name path of its own, and every
  // ancestor of a selected item is expanded by the replay anyway.
  for (int i = 0; i < list->topLevelItemCount(); i++) {
    const QTreeWidgetItem* item = list->topLevelItem(i);
    const QString& path = CShotChapter::itemPathOf(item);
    if (item->isExpanded() && !path.isEmpty()) {
      paths << path;
    }
  }
  return paths;
}

QSet<QString> CShotRecorder::detailsNow() const {
  QSet<QString> paths;
  CGisListWks* list = ctx.wksList();
  if (nullptr == list) {
    return paths;
  }
  for (int i = 0; i < list->topLevelItemCount(); i++) {
    collectDetails(list->topLevelItem(i), paths);
  }
  return paths;
}

QList<CShotRecorder::input_t> CShotRecorder::inputsOf() const {
  QList<input_t> inputs;
  CMainWindow* main = ctx.mainWindow();
  if (nullptr == main) {
    return inputs;
  }

  const auto add = [&](QWidget* widget, const char* property) {
    if (isIgnored(widget)) {
      return;
    }
    const QString& address = CShotChapter::addressOf(main, widget);
    if (address.isEmpty()) {
      return;
    }
    inputs << input_t{widget, address, QString::fromLatin1(property), widget->property(property)};
  };

  // The closed list of what a shot can drive. An input the writer operates and this does not know
  // is simply not recorded, which is visible as a picture that does not come out - never as a
  // recording that silently loses a step.
  const QList<QComboBox*>& combos = main->findChildren<QComboBox*>();
  for (QComboBox* widget : combos) {
    add(widget, "currentIndex");
  }
  const QList<QTabWidget*>& tabs = main->findChildren<QTabWidget*>();
  for (QTabWidget* widget : tabs) {
    add(widget, "currentIndex");
  }
  const QList<QSpinBox*>& spins = main->findChildren<QSpinBox*>();
  for (QSpinBox* widget : spins) {
    add(widget, "value");
  }
  const QList<QDoubleSpinBox*>& doubleSpins = main->findChildren<QDoubleSpinBox*>();
  for (QDoubleSpinBox* widget : doubleSpins) {
    add(widget, "value");
  }
  const QList<QSlider*>& sliders = main->findChildren<QSlider*>();
  for (QSlider* widget : sliders) {
    add(widget, "value");
  }
  const QList<QLineEdit*>& edits = main->findChildren<QLineEdit*>();
  for (QLineEdit* widget : edits) {
    add(widget, "text");
  }
  const QList<QAbstractButton*>& buttons = main->findChildren<QAbstractButton*>();
  for (QAbstractButton* widget : buttons) {
    if (widget->isCheckable()) {
      add(widget, "checked");
    }
  }
  const QList<QGroupBox*>& groups = main->findChildren<QGroupBox*>();
  for (QGroupBox* widget : groups) {
    if (widget->isCheckable()) {
      add(widget, "checked");
    }
  }
  return inputs;
}

void CShotRecorder::snapshot() {
  lastSelection = selectionNow();
  lastExpanded = expandedNow();
  lastDetails = detailsNow();

  lastInputs.clear();
  const QList<input_t>& inputs = inputsOf();
  for (const input_t& input : inputs) {
    lastInputs.insert(input.address + "." + input.property, input.value);
  }
}

void CShotRecorder::captureChanges() {
  // The selection first: it is what most pictures are about, and a click on the map sets it too.
  const QString& selection = selectionNow();
  if (!selection.isEmpty() && selection != lastSelection) {
    QJsonObject action;
    action["do"] = "select";
    action["item"] = selection;
    actions.append(action);
  }

  const QSet<QString>& expanded = expandedNow();
  QStringList fresh;
  for (const QString& path : expanded) {
    if (!lastExpanded.contains(path)) {
      fresh << path;
    }
  }
  // A set has no order, and two recordings of the same session have to be the same file.
  fresh.sort();
  for (const QString& path : std::as_const(fresh)) {
    QJsonObject action;
    action["do"] = "expand";
    action["item"] = path;
    actions.append(action);
  }

  // Before the inputs: opening the details is what puts their controls in the window at all, so a
  // value driven into one only means something once the page is there.
  const QSet<QString>& details = detailsNow();
  QStringList opened;
  for (const QString& path : details) {
    if (!lastDetails.contains(path)) {
      opened << path;
    }
  }
  opened.sort();
  for (const QString& path : std::as_const(opened)) {
    QJsonObject action;
    action["do"] = "details";
    action["item"] = path;
    actions.append(action);
  }
  // Closed again, so the step that opened them is not part of the scenario either.
  for (const QString& path : std::as_const(lastDetails)) {
    if (!details.contains(path)) {
      forgetDetailsOn(path);
    }
  }

  const QList<input_t>& inputs = inputsOf();
  for (const input_t& input : inputs) {
    const QString& key = input.address + "." + input.property;
    // Only a widget that was there before: one that appeared since is part of what the last step
    // produced, and its value is that step's result, not something the writer drove.
    if (!lastInputs.contains(key) || lastInputs.value(key) == input.value) {
      continue;
    }
    // And only the one the writer pressed on. §6's rule is to drive inputs and let the application
    // produce the rest, and an input it flipped in answer to something else is the rest: clicking a
    // track on the map ticks the profile button inside that track's own options.
    if (pressWidget.isNull() || !isWithin(input.widget, pressWidget)) {
      continue;
    }
    QJsonObject action;
    action["do"] = "set";
    action["widget"] = input.address;
    action["property"] = input.property;
    action["value"] = QJsonValue::fromVariant(input.value);
    actions.append(action);
  }

  // The map view is not diffed: it is taken whole when the recording stops, so it comes first on
  // replay and a click's geographic point lands where the writer made it.

  // Last, so the map is where the click was made before the click is made again.
  if (pressedOnCanvas && !pressItem.isEmpty()) {
    if (optionsShown(ctx.canvas())) {
      QJsonObject action;
      action["do"] = "click";
      action["item"] = pressItem;
      action["lat"] = pressCoord.y();
      action["lon"] = pressCoord.x();
      actions.append(action);
    } else {
      // The second click on an item takes its options away again. The writer ended with none, so
      // the click that opened them is not part of the scenario either.
      forgetClickOn(pressItem);
    }
  }
  pressedOnCanvas = false;
  pressItem.clear();

  snapshot();
}

void CShotRecorder::forgetClickOn(const QString& item) {
  for (qsizetype i = actions.size() - 1; i >= 0; i--) {
    const QJsonObject& action = actions.at(i).toObject();
    if ("click" == action["do"].toString() && item == action["item"].toString()) {
      actions.removeAt(i);
      return;
    }
  }
}

void CShotRecorder::forgetDetailsOn(const QString& item) {
  for (qsizetype i = actions.size() - 1; i >= 0; i--) {
    const QJsonObject& action = actions.at(i).toObject();
    if ("details" == action["do"].toString() && item == action["item"].toString()) {
      actions.removeAt(i);
      return;
    }
  }
}

bool CShotRecorder::eventFilter(QObject* watched, QEvent* event) {
  if (!recording) {
    return QObject::eventFilter(watched, event);
  }

  const QEvent::Type type = event->type();
  if (QEvent::MouseButtonPress != type && QEvent::MouseButtonRelease != type) {
    return QObject::eventFilter(watched, event);
  }

  QWidget* widget = qobject_cast<QWidget*>(watched);
  const QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
  if (nullptr == widget || isIgnored(widget) || Qt::LeftButton != mouse->button()) {
    return QObject::eventFilter(watched, event);
  }

  CCanvas* canvas = canvasHolding(widget);
  if (QEvent::MouseButtonPress == type) {
    pressWidget = widget;
    pressedOnCanvas = false;
    pressItem.clear();
    if (nullptr != canvas) {
      // Read before the application handles the press: handling it is what moves the map and
      // changes which item is under the point.
      pressPixel = canvas->mapFromGlobal(mouse->globalPosition().toPoint());
      QPointF coord(pressPixel);
      canvas->convertPx2Rad(coord);
      pressCoord = coord * RAD_TO_DEG;
      pressItem = itemPathAt(pressPixel);
      pressedOnCanvas = true;
    }
  } else if (nullptr != canvas && pressedOnCanvas) {
    const QPoint& released = canvas->mapFromGlobal(mouse->globalPosition().toPoint());
    if ((released - pressPixel).manhattanLength() > kDragSlack) {
      // The map was dragged. Where it ended up is a view, and the view is recorded on its own.
      pressedOnCanvas = false;
      pressItem.clear();
    }
  }

  if (QEvent::MouseButtonRelease == type && !capturePending) {
    capturePending = true;
    // Queued, because the widget has not seen this release yet - and it is the state the release
    // produces that is worth recording, never the click that produced it.
    QTimer::singleShot(0, this, [this]() {
      capturePending = false;
      if (recording) {
        captureChanges();
      }
    });
  }
  return QObject::eventFilter(watched, event);
}

int CShotRecorder::replay(const QJsonArray& actions, CShotContext& ctx) {
  int failures = 0;
  // A `details` step adds a page the arrangement was recorded with, and it does not exist yet while
  // that arrangement is applied. A tab index past the last page is silently ignored, so the central
  // tab is chosen once every step has run.
  int wantedTab = NOIDX;

  for (const QJsonValue& value : actions) {
    const QJsonObject& action = value.toObject();
    const QString& what = action["do"].toString();

    if ("select" == what || "expand" == what) {
      const QString& path = action["item"].toString();
      QTreeWidgetItem* item = CShotChapter::resolveItemPath(ctx.wksList(), path);
      if (nullptr == item) {
        qWarning() << "shoot: the scenario has no item" << path;
        failures++;
        continue;
      }
      for (QTreeWidgetItem* up = item->parent(); nullptr != up; up = up->parent()) {
        up->setExpanded(true);
      }
      if ("expand" == what) {
        item->setExpanded(true);
      } else {
        ctx.wksList()->setCurrentItem(item);
      }
      continue;
    }

    if ("details" == what) {
      const QString& path = action["item"].toString();
      QTreeWidgetItem* item = CShotChapter::resolveItemPath(ctx.wksList(), path);
      // A track and a project are the two whose details are a page of the window. Every other
      // edit() runs a modal dialog, which a headless run would sit in forever - such a picture is
      // an exposure, not a scenario.
      if (IGisProject* project = dynamic_cast<IGisProject*>(item); nullptr != project) {
        project->edit();
      } else if (CGisItemTrk* trk = dynamic_cast<CGisItemTrk*>(item); nullptr != trk) {
        trk->edit();
      } else {
        qWarning() << "shoot:" << path << "has no details this scenario can open";
        failures++;
      }
      continue;
    }

    if ("set" == what) {
      const QString& address = action["widget"].toString();
      const QString& property = action["property"].toString();
      QWidget* widget = CShotChapter::resolve(ctx.mainWindow(), address);
      if (!CShotChapter::driveProperty(widget, property, action["value"].toVariant())) {
        qWarning() << "shoot: the scenario cannot set" << address << property << "to" << action["value"].toVariant();
        failures++;
      }
      continue;
    }

    if ("layout" == what) {
      CMainWindow* main = ctx.mainWindow();
      if (nullptr == main) {
        qWarning() << "shoot: there is no window to arrange";
        failures++;
        continue;
      }
      const QByteArray& geometry = QByteArray::fromBase64(action["geometry"].toString().toLatin1());
      const QByteArray& state = QByteArray::fromBase64(action["state"].toString().toLatin1());
      if (!geometry.isEmpty()) {
        main->restoreGeometry(geometry);
      }
      if (!state.isEmpty()) {
        main->restoreState(state);
      }
      if (action.contains("tab")) {
        wantedTab = action["tab"].toInt();
      }
      CShotWriter::settle(main);
      continue;
    }

    if ("view" == what) {
      CCanvas* canvas = ctx.canvas();
      if (nullptr == canvas) {
        qWarning() << "shoot: there is no map to set up";
        failures++;
        continue;
      }

      if (action.contains("zoom")) {
        // The level first: where a point sits on screen depends on it.
        canvas->zoom(action["zoom"].toInt());
        QPointF focus(action["lon"].toDouble(), action["lat"].toDouble());
        focus *= DEG_TO_RAD;
        canvas->convertRad2Px(focus);
        // moveMap() shifts the focus by a pixel delta, so ask for the one that carries the recorded
        // point to the middle of the canvas, which is where the focus is.
        canvas->moveMap(QPointF(canvas->width() / 2.0, canvas->height() / 2.0) - focus);
      } else {
        // Recorded before the zoom level was stored. zoomTo() refits a rectangle to the canvas
        // aspect and snaps it to a level, so the view comes out near the recorded one and never on
        // it: every pixel of the map lands elsewhere and a stored rectangle frames the wrong thing.
        // Built anyway and counted as a failure - a build must refuse a picture like that, and
        // documentation mode has to put the state on screen so the writer can repair the scenario.
        const QJsonArray& area = action["area"].toArray();
        if (4 == area.size()) {
          const QRectF degrees = QRectF(QPointF(area.at(0).toDouble(), area.at(1).toDouble()),
                                        QPointF(area.at(2).toDouble(), area.at(3).toDouble()))
                                     .normalized();
          canvas->zoomTo(QRectF(degrees.topLeft() * DEG_TO_RAD, degrees.bottomRight() * DEG_TO_RAD));
        }
        qWarning() << "shoot: this scenario stores a map rectangle, not a view. Select it in the "
                      "panel and press Update, then take its pictures again.";
        failures++;
      }

      canvas->waitForDrawContexts();
      continue;
    }

    if ("click" == what) {
      if (!clickOnMap(action, ctx)) {
        failures++;
      }
      continue;
    }

    qWarning() << "shoot: the scenario asks for" << what << "which this build does not know";
    failures++;
  }

  if (NOIDX != wantedTab) {
    QTabWidget* tabs = centralTabs(ctx);
    if (nullptr == tabs || wantedTab >= tabs->count()) {
      qWarning() << "shoot: the scenario was arranged around a tab" << wantedTab << "this window has not got";
      failures++;
    } else {
      tabs->setCurrentIndex(wantedTab);
      CShotWriter::settle(tabs);
    }
  }

  return failures;
}

void CShotRecorder::reset(CShotContext& ctx) {
  closeDetailPages(ctx);

  if (CCanvas* canvas = ctx.canvas(); nullptr != canvas) {
    if (CMouseNormal* mouse = canvas->findChild<CMouseNormal*>(); nullptr != mouse) {
      mouse->clearScreenOption();
    }
  }

  CGisListWks* list = ctx.wksList();
  if (nullptr == list) {
    return;
  }
  list->setCurrentItem(nullptr);
  for (int i = 0; i < list->topLevelItemCount(); i++) {
    list->topLevelItem(i)->setExpanded(false);
  }
}

void CShotRecorder::clear(const QJsonArray& actions, CShotContext& ctx) {
  CCanvas* canvas = ctx.canvas();
  if (nullptr == canvas) {
    return;
  }
  if (CMouseNormal* mouse = canvas->findChild<CMouseNormal*>(); nullptr != mouse) {
    mouse->clearScreenOption();
  }

  // A details page the scenario opened stays a page of the window, so the next picture would be
  // taken with the tab bar this one added.
  for (const QJsonValue& value : actions) {
    if ("details" == value.toObject()["do"].toString()) {
      closeDetailPages(ctx);
      break;
    }
  }

  // Opening an item's options gives a track a click focus, which changes the way it draws. The
  // next picture of the chapter has to start from the same place this one did.
  for (const QJsonValue& value : actions) {
    const QJsonObject& action = value.toObject();
    if ("click" != action["do"].toString()) {
      continue;
    }
    CGisItemTrk* trk =
        dynamic_cast<CGisItemTrk*>(CShotChapter::resolveItemPath(ctx.wksList(), action["item"].toString()));
    if (nullptr == trk) {
      continue;
    }
    trk->setMouseFocusByPoint(NOPOINT, CGisItemTrk::eFocusMouseClick, "CShotRecorder");
    trk->setMouseFocusByPoint(NOPOINT, CGisItemTrk::eFocusMouseMove, "CShotRecorder");
  }
}
