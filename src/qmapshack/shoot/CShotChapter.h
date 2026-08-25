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

#ifndef CSHOTCHAPTER_H
#define CSHOTCHAPTER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class CShotContext;
class CGisListWks;
class CMainWindow;
class QTreeWidgetItem;
class QWidget;

/**
   @brief One chapter's shots, stored as JSON instead of generated C++.

   Documentation mode and the headless run go through shootOne(), so the picture the writer accepts
   is produced by the same code that reproduces it later.
 */
namespace CShotChapter {
/// What `--shoot-scenario` is given for the shots that name no scenario: the application as the
/// configuration starts it. A scenario cannot be called this - the panel refuses the name.
const QString kBaseScenario = QStringLiteral("-");

// --- addressing a widget of the running application -------------------------------------------

/**
   @brief How a shot names a widget of the running application.

   `dockWorkspace` for anything the .ui files named. A widget built in code has no objectName, so it
   is addressed relative to its nearest named ancestor by class and position: `IMapList/QMenu#0`.

   @return An empty string for the main window itself, or when the widget cannot be addressed
 */
QString addressOf(const QWidget* main, const QWidget* widget);

/// @return The widget the address names, or nullptr
QWidget* resolve(QWidget* main, const QString& address);

// --- addressing an item of the workspace ------------------------------------------------------

/// @brief `Shoot Demo/trk:Demo Track`, or an empty string for an item that is not addressable
QString itemPathOf(const QTreeWidgetItem* item);

/// @return The item the path names, or nullptr
QTreeWidgetItem* resolveItemPath(CGisListWks* list, const QString& path);

// --- the chapter file -------------------------------------------------------------------------

/**
   @brief Put one shot into the chapter file, creating the file if needed.

   An id that is already in the file is replaced in place, so taking a picture again with the same
   name attaches a new image to the entry the page already references.
 */
bool store(const QString& file, const QJsonObject& shot);

/**
   @brief Put a scenario the writer performed into the chapter file under its name.

   A scenario recorded here is the chapter's own data, so several of its shots can reference one
   and nobody has to read a global list to find out whether it fits. A name that is already in the
   file is replaced.
 */
bool storeScenario(const QString& file, const QString& name, const QJsonArray& actions);

/// @return The chapter's recorded scenarios, by name
QJsonObject scenariosOf(const QString& file);

/// @return Every recorded scenario's name, sorted
QStringList scenarioNames(const QString& file);

/// @return The scenario a shot is taken in, empty when it has none yet
QString scenarioOf(const QString& file, const QString& id);

/// @return The ids of the shots taken in this scenario
QStringList shotsUsing(const QString& file, const QString& scenario);

/**
   @brief Rename a scenario and every shot taken in it.

   A rename says nothing about the state, so it invalidates no picture.
 */
bool renameScenario(const QString& file, const QString& from, const QString& to);

/**
   @brief Drop a scenario, and every definition that only meant something inside it.

   A widget address and a rectangle frame something else in another state, so nothing of a shot's
   definition survives losing its scenario: the entry is reduced to its bare name. It stays because
   the page still asks for the picture, and it is taken again from scratch.
 */
bool deleteScenario(const QString& file, const QString& name);

/// @brief Take a shot in another scenario, dropping everything else the entry said - see above
bool rebindShot(const QString& file, const QString& id, const QString& scenario);

/// @brief Apply one shot's state and emit its image
/// @return The number of failures
int shootOne(const QJsonObject& shot, CShotContext& ctx);

/**
   @brief Shoot the chapter's entries.

   @param only      one shot id, or empty for all of them
   @param scenario  only the shots taken in this scenario, `kBaseScenario` for the ones taken in
                    no scenario at all, or empty for all of them. A scenario carries its own
                    configuration, and settings are read in constructors, so a run covers one
                    scenario per process.

   @return The number of failures
 */
int run(const QString& file, CShotContext& ctx, const QString& only = QString(), const QString& scenario = QString());
}  // namespace CShotChapter

#endif  // CSHOTCHAPTER_H
