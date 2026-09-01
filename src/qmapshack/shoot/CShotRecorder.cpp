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
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QRect>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QtTest/QtTest>
#include <functional>
#include <memory>
#include <utility>

#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "gis/CGisListWks.h"
#include "gis/CGisWorkspace.h"
#include "gis/CWksItemDelegate.h"
#include "gis/IGisItem.h"
#include "gis/IWksItem.h"
#include "gis/proj_x.h"
#include "gis/trk/CGisItemTrk.h"
#include "mouse/CMouseNormal.h"
#include "mouse/IScrOpt.h"
#include "plot/IPlot.h"
#include "shoot/CShotChapter.h"
#include "shoot/CShotContext.h"
#include "shoot/CShotWriter.h"
#include "units/IUnit.h"
#include "widgets/CIconGrid.h"

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

/// @return The nearest ancestor of that type, the widget itself included; nullptr when there is none
template <typename T>
T* holding(QWidget* widget) {
  for (QWidget* w = widget; nullptr != w; w = w->parentWidget()) {
    if (T* found = qobject_cast<T*>(w); nullptr != found) {
      return found;
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
// --- synthesized input ---------------------------------------------------------------------------

/// @return The widget an address names, or nullptr after warning about it
QWidget* addressedWidget(const QJsonObject& action, CShotContext& ctx) {
  const QString& address = action["widget"].toString();
  QWidget* widget = CShotChapter::resolve(ctx.mainWindow(), address);
  if (nullptr == widget) {
    qWarning() << "shoot: the scenario has no widget at" << address;
  }
  return widget;
}

/**
   @brief Where a recorded position lands in this widget now.

   A fraction of the widget, never a pixel: the same layout comes out two pixels taller on Windows
   than on Linux, and a widget that is laid out at all can be a different size on the next run.
 */
QPoint pointIn(const QWidget* widget, const QJsonArray& at) {
  if (2 != at.size()) {
    return QPoint(widget->width() / 2, widget->height() / 2);
  }
  return QPoint(qRound(at.at(0).toDouble() * widget->width()), qRound(at.at(1).toDouble() * widget->height()));
}

/**
   @brief What sits at this point of the widget.

   A position is never replayed on its own; this is what it is checked against. A row of an item
   view answers with its name path, anything else with the address of the child under the point, so
   a step that would land somewhere else fails instead of photographing the wrong state.
 */
QString hitAt(QWidget* widget, const QPoint& pos) {
  if (const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(widget); nullptr != view) {
    const QModelIndex& index = view->indexAt(pos);
    const QTreeWidget* tree = qobject_cast<const QTreeWidget*>(widget);
    if (index.isValid() && nullptr != tree) {
      return CShotChapter::itemPathOf(tree->itemFromIndex(index));
    }
    return index.isValid() ? QString::number(index.row()) : QString();
  }
  QWidget* child = widget->childAt(pos);
  return (nullptr == child) ? CShotChapter::addressOf(nullptr, widget) : CShotChapter::addressOf(widget, child);
}

/**
   @brief Does a press mean anything, or is it the writer reaching for the furniture?

   The diff had nothing to say about this release, so what the press landed on is all that is left
   to judge it by. A splitter handle, a dock title, a scroll bar, a menu bar, a label or the empty
   space under the last row of a tree acts on nothing, and a scenario carrying such a click replays
   it against whatever has grown into that place since.

   A whitelist, not a blacklist: a widget nobody has taught the recorder about records nothing,
   which is recoverable, while a wrong click is a picture of the wrong state.
 */
bool pressIsStep(QWidget* widget, const QPoint& pos) {
  for (QWidget* w = widget; nullptr != w; w = w->parentWidget()) {
    // Chrome first at every level: a scroll bar sits inside the very view it scrolls.
    if (nullptr != qobject_cast<QScrollBar*>(w) || nullptr != qobject_cast<QSplitterHandle*>(w) ||
        nullptr != qobject_cast<QMenuBar*>(w)) {
      return false;
    }
    // A header sorts or resizes; it is an item view too, but it has no index to hit.
    if (nullptr != qobject_cast<QHeaderView*>(w)) {
      return true;
    }
    if (const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(w); nullptr != view) {
      const QPoint& inViewport = view->viewport()->mapFromGlobal(widget->mapToGlobal(pos));
      return view->indexAt(inViewport).isValid();
    }
    if (nullptr != qobject_cast<QAbstractButton*>(w) || nullptr != qobject_cast<QTabBar*>(w) ||
        nullptr != qobject_cast<QComboBox*>(w)) {
      return true;
    }
  }
  return false;
}

/// @brief The viewport a position is delivered to, which for a scroll area is not the widget itself
QWidget* inputTarget(QWidget* widget) {
  QAbstractScrollArea* area = qobject_cast<QAbstractScrollArea*>(widget);
  return (nullptr == area) ? widget : area->viewport();
}

/**
   @brief Act on a workspace row's tool button.

   Through the delegate, not through a point: the buttons are painted into the row and carry no
   widget, so the only thing that still means the same button after a relayout is its name.

   @param rectRow  where the row sits, only to place the popup the setup button opens
   @return true when the row and the button were both found and the button acted
 */
bool pressRowButton(CShotContext& ctx, const QString& path, const QString& name, const QRect& rectRow) {
  CGisListWks* list = ctx.wksList();
  CWksItemDelegate* delegate = (nullptr == list) ? nullptr : qobject_cast<CWksItemDelegate*>(list->itemDelegate());
  IWksItem* item = dynamic_cast<IWksItem*>(CShotChapter::resolveItemPath(list, path));
  if (nullptr == delegate || nullptr == item) {
    qWarning() << "shoot: the scenario has no item" << path << "with buttons";
    return false;
  }

  const CWksItemDelegate::button_e button = CWksItemDelegate::buttonByName(name);
  if (CWksItemDelegate::button_e::eNone == button) {
    qWarning() << "shoot: the scenario asks for a row button called" << name << "which this build does not know";
    return false;
  }

  // The row is made current first: the writer clicked the button on a row that had the focus, and
  // that is what the active-project button asks about.
  list->setCurrentItem(item);
  if (!delegate->pressButton(button, *item, true, rectRow)) {
    qWarning() << "shoot: the button" << name << "of" << path << "does nothing in this state";
    return false;
  }
  return true;
}

/**
   @brief Where a step lands on a surface that names what it holds.

   @return NOPOINT when the surface no longer holds it, which is a counted failure and not a click
           somewhere else
 */
QPoint pointOnNamedSurface(const QJsonObject& action, QWidget* widget) {
  if (action.contains("x")) {
    IPlot* plot = qobject_cast<IPlot*>(widget);
    if (nullptr == plot) {
      qWarning() << "shoot:" << action["widget"].toString() << "is a" << widget->metaObject()->className()
                 << "and not a plot";
      return NOPOINT;
    }
    const QPoint& pos = plot->pointOfXValue(action["x"].toDouble());
    if (NOPOINT == pos) {
      qWarning() << "shoot: the plot does not reach" << action["x"].toDouble() << "on its x axis";
    }
    return pos;
  }

  CIconGrid* grid = qobject_cast<CIconGrid*>(widget);
  if (nullptr == grid) {
    qWarning() << "shoot:" << action["widget"].toString() << "is a" << widget->metaObject()->className()
               << "and not an icon grid";
    return NOPOINT;
  }
  const QString& name = action["icon"].toString();
  const QRect& rect = grid->rectOfIcon(name);
  if (!rect.isValid()) {
    qWarning() << "shoot: the icon grid does not show" << name;
    return NOPOINT;
  }
  return rect.center();
}

/// @return Where a workspace row sits, after putting it on screen; invalid when it cannot be found
QRect rowRect(CShotContext& ctx, const QString& path) {
  CGisListWks* list = ctx.wksList();
  QTreeWidgetItem* item = CShotChapter::resolveItemPath(list, path);
  if (nullptr == list || nullptr == item) {
    return {};
  }
  for (QTreeWidgetItem* up = item->parent(); nullptr != up; up = up->parent()) {
    up->setExpanded(true);
  }
  list->scrollToItem(item);
  CShotWriter::settle(list);
  return list->visualItemRect(item);
}

/**
   @brief Click, and double click if asked.

   The plain click before a double click is not politeness: QAbstractItemView drops a double click
   whose index does not match the one a press recorded, so a bare double click emits nothing at all
   and the coordinates look wrong when they are right.
 */
void clickOn(QWidget* target, const QPoint& pos, bool twice) {
  QTest::mouseClick(target, Qt::LeftButton, {}, pos);
  if (twice) {
    QTest::mouseDClick(target, Qt::LeftButton, {}, pos);
  }
}

/**
   @brief Ask for a context menu the way the window system does.

   A synthesized right click is not enough: a list on Qt::CustomContextMenu raises
   customContextMenuRequested from a QContextMenuEvent, which the platform sends separately, and it
   is delivered to the widget under the pointer - the viewport, not the scroll area.
 */
void requestContextMenu(QWidget* target, const QPoint& pos) {
  QContextMenuEvent event(QContextMenuEvent::Mouse, pos, target->mapToGlobal(pos));
  QApplication::sendEvent(target, &event);
}

/**
   @brief Perform one step of a scenario.

   @param wantedTab  out: the central tab the arrangement asks for, applied once every step has run
   @return The number of failures
 */
int applyAction(const QJsonObject& action, CShotContext& ctx, int& wantedTab) {
  int failures = 0;
  const QString& what = action["do"].toString();

  if ("select" == what || "expand" == what) {
    const QString& path = action["item"].toString();
    QTreeWidgetItem* item = CShotChapter::resolveItemPath(ctx.wksList(), path);
    if (nullptr == item) {
      qWarning() << "shoot: the scenario has no item" << path;
      failures++;
      return failures;
    }
    for (QTreeWidgetItem* up = item->parent(); nullptr != up; up = up->parent()) {
      up->setExpanded(true);
    }
    if ("expand" == what) {
      item->setExpanded(true);
    } else {
      ctx.wksList()->setCurrentItem(item);
    }
    return failures;
  }

  if ("set" == what) {
    const QString& address = action["widget"].toString();
    const QString& property = action["property"].toString();
    QWidget* widget = CShotChapter::resolve(ctx.mainWindow(), address);
    if (!CShotChapter::driveProperty(widget, property, action["value"].toVariant())) {
      qWarning() << "shoot: the scenario cannot set" << address << property << "to" << action["value"].toVariant();
      failures++;
    }
    return failures;
  }

  if ("layout" == what) {
    CMainWindow* main = ctx.mainWindow();
    if (nullptr == main) {
      qWarning() << "shoot: there is no window to arrange";
      failures++;
      return failures;
    }
    if (action.contains("geometry")) {
      // Recorded before the window size became the shot's alone. Ignored, not applied: it is the
      // record that used to win over the shot's `size` and put a rectangle outside its picture.
      qWarning() << "shoot: this scenario still carries a window geometry, which is no longer read."
                 << "Record it again, or take the geometry out of the file.";
    }
    // After the caller has put the window at the shot's size, never before: saveState() stores dock
    // extents in pixels and Qt distributes them across whatever width the window has now.
    const QByteArray& state = QByteArray::fromBase64(action["state"].toString().toLatin1());
    if (!state.isEmpty()) {
      main->restoreState(state);
    }
    if (action.contains("tab")) {
      wantedTab = action["tab"].toInt();
    }
    CShotWriter::settle(main);
    return failures;
  }

  if ("view" == what) {
    CCanvas* canvas = ctx.canvas();
    if (nullptr == canvas) {
      qWarning() << "shoot: there is no map to set up";
      failures++;
      return failures;
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
    return failures;
  }

  if ("click" == what && action.contains("lat")) {
    if (!clickOnMap(action, ctx)) {
      failures++;
    }
    return failures;
  }

  if ("click" == what || "dclick" == what) {
    const bool twice = ("dclick" == what);

    // A workspace row is named, so it needs no coordinates at all.
    const QString& path = action["item"].toString();
    if (!path.isEmpty()) {
      const QRect& rect = rowRect(ctx, path);
      if (!rect.isValid()) {
        qWarning() << "shoot: the scenario has no item" << path;
        return failures + 1;
      }
      // A row's tool button is painted, not a widget, so there is no point to click that would
      // mean the same button after a relayout. The delegate acts on it by name instead.
      const QString& button = action["button"].toString();
      if (!button.isEmpty()) {
        return pressRowButton(ctx, path, button, rect) ? failures : failures + 1;
      }
      clickOn(ctx.wksList()->viewport(), rect.center(), twice);
      return failures;
    }

    QWidget* widget = addressedWidget(action, ctx);
    if (nullptr == widget) {
      return failures + 1;
    }

    // A plot and an icon grid carry what they hit, not where: an axis value and an icon name.
    if (action.contains("x") || action.contains("icon")) {
      const QPoint& named = pointOnNamedSurface(action, widget);
      if (NOPOINT == named) {
        return failures + 1;
      }
      clickOn(widget, named, twice);
      return failures;
    }

    QWidget* target = inputTarget(widget);
    const QPoint& pos = pointIn(target, action["at"].toArray());

    // A position is never acted on unheard: what it hit when it was recorded has to still be there,
    // or the picture would show another state and say nothing about it.
    const QString& wanted = action["hit"].toString();
    if (!wanted.isEmpty() && hitAt(target, pos) != wanted) {
      qWarning() << "shoot: the scenario points at" << wanted << "and finds" << hitAt(target, pos);
      return failures + 1;
    }
    clickOn(target, pos, twice);
    return failures;
  }

  if ("trigger" == what) {
    const QString& name = action["action"].toString();
    QObject* owner = action.contains("widget") ? addressedWidget(action, ctx) : ctx.mainWindow();
    QAction* target = (nullptr == owner) ? nullptr : owner->findChild<QAction*>(name);
    if (nullptr == target) {
      qWarning() << "shoot: the scenario has no action called" << name;
      return failures + 1;
    }
    if (!target->isEnabled()) {
      qWarning() << "shoot:" << name << "is disabled in this state";
      return failures + 1;
    }
    target->trigger();
    return failures;
  }

  if ("menu" == what) {
    QWidget* widget = addressedWidget(action, ctx);
    if (nullptr == widget) {
      return failures + 1;
    }
    QWidget* target = inputTarget(widget);
    const QString& path = action["item"].toString();
    QPoint pos;
    if (path.isEmpty()) {
      pos = pointIn(target, action["at"].toArray());
    } else {
      const QRect& rect = rowRect(ctx, path);
      if (!rect.isValid()) {
        qWarning() << "shoot: the scenario has no item" << path;
        return failures + 1;
      }
      QTest::mouseClick(target, Qt::LeftButton, {}, rect.center());
      pos = rect.center();
    }
    requestContextMenu(target, pos);
    return failures;
  }

  if ("key" == what) {
    QWidget* widget = addressedWidget(action, ctx);
    if (nullptr == widget) {
      return failures + 1;
    }
    QTest::keyClicks(widget, action["keys"].toString());
    return failures;
  }

  qWarning() << "shoot: the scenario asks for" << what << "which this build does not know";
  failures++;

  return failures;
}

/// @brief Choose the central tab the arrangement asked for, now that every page it counted exists
int applyWantedTab(CShotContext& ctx, int wantedTab) {
  int failures = 0;
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

/// @brief Close whatever runs an event loop on top of the application, so a nested one can return
void closeBlockingWindow() {
  if (QWidget* popup = QApplication::activePopupWidget(); nullptr != popup) {
    popup->close();
  }
  if (QWidget* modal = QApplication::activeModalWidget(); nullptr != modal) {
    modal->close();
  }
}

/// A scenario that opens something no later step closes would leave replay() below that dialog's
/// own event loop. The deadline closes it, counts a failure and returns instead of hanging a build.
constexpr int kReplayTimeoutMs = 60000;

/// @brief What one replay carries, kept alive for any step still queued when it gives up
struct replay_t {
  QJsonArray actions;
  CShotContext* ctx = nullptr;
  qsizetype next = 0;
  int failures = 0;
  int wantedTab = NOIDX;
  bool abandoned = false;
  /// How many steps are on the stack. More than none means one of them has not returned yet.
  int running = 0;
  std::function<void()> whenReady;
  std::function<void()> pump;
  QEventLoop loop;
};
}  // namespace

CShotRecorder::CShotRecorder(CShotContext& ctx, QObject* parent) : QObject(parent), ctx(ctx) {}

CShotRecorder::~CShotRecorder() {
  if (recording) {
    qApp->removeEventFilter(this);
  }
}

void CShotRecorder::watchRowButtons() {
  CGisListWks* list = ctx.wksList();
  CWksItemDelegate* delegate = (nullptr == list) ? nullptr : qobject_cast<CWksItemDelegate*>(list->itemDelegate());
  if (nullptr == delegate) {
    qWarning() << "shoot: the workspace has no delegate, so its row buttons cannot be recorded";
    return;
  }
  // A row's tool buttons are painted, not widgets, so no press reaches the filter as anything but
  // a point in the viewport. The delegate is the only thing that knows which button that was.
  connect(
      delegate, &CWksItemDelegate::sigButtonPressed, this,
      [this, list](const QModelIndex& index, CWksItemDelegate::button_e button) {
        if (!recording) {
          return;
        }
        pressButtonItem = CShotChapter::itemPathOf(list->itemFromIndex(index));
        pressButtonName = CWksItemDelegate::buttonName(button);
      },
      Qt::UniqueConnection);
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
  pressButtonName.clear();
  pressButtonItem.clear();
  snapshot();
  recording = true;
  watchRowButtons();
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
  // The arrangement only, never saveGeometry(): the window's size is the shot's `size`, and two
  // records of one number drift apart - a scenario recorded at one size and a rectangle dragged at
  // another framed something else entirely.
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

  lastInputs.clear();
  const QList<input_t>& inputs = inputsOf();
  for (const input_t& input : inputs) {
    lastInputs.insert(input.address + "." + input.property, input.value);
  }
}

void CShotRecorder::captureChanges() {
  const qsizetype before = actions.size();

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

  // After the selection the same click produced, because a row is selected before its button acts.
  if (!pressButtonName.isEmpty() && !pressButtonItem.isEmpty()) {
    QJsonObject action;
    action["do"] = "click";
    action["item"] = pressButtonItem;
    action["button"] = pressButtonName;
    actions.append(action);
  }
  pressButtonName.clear();
  pressButtonItem.clear();

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

  // The diff had nothing to say about this release, so the press itself is the step. A button that
  // opens a dialog leaves no state behind and used to be unrecordable.
  if (actions.size() == before) {
    recordPress(pressWasDouble);
  }
  pressWasDouble = false;

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

bool CShotRecorder::recordNamedSurface(QWidget* widget) {
  CMainWindow* main = ctx.mainWindow();

  if (IPlot* plot = holding<IPlot>(widget); nullptr != plot) {
    const QPoint& pos = plot->mapFromGlobal(widget->mapToGlobal(pressPos));
    const qreal value = plot->xValueAt(pos);
    if (NOFLOAT == value) {
      return false;
    }
    const QString& address = CShotChapter::addressOf(main, plot);
    if (address.isEmpty()) {
      return false;
    }
    QJsonObject action;
    action["do"] = "click";
    action["widget"] = address;
    // The x axis' own value - metres along the track, or seconds into it. The graph area moves
    // with the axis labels, so a fraction of the widget points somewhere else at another size.
    action["x"] = value;
    actions.append(action);
    return true;
  }

  if (CIconGrid* grid = holding<CIconGrid>(widget); nullptr != grid) {
    const QPoint& pos = grid->mapFromGlobal(widget->mapToGlobal(pressPos));
    const QString& name = grid->iconAt(pos);
    if (name.isEmpty()) {
      return false;
    }
    const QString& address = CShotChapter::addressOf(main, grid);
    if (address.isEmpty()) {
      return false;
    }
    QJsonObject action;
    action["do"] = "click";
    action["widget"] = address;
    // The grid reflows to the width it is given, so the tile the writer hit is only ever the icon.
    action["icon"] = name;
    actions.append(action);
    return true;
  }

  return false;
}

void CShotRecorder::recordPress(bool twice) {
  QWidget* widget = pressWidget;
  if (nullptr == widget || isIgnored(widget)) {
    return;
  }
  // The canvas has a vocabulary of its own - a geographic point - and the workspace list is what
  // the selection diff is about. Neither wants a position.
  if (nullptr != canvasHolding(widget) || nullptr != qobject_cast<QMenu*>(widget)) {
    return;
  }
  CGisListWks* list = ctx.wksList();
  if (nullptr != list && (widget == list || widget == list->viewport())) {
    return;
  }
  // A surface that names what it holds is recorded by that name; only what has none falls through
  // to a position, and only then does it have to be a widget that acts on a click.
  if (recordNamedSurface(widget)) {
    return;
  }
  if (!pressIsStep(widget, pressPos)) {
    qDebug() << "shoot: nothing here acts on a click, so it is not recorded:" << widget->metaObject()->className();
    return;
  }

  CMainWindow* main = ctx.mainWindow();
  const QString& address = CShotChapter::addressOf(main, widget);
  if (address.isEmpty()) {
    qWarning() << "shoot: nothing here can be named, so the click is not recorded:"
               << widget->metaObject()->className();
    return;
  }

  QJsonObject action;
  action["do"] = twice ? "dclick" : "click";
  action["widget"] = address;
  action["at"] =
      QJsonArray({double(pressPos.x()) / qMax(1, widget->width()), double(pressPos.y()) / qMax(1, widget->height())});
  const QString& hit = hitAt(widget, pressPos);
  if (!hit.isEmpty()) {
    action["hit"] = hit;
  }
  actions.append(action);
}

void CShotRecorder::recordMenuAction(QWidget* menu, const QPoint& pos) {
  QMenu* popup = qobject_cast<QMenu*>(menu);
  const QAction* picked = (nullptr == popup) ? nullptr : popup->actionAt(pos);
  if (nullptr == picked || picked->isSeparator()) {
    return;
  }
  if (picked->objectName().isEmpty()) {
    // The text is translated, so it is no address. Naming the action is the fix, in the class that
    // builds the menu.
    qWarning() << "shoot: the menu entry" << picked->text() << "has no objectName and cannot be recorded";
    return;
  }

  QJsonObject action;
  action["do"] = "trigger";
  action["action"] = picked->objectName();
  actions.append(action);
}

void CShotRecorder::recordContextMenu(QWidget* widget, const QPoint& pos) {
  CMainWindow* main = ctx.mainWindow();
  CGisListWks* list = ctx.wksList();
  // The viewport is what the event is delivered to; the address belongs to the widget that owns it.
  QWidget* owner = widget;
  if (nullptr != list && widget == list->viewport()) {
    owner = list;
  }

  const QString& address = CShotChapter::addressOf(main, owner);
  if (address.isEmpty()) {
    return;
  }

  QJsonObject action;
  action["do"] = "menu";
  action["widget"] = address;
  if (owner == list) {
    const QTreeWidgetItem* item = list->itemAt(pos);
    const QString& path = CShotChapter::itemPathOf(item);
    if (!path.isEmpty()) {
      action["item"] = path;
    }
  } else {
    action["at"] = QJsonArray({double(pos.x()) / qMax(1, owner->width()), double(pos.y()) / qMax(1, owner->height())});
  }
  actions.append(action);
}

bool CShotRecorder::eventFilter(QObject* watched, QEvent* event) {
  if (!recording) {
    return QObject::eventFilter(watched, event);
  }

  const QEvent::Type type = event->type();

  // A context menu is asked for by an event of its own, never by the right button alone.
  if (QEvent::ContextMenu == type) {
    QWidget* widget = qobject_cast<QWidget*>(watched);
    const QContextMenuEvent* request = static_cast<QContextMenuEvent*>(event);
    if (nullptr != widget && !isIgnored(widget)) {
      recordContextMenu(widget, request->pos());
    }
    return QObject::eventFilter(watched, event);
  }

  if (QEvent::MouseButtonPress != type && QEvent::MouseButtonRelease != type && QEvent::MouseButtonDblClick != type) {
    return QObject::eventFilter(watched, event);
  }

  QWidget* widget = qobject_cast<QWidget*>(watched);
  const QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
  if (nullptr == widget || isIgnored(widget) || Qt::LeftButton != mouse->button()) {
    return QObject::eventFilter(watched, event);
  }

  // What the writer picked in a menu is an action, and it has to be read before the menu closes.
  if (QEvent::MouseButtonRelease == type && nullptr != qobject_cast<QMenu*>(widget)) {
    recordMenuAction(widget, mouse->position().toPoint());
    return QObject::eventFilter(watched, event);
  }

  if (QEvent::MouseButtonDblClick == type) {
    pressWasDouble = true;
    return QObject::eventFilter(watched, event);
  }

  CCanvas* canvas = canvasHolding(widget);
  if (QEvent::MouseButtonPress == type) {
    pressWidget = widget;
    pressPos = mouse->position().toPoint();
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

int CShotRecorder::replay(const QJsonArray& actions, CShotContext& ctx, const std::function<void()>& whenReady) {
  // Steps are scheduled, never called one after another. A step may enter a modal dialog, a popup
  // menu or a nested progress loop, and the event loop running inside it delivers the step after
  // it - so the rest of the scenario still happens. A `for` loop sits inside exec() until somebody
  // closes what it opened, which is why every such window used to need an exposure of its own.
  const std::shared_ptr<replay_t> state = std::make_shared<replay_t>();
  state->actions = actions;
  state->ctx = &ctx;
  state->whenReady = whenReady;

  // Weak, or the state would own the lambda that owns the state.
  const std::weak_ptr<replay_t> weak = state;
  state->pump = [weak]() {
    const std::shared_ptr<replay_t> s = weak.lock();
    if (nullptr == s || s->abandoned) {
      return;
    }
    if (s->running > 0 && nullptr == QApplication::activeModalWidget() &&
        nullptr == QApplication::activePopupWidget()) {
      // A step has not returned and nothing with an event loop of its own is up, so it is spinning
      // the loop itself - CShotWriter::settle(), waitForDrawContexts(). Let it finish; going on
      // here would run the next step in the middle of this one.
      QTimer::singleShot(1, qApp, s->pump);
      return;
    }

    if (s->next < s->actions.size()) {
      const QJsonObject& action = s->actions.at(s->next++).toObject();
      // Queued before the step runs, not after: a step that opens a modal dialog or a popup menu
      // does not return until it is closed, and it is the loop running inside it that delivers
      // this. Queue it afterwards and the first such step stalls the whole scenario.
      QTimer::singleShot(0, qApp, s->pump);
      s->running++;
      s->failures += applyAction(action, *s->ctx, s->wantedTab);
      s->running--;
      return;
    }
    s->failures += applyWantedTab(*s->ctx, s->wantedTab);
    if (s->whenReady) {
      s->whenReady();
    }
    // The picture is taken; whatever a step opened has to go before this loop, which sits below its
    // event loop, can return at all.
    closeBlockingWindow();
    s->loop.quit();
  };

  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &state->loop, [state]() {
    qWarning() << "shoot: the scenario did not finish - a step opened something no later step closed";
    state->failures++;
    closeBlockingWindow();
    state->loop.quit();
  });
  deadline.start(kReplayTimeoutMs);

  QTimer::singleShot(0, qApp, state->pump);
  state->loop.exec();

  // A step queued before the deadline gave up must find nothing left to do.
  state->abandoned = true;
  return state->failures;
}

void CShotRecorder::reset(CShotContext& ctx) {
  if (CCanvas* canvas = ctx.canvas(); nullptr != canvas) {
    // A scenario can leave the canvas in range or edit mode, and nothing else takes those down.
    // Replacing the delegate destroys it, and its destructor is what puts the track back to normal.
    canvas->resetMouse();
    if (CMouseNormal* mouse = canvas->findChild<CMouseNormal*>(); nullptr != mouse) {
      mouse->clearScreenOption();
    }
  }

  // setCurrentItem() below emits nothing, so the hint the workspace put on the canvas when an item
  // was selected outlives it.
  CGisWorkspace::self().slotWksItemSelectionReset();

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

  // Selecting a workspace item puts a hint on every canvas telling the user to click the map. It is
  // the selection's leftover, not the next picture's.
  CGisWorkspace::self().slotWksItemSelectionReset();

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
