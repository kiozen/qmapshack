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

#include "shoot/CShotChapter.h"

#include <QDebug>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRect>
#include <QTabWidget>
#include <QWidget>

#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "gis/CGisListWks.h"
#include "gis/IWksItem.h"
#include "gis/prj/IGisProject.h"
#include "shoot/CShotContext.h"
#include "shoot/CShotRecorder.h"
#include "shoot/CShotRegistry.h"
#include "shoot/CShotWriter.h"

namespace {
/// @brief Direct children of `parent` whose class name matches, in construction order
QList<QWidget*> childrenOfClass(const QWidget* parent, const QString& className) {
  QList<QWidget*> matches;
  const QList<QWidget*>& children = parent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (QWidget* child : children) {
    if (QString::fromLatin1(child->metaObject()->className()) == className) {
      matches << child;
    }
  }
  return matches;
}

/// @brief Make the map view the current central tab. There is no API for it; the canvas is a page
///        of the main window's tab widget, so walk up to it.
void showCanvasTab(CMainWindow* main) {
  const QList<CCanvas*>& canvases = main->getCanvas();
  if (canvases.isEmpty()) {
    return;
  }
  QWidget* canvas = canvases.first();
  for (QWidget* w = canvas->parentWidget(); nullptr != w; w = w->parentWidget()) {
    if (QTabWidget* tabs = qobject_cast<QTabWidget*>(w); nullptr != tabs) {
      tabs->setCurrentWidget(canvas);
      return;
    }
  }
}

/// @brief Show exactly the named dockers and hide every other one
void showDocks(CMainWindow* main, const QStringList& wanted) {
  const QList<QDockWidget*>& docks = main->findChildren<QDockWidget*>();
  for (QDockWidget* dock : docks) {
    dock->setVisible(wanted.contains(dock->objectName()));
  }
}

/// @brief `IWksItem::eTypeTrk` -> `trk`. QTreeWidgetItem is no QObject, so the type integer is it.
QString typeTag(const QTreeWidgetItem* item) {
  switch (item->type()) {
    case IWksItem::eTypeWpt:
      return "wpt";
    case IWksItem::eTypeTrk:
      return "trk";
    case IWksItem::eTypeRte:
      return "rte";
    case IWksItem::eTypeOvl:
      return "area";
    default:
      return {};
  }
}

QJsonObject readChapter(const QString& file) {
  QFile in(file);
  if (!in.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(in.readAll()).object();
}

bool writeChapter(const QString& file, const QJsonObject& chapter) {
  QFile out(file);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning() << "doc: cannot write" << file;
    return false;
  }
  out.write(QJsonDocument(chapter).toJson(QJsonDocument::Indented));
  return true;
}

/**
   @brief Take back what a recorded scenario left on screen once the shot is done with it.

   A build takes every picture in one application, so the screen options a scenario opened would
   otherwise stand in the next picture too.
 */
struct scenario_cleanup_t {
  const QJsonArray& actions;
  CShotContext& ctx;
  ~scenario_cleanup_t() {
    // Holding is documentation mode leaving the state up for the writer to look at.
    if (!actions.isEmpty() && !ctx.holding()) {
      CShotRecorder::clear(actions, ctx);
    }
  }
};

QString nameOf(const QTreeWidgetItem* item) {
  const IWksItem* wks = dynamic_cast<const IWksItem*>(item);
  return nullptr == wks ? QString() : wks->getName();
}

/// @brief Depth first search for a child of `parent` with this name and type tag
QTreeWidgetItem* findItem(QTreeWidgetItem* parent, const QString& tag, const QString& name) {
  for (int i = 0; i < parent->childCount(); i++) {
    QTreeWidgetItem* child = parent->child(i);
    if (typeTag(child) == tag && nameOf(child) == name) {
      return child;
    }
    if (QTreeWidgetItem* found = findItem(child, tag, name); nullptr != found) {
      return found;
    }
  }
  return nullptr;
}
}  // namespace

QString CShotChapter::addressOf(const QWidget* main, const QWidget* widget) {
  if (nullptr == widget || widget == main) {
    return {};
  }

  // A name the .ui file gave it, and unique enough that findChild lands on this very widget.
  if (!widget->objectName().isEmpty() && main->findChild<QWidget*>(widget->objectName()) == widget) {
    return widget->objectName();
  }

  const QWidget* parent = widget->parentWidget();
  if (nullptr == parent) {
    return {};
  }

  QString prefix;
  if (parent != main) {
    prefix = addressOf(main, parent);
    if (prefix.isEmpty()) {
      return {};
    }
    prefix += "/";
  }

  const QString& className = QString::fromLatin1(widget->metaObject()->className());
  const qsizetype index = childrenOfClass(parent, className).indexOf(const_cast<QWidget*>(widget));
  if (index < 0) {
    return {};
  }
  return QString("%1%2#%3").arg(prefix, className).arg(index);
}

QWidget* CShotChapter::resolve(QWidget* main, const QString& address) {
  if (address.isEmpty()) {
    return main;
  }

  QWidget* current = main;
  const QStringList& parts = address.split('/');
  for (const QString& part : parts) {
    if (nullptr == current) {
      return nullptr;
    }
    if (part.contains('#')) {
      current = childrenOfClass(current, part.section('#', 0, 0)).value(part.section('#', 1).toInt(), nullptr);
    } else {
      current = current->findChild<QWidget*>(part);
    }
  }
  return current;
}

bool CShotChapter::driveProperty(QObject* target, const QString& property, const QVariant& value) {
  const QByteArray& name = property.toLatin1();
  if (nullptr == target || !target->setProperty(name, value)) {
    return false;
  }
  // JSON knows one number type, so the answer is compared in the property's own: 1 as a double is
  // the same currentIndex as 1 as an int.
  const QVariant& now = target->property(name);
  QVariant wanted = value;
  return !wanted.convert(now.metaType()) || wanted == now;
}

QString CShotChapter::itemPathOf(const QTreeWidgetItem* item) {
  if (nullptr == item) {
    return {};
  }
  if (nullptr != dynamic_cast<const IGisProject*>(item)) {
    return nameOf(item);
  }

  const QString& tag = typeTag(item);
  if (tag.isEmpty()) {
    return {};
  }

  // The project is the top level item; anything between is a folder and does not go in the path.
  const QTreeWidgetItem* top = item;
  while (nullptr != top->parent()) {
    top = top->parent();
  }
  return QString("%1/%2:%3").arg(nameOf(top), tag, nameOf(item));
}

QTreeWidgetItem* CShotChapter::resolveItemPath(CGisListWks* list, const QString& path) {
  if (nullptr == list || path.isEmpty()) {
    return nullptr;
  }

  const QString& projectName = path.section('/', 0, 0);
  QTreeWidgetItem* project = nullptr;
  for (int i = 0; i < list->topLevelItemCount(); i++) {
    if (nameOf(list->topLevelItem(i)) == projectName) {
      project = list->topLevelItem(i);
      break;
    }
  }
  if (nullptr == project || !path.contains('/')) {
    return project;
  }

  const QString& rest = path.section('/', 1);
  return findItem(project, rest.section(':', 0, 0), rest.section(':', 1));
}

bool CShotChapter::store(const QString& file, const QJsonObject& shot) {
  QJsonObject chapter = readChapter(file);

  QJsonArray shots = chapter["shots"].toArray();
  const QString& id = shot["id"].toString();

  int index = -1;
  for (int i = 0; i < shots.size(); i++) {
    if (shots.at(i).toObject()["id"].toString() == id) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    shots.append(shot);
  } else {
    shots.replace(index, shot);
  }
  chapter["shots"] = shots;

  return writeChapter(file, chapter);
}

bool CShotChapter::storeScenario(const QString& file, const QString& name, const QJsonArray& actions) {
  QJsonObject chapter = readChapter(file);
  QJsonObject scenarios = chapter["scenarios"].toObject();
  scenarios[name] = actions;
  chapter["scenarios"] = scenarios;
  return writeChapter(file, chapter);
}

QJsonObject CShotChapter::scenariosOf(const QString& file) { return readChapter(file)["scenarios"].toObject(); }

QStringList CShotChapter::scenarioNames(const QString& file) { return scenariosOf(file).keys(); }

QString CShotChapter::scenarioOf(const QString& file, const QString& id) {
  const QJsonArray& shots = readChapter(file)["shots"].toArray();
  for (const QJsonValue& value : shots) {
    if (value.toObject()["id"].toString() == id) {
      return value.toObject()["scenario"].toString();
    }
  }
  return {};
}

QStringList CShotChapter::shotsUsing(const QString& file, const QString& scenario) {
  QStringList ids;
  const QJsonArray& shots = readChapter(file)["shots"].toArray();
  for (const QJsonValue& value : shots) {
    if (value.toObject()["scenario"].toString() == scenario) {
      ids << value.toObject()["id"].toString();
    }
  }
  return ids;
}

bool CShotChapter::renameScenario(const QString& file, const QString& from, const QString& to) {
  QJsonObject chapter = readChapter(file);
  QJsonObject scenarios = chapter["scenarios"].toObject();
  if (to.isEmpty() || !scenarios.contains(from) || scenarios.contains(to)) {
    return false;
  }
  scenarios[to] = scenarios.value(from);
  scenarios.remove(from);
  chapter["scenarios"] = scenarios;

  QJsonArray shots = chapter["shots"].toArray();
  for (int i = 0; i < shots.size(); i++) {
    QJsonObject shot = shots.at(i).toObject();
    if (shot["scenario"].toString() != from) {
      continue;
    }
    shot["scenario"] = to;
    shots.replace(i, shot);
  }
  chapter["shots"] = shots;

  return writeChapter(file, chapter);
}

bool CShotChapter::deleteScenario(const QString& file, const QString& name) {
  QJsonObject chapter = readChapter(file);
  QJsonObject scenarios = chapter["scenarios"].toObject();
  if (!scenarios.contains(name)) {
    return false;
  }
  scenarios.remove(name);
  chapter["scenarios"] = scenarios;

  QJsonArray shots = chapter["shots"].toArray();
  for (int i = 0; i < shots.size(); i++) {
    const QJsonObject& shot = shots.at(i).toObject();
    if (shot["scenario"].toString() != name) {
      continue;
    }
    QJsonObject bare;
    bare["id"] = shot["id"];
    shots.replace(i, bare);
  }
  chapter["shots"] = shots;

  return writeChapter(file, chapter);
}

bool CShotChapter::rebindShot(const QString& file, const QString& id, const QString& scenario) {
  QJsonObject chapter = readChapter(file);
  QJsonArray shots = chapter["shots"].toArray();

  QJsonObject bare;
  bare["id"] = id;
  if (!scenario.isEmpty()) {
    bare["scenario"] = scenario;
  }

  int index = -1;
  for (int i = 0; i < shots.size(); i++) {
    if (shots.at(i).toObject()["id"].toString() == id) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    shots.append(bare);
  } else {
    shots.replace(index, bare);
  }
  chapter["shots"] = shots;

  return writeChapter(file, chapter);
}

int CShotChapter::shootOne(const QJsonObject& shot, CShotContext& ctx) {
  const QString& id = shot["id"].toString();
  int failures = 0;

  CMainWindow* main = ctx.mainWindow();

  // The window arrangement a chapter's layout would otherwise carry.
  if (nullptr != main && shot.contains("docks")) {
    QStringList wanted;
    const QJsonArray& docks = shot["docks"].toArray();
    for (const QJsonValue& dock : docks) {
      wanted << dock.toString();
    }
    showDocks(main, wanted);
  }
  if (nullptr != main && shot["canvas"].toBool()) {
    showCanvasTab(main);
  }

  // State first: it is what the shot is about.
  const QJsonArray& expand = shot["expand"].toArray();
  for (const QJsonValue& path : expand) {
    if (QTreeWidgetItem* item = resolveItemPath(ctx.wksList(), path.toString()); nullptr != item) {
      item->setExpanded(true);
    } else {
      qWarning() << "shoot:" << id << "cannot expand" << path.toString();
      failures++;
    }
  }

  if (shot.contains("select")) {
    QTreeWidgetItem* item = resolveItemPath(ctx.wksList(), shot["select"].toString());
    if (nullptr == item) {
      qWarning() << "shoot:" << id << "cannot select" << shot["select"].toString();
      return failures + 1;
    }
    for (QTreeWidgetItem* up = item->parent(); nullptr != up; up = up->parent()) {
      up->setExpanded(true);
    }
    ctx.wksList()->setCurrentItem(item);
  }

  const QJsonArray& size = shot["size"].toArray();
  QSize renderSize = (2 == size.size()) ? QSize(size.at(0).toInt(), size.at(1).toInt()) : QSize();

  ctx.beginRecipe(id);

  // A rectangle is not a kind of shot, it is what is kept of one: whatever produced the picture -
  // a widget, an exposure, a scenario - the rectangle is cut out of the result. That is what makes
  // a region of something dynamic possible at all, because the scenario is what puts the
  // application into the state the rectangle frames.
  const QJsonArray& region = shot["rect"].toArray();
  ctx.setCrop(4 == region.size()
                  ? QRect(region.at(0).toInt(), region.at(1).toInt(), region.at(2).toInt(), region.at(3).toInt())
                  : QRect());

  // The scenario puts the application into the state; what is photographed is still this shot's
  // widget or exposure.
  QJsonArray performed;
  const scenario_cleanup_t cleanup{performed, ctx};

  const QString& scenario = shot["scenario"].toString();
  if (!scenario.isEmpty()) {
    if (!ctx.scenarios().contains(scenario)) {
      qWarning() << "shoot:" << id << "wants the scenario" << scenario << "which this chapter has not got";
      return failures + 1;
    }
    // The window's size first. Resizing it afterwards resizes the canvas, and the canvas size is
    // what decides which piece of the world a map area lands on and where on screen the item sits
    // that a click anchored its options to.
    if (nullptr != main && renderSize.isValid() && shot["widget"].toString().isEmpty()) {
      main->resize(renderSize);
      CShotWriter::settle(main);
    }
    performed = ctx.scenarios().value(scenario).toArray();
    failures += CShotRecorder::replay(performed, ctx);

    // The scenario's own layout owns the window size from here on. Resizing again at render time
    // would lay the canvas out afresh and move the map under a rectangle that was measured against
    // this state - documentation mode never resizes twice, which is why it looked right there.
    if (nullptr != main && shot["widget"].toString().isEmpty()) {
      renderSize = main->size();
    }
  }

  QWidget* widget = nullptr;
  bool owned = false;
  if (shot.contains("exposure")) {
    widget = CShotRegistry::self().buildExposure(shot["exposure"].toString(), ctx, ctx.parent());
    owned = true;
    if (nullptr == widget) {
      qWarning() << "shoot:" << id << "has no exposure" << shot["exposure"].toString();
      return failures + 1;
    }
  } else {
    widget = resolve(main, shot["widget"].toString());
    if (nullptr == widget) {
      qWarning() << "shoot:" << id << "found no widget at" << shot["widget"].toString();
      return failures + 1;
    }
  }

  // Drive inputs by name. Unchecked by the compiler on purpose; an unknown name fails here. A key
  // without a dot is a property of the photographed widget itself, which is how a tab page is
  // named when the .ui file left the tab widget unnamed.
  const QJsonObject& set = shot["set"].toObject();
  for (auto it = set.constBegin(); it != set.constEnd(); ++it) {
    const QString& name = it.key().section('.', 0, 0);
    const QString& property = it.key().contains('.') ? it.key().section('.', 1) : it.key();
    // The widget part is an address relative to the photographed widget, so an unnamed tab widget
    // is reachable as QTabWidget#0 just like anywhere else.
    QObject* driven = it.key().contains('.') ? resolve(widget, name) : widget;
    if (!driveProperty(driven, property, it.value().toVariant())) {
      qWarning() << "shoot:" << id << "cannot set" << it.key() << "to" << it.value().toVariant();
      failures++;
    }
  }
  CShotWriter::settle(widget);

  // Only a window can be given a size. Anything inside the main window is sized by the layout, so
  // resizing it here would be undone at the next layout pass and the picture would not match the
  // number in the file. What decides such a picture's size is the chapter's stored arrangement.
  if (!widget->isWindow() && renderSize.isValid()) {
    renderSize = QSize();
  }

  ctx.shot(widget, renderSize);
  if (ctx.cropMissed()) {
    qWarning() << "shoot:" << id << "region does not fit the picture";
    failures++;
  }
  ctx.setCrop({});
  if (owned) {
    delete widget;
  }
  return failures;
}

int CShotChapter::run(const QString& file, CShotContext& ctx, const QString& only, const QString& scenario) {
  QFile in(file);
  if (!in.open(QIODevice::ReadOnly)) {
    qWarning() << "shoot: cannot read" << file;
    return 1;
  }
  const QJsonObject& chapter = QJsonDocument::fromJson(in.readAll()).object();

  // What a shot's "scenario" resolves against: this chapter's recordings first, the registry after.
  ctx.setScenarios(chapter["scenarios"].toObject());

  int failures = 0;
  const QJsonArray& shots = chapter["shots"].toArray();
  for (const QJsonValue& value : shots) {
    const QJsonObject& shot = value.toObject();
    const QString& id = shot["id"].toString();

    if (!only.isEmpty() && only != id) {
      continue;
    }
    if (!scenario.isEmpty()) {
      const QString& own = shot["scenario"].toString();
      const bool wanted = (kBaseScenario == scenario) ? own.isEmpty() : (own == scenario);
      if (!wanted) {
        continue;
      }
    }

    failures += shootOne(shot, ctx);
  }
  return failures;
}
