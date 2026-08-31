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

#ifndef CSHOTRUNNER_H
#define CSHOTRUNNER_H

#include <QDir>
#include <QJsonObject>
#include <QObject>
#include <QString>

/**
   @brief Drives one shoot run and quits the application when it is done.

   Started from a queued invocation, never inline in main(): the main window defers part of its
   own initialization by a timer and the canvases start their draw threads asynchronously.
 */
class CShotRunner : public QObject {
  Q_OBJECT
 public:
  enum task_e { eTaskChapter, eTaskList, eTaskInspect, eTaskExplore };

  /// @return eTaskChapter for an unknown name
  static task_e taskFromName(const QString& name);

  CShotRunner(task_e task, const QDir& outDir, const QString& only, const QString& target, const QString& scenario,
              QObject* parent);

  /// @brief Queue the run; the application quits with the failure count when it returns
  void start();

  int failures() const { return failures_; }

 private slots:
  void slotRun();

 private:
  /// @brief Spin the event loop until the main window and its canvases have settled
  void waitForApplication();

  void runList();
  void runInspect();
  void runExplore();

  /// @brief Shoot the JSON chapter file named by --shoot-target
  void runChapter();

  /// @brief Write a machine readable result beside the images; shots.py reads this, not stdout
  void writeReport(const QString& name, const QJsonObject& report);

  task_e task;
  QDir outDir;
  QString only;
  QString target;
  /// Which of the chapter's scenarios this process takes; empty for all of them
  QString scenario;
  int failures_ = 0;
};

#endif  // CSHOTRUNNER_H
