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

#include "shoot/CShotRunner.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "shoot/CShotChapter.h"
#include "shoot/CShotContext.h"
#include "shoot/CShotFixture.h"
#include "shoot/CShotRegistry.h"
#include "shoot/CShotWriter.h"

namespace {
/// Preview images for list/inspect/explore go here, never beside the documentation images.
const QString kPreviewDir = "_preview";

/// @brief True if the two renders differ in size or in a single pixel
bool differs(const QImage& a, const QImage& b) { return a.size() != b.size() || a != b; }

/// @brief Properties worth showing a recipe author, out of the hundreds a QWidget declares
bool isInterestingProperty(const QString& name) {
  static const QSet<QString> interesting = {"text",     "title",       "checked", "checkable",      "currentIndex",
                                            "value",    "minimum",     "maximum", "visible",        "enabled",
                                            "readOnly", "currentText", "count",   "placeholderText"};
  return interesting.contains(name);
}

/// @brief One control of the pre-drive snapshot; the pointer clears when the application deletes it
struct control_t {
  QPointer<QWidget> widget;
  QString name;
  QString type;
};

/// @brief How many of the snapshotted widgets the application has not destroyed yet
qsizetype countAlive(const QList<QPointer<QWidget>>& widgets) {
  qsizetype alive = 0;
  for (const QPointer<QWidget>& widget : widgets) {
    if (!widget.isNull()) {
      alive++;
    }
  }
  return alive;
}
}  // namespace

CShotRunner::task_e CShotRunner::taskFromName(const QString& name) {
  if (name == "list") {
    return eTaskList;
  }
  if (name == "inspect") {
    return eTaskInspect;
  }
  if (name == "explore") {
    return eTaskExplore;
  }
  return eTaskChapter;
}

CShotRunner::CShotRunner(task_e task, const QDir& outDir, const QString& only, const QString& target,
                         const QString& scenario, QObject* parent)
    : QObject(parent), task(task), outDir(outDir), only(only), target(target), scenario(scenario) {}

void CShotRunner::start() { QTimer::singleShot(0, this, &CShotRunner::slotRun); }

void CShotRunner::waitForApplication() {
  // CMainWindow defers slotLateInit() and slotSanityTest() by 100 ms, so a fixed spin is the only
  // honest way past them. Everything after that waits on the canvas itself.
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 300) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
  }

  if (!CMainWindow::isNull()) {
    const QList<CCanvas*>& canvases = CMainWindow::self().getCanvas();
    for (CCanvas* canvas : canvases) {
      canvas->waitForDrawContexts();
    }
  }
}

void CShotRunner::slotRun() {
  waitForApplication();

  outDir.mkpath(".");

  switch (task) {
    case eTaskList:
      runList();
      break;
    case eTaskInspect:
      runInspect();
      break;
    case eTaskExplore:
      runExplore();
      break;
    case eTaskChapter:
      runChapter();
      break;
  }

  qApp->quit();
}

void CShotRunner::writeReport(const QString& name, const QJsonObject& report) {
  QFile file(outDir.absoluteFilePath(name));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning() << "shoot: cannot write" << file.fileName();
    failures_++;
    return;
  }
  file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
}

void CShotRunner::runChapter() {
  CShotWriter writer(outDir, "en");
  CShotContext ctx(writer, "en");
  CShotFixture::build(ctx);

  failures_ += CShotChapter::run(target, ctx, only, scenario);
  failures_ += writer.failures();
  qDebug() << "shoot:" << writer.manifest().size() << "images from" << target << "," << failures_ << "failures";
}

void CShotRunner::runList() {
  // Only the widget classes a shot can build from nothing. There is no list of scenarios to report:
  // a scenario is recorded by the writer and lives in the chapter file, not in this build.
  QJsonArray exposures;
  const QStringList& exposureIds = CShotRegistry::self().exposureIds();
  for (const QString& id : exposureIds) {
    QJsonObject entry;
    entry["id"] = id;
    entry["description"] = CShotRegistry::self().exposureDescription(id);
    exposures.append(entry);
  }

  QJsonObject report;
  report["exposures"] = exposures;
  writeReport("list.json", report);
}

void CShotRunner::runInspect() {
  QDir previewDir(outDir.absoluteFilePath(kPreviewDir));
  previewDir.mkpath(".");

  CShotWriter writer(previewDir, "en");
  CShotContext ctx(writer, "en");
  CShotFixture::build(ctx);

  QWidget* w = CShotRegistry::self().buildExposure(target, ctx, ctx.parent());
  if (nullptr == w) {
    qWarning() << "shoot: no exposure named" << target;
    failures_++;
    return;
  }
  CShotWriter::settle(w);
  w->resize(w->size().expandedTo(w->sizeHint()));
  CShotWriter::settle(w);

  QJsonArray children;
  const QList<QWidget*>& kids = w->findChildren<QWidget*>();
  for (QWidget* kid : kids) {
    if (kid->objectName().isEmpty()) {
      continue;
    }

    QJsonObject entry;
    entry["name"] = kid->objectName();
    entry["type"] = QString::fromLatin1(kid->metaObject()->className());

    QJsonObject props;
    const QMetaObject* meta = kid->metaObject();
    for (int i = 0; i < meta->propertyCount(); i++) {
      const QMetaProperty& prop = meta->property(i);
      const QString& name = QString::fromLatin1(prop.name());
      if (!prop.isWritable() || !isInterestingProperty(name)) {
        continue;
      }
      const QVariant& value = prop.read(kid);
      if (value.canConvert<QString>()) {
        props[name] = value.toString();
      }
    }
    entry["properties"] = props;
    children.append(entry);
  }

  QJsonObject report;
  report["target"] = target;
  report["type"] = QString::fromLatin1(w->metaObject()->className());
  report["children"] = children;
  report["preview"] = kPreviewDir + "/" + QFileInfo(writer.write(CShotWriter::render(w, {}), target)).fileName();
  writeReport("inspect.json", report);

  delete w;
}

void CShotRunner::runExplore() {
  QDir previewDir(outDir.absoluteFilePath(kPreviewDir));
  previewDir.mkpath(".");

  CShotWriter writer(previewDir, "en");
  CShotContext ctx(writer, "en");
  CShotFixture::build(ctx);

  QWidget* w = CShotRegistry::self().buildExposure(target, ctx, ctx.parent());
  if (nullptr == w) {
    qWarning() << "shoot: no exposure named" << target;
    failures_++;
    return;
  }

  CShotWriter::settle(w);
  w->resize(w->size().expandedTo(w->sizeHint()));
  CShotWriter::settle(w);
  const QImage& baseline = w->grab().toImage();
  writer.write(baseline, target + "-baseline");

  QJsonArray controls;

  // A drive can rebuild part of the tree under the widget - CSelectActivityColor::updateData()
  // qDeleteAll()s its rows when the track colour changes - so the snapshot is held as QPointer and
  // re-checked after every drive. Name and type are copied out because they are unreadable once the
  // widget is gone.
  QList<control_t> snapshot;
  QList<QPointer<QWidget>> allKids;
  const QList<QWidget*>& kids = w->findChildren<QWidget*>();
  snapshot.reserve(kids.size());
  allKids.reserve(kids.size());
  for (QWidget* kid : kids) {
    allKids.append(kid);
    if (!kid->objectName().isEmpty()) {
      snapshot.append({kid, kid->objectName(), QString::fromLatin1(kid->metaObject()->className())});
    }
  }

  const QPointer<QWidget> exposed(w);
  const QString exposedType = QString::fromLatin1(w->metaObject()->className());

  // Drive one input at a time and let the application's own signal chain produce the state. Setting
  // an output property directly would report a state no user can reach.
  for (const control_t& control : snapshot) {
    QJsonObject entry;
    entry["name"] = control.name;
    entry["type"] = control.type;

    if (control.widget.isNull()) {
      entry["effect"] = "rebuilt while another control was driven; not explored";
      controls.append(entry);
      continue;
    }

    QWidget* kid = control.widget;
    if (!kid->isEnabled()) {
      continue;
    }

    const qsizetype aliveBefore = countAlive(allKids);

    QString drove;
    std::function<void()> restore;

    if (QAbstractButton* button = qobject_cast<QAbstractButton*>(kid); nullptr != button && button->isCheckable()) {
      const bool was = button->isChecked();
      drove = QString("checked -> %1").arg(!was);
      button->setChecked(!was);
      restore = [button, was]() { button->setChecked(was); };
    } else if (QComboBox* combo = qobject_cast<QComboBox*>(kid); nullptr != combo && combo->count() > 1) {
      const int was = combo->currentIndex();
      const int next = (was + 1) % combo->count();
      drove =
          QString("currentIndex -> %1 (\"%2\", %3 entries)").arg(next).arg(combo->itemText(next)).arg(combo->count());
      combo->setCurrentIndex(next);
      restore = [combo, was]() { combo->setCurrentIndex(was); };
    } else if (QTabWidget* tabs = qobject_cast<QTabWidget*>(kid); nullptr != tabs && tabs->count() > 1) {
      const int was = tabs->currentIndex();
      const int count = tabs->count();
      QStringList titles;
      for (int i = 0; i < count; i++) {
        titles << tabs->tabText(i);
        tabs->setCurrentIndex(i);
        CShotWriter::settle(w);
        if (exposed.isNull() || control.widget.isNull()) {
          break;
        }
        writer.write(w->grab().toImage(), QString("%1-%2-tab%3").arg(target, control.name).arg(i));
      }
      if (!control.widget.isNull()) {
        tabs->setCurrentIndex(was);
        CShotWriter::settle(w);
      }
      entry["effect"] = QString("%1 tabs: %2").arg(count).arg(titles.join(", "));
      controls.append(entry);
      if (exposed.isNull()) {
        break;
      }
      continue;
    } else if (QGroupBox* group = qobject_cast<QGroupBox*>(kid); nullptr != group && group->isCheckable()) {
      const bool was = group->isChecked();
      drove = QString("checked -> %1").arg(!was);
      group->setChecked(!was);
      restore = [group, was]() { group->setChecked(was); };
    } else {
      continue;
    }

    CShotWriter::settle(w);
    if (exposed.isNull()) {
      entry["drove"] = drove;
      entry["effect"] = "destroyed the widget under exploration";
      controls.append(entry);
      break;
    }

    const QImage& after = w->grab().toImage();
    const qsizetype rebuilt = aliveBefore - countAlive(allKids);
    if (!control.widget.isNull()) {
      restore();
    }

    entry["drove"] = drove;
    if (rebuilt > 0) {
      entry["rebuilt"] = int(rebuilt);
    }
    if (differs(baseline, after)) {
      entry["effect"] = (baseline.size() != after.size()) ? "layout changes" : "appearance changes";
      entry["preview"] =
          kPreviewDir + "/" + QFileInfo(writer.write(after, QString("%1-%2").arg(target, control.name))).fileName();
    } else {
      entry["effect"] = "no visible change";
    }
    controls.append(entry);
  }

  if (!exposed.isNull()) {
    CShotWriter::settle(w);
  }

  QJsonObject report;
  report["target"] = target;
  report["type"] = exposedType;
  report["baseline"] = kPreviewDir + "/" + target + "-baseline.png";
  report["controls"] = controls;
  writeReport("explore.json", report);

  if (!exposed.isNull()) {
    delete w;
  }
}
