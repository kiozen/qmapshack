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

#ifndef CSHOTDOCMODE_H
#define CSHOTDOCMODE_H

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>

class CMainWindow;
class CShotContext;
class CShotWriter;
class QLocalSocket;
class QWidget;

/**
   @brief Interactive authoring: the writer navigates to a widget, presses F9, and the recipe
          writes itself.

   The tagged widget is only used to identify its class. The image comes from a freshly built
   instance rendered through the same CShotWriter path the headless build uses, so what the writer
   accepts is what regeneration reproduces.
 */
class CShotDocMode : public QObject {
  Q_OBJECT
 public:
  /// @param repo      the checkout to write images and chapter files into
  /// @param chapter   which chapter file F9 appends to
  /// @param scenario  the state to come up in; `CShotChapter::kBaseScenario` for the base
  CShotDocMode(const QDir& repo, const QString& chapter, const QString& scenario, QObject* parent);
  virtual ~CShotDocMode();

  /// @brief Connect back to the launcher and queue the fixture build
  void start();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
  void slotBuildFixture();

  /// @brief Put the application on the screen the panel is on, keeping its size
  void moveToWritersScreen();

 private:
  /// @brief Do what the launcher asked for: `region`, `update`, `record`, `stop`, `sync`
  void obey(const QString& line);

  /// @brief One line back to the launcher; logged instead when there is none
  void send(const QString& line);

  /// @brief Tell the launcher what to put in the panel's status line
  void report(const QString& status);

 private:
  /// @brief The window the writer means: the modal one, else the active one, else the focused one
  static QWidget* activeTarget();

  /// @return Every `images/<id>.png` the chapter's page references
  QSet<QString> pageReferences() const;

  /// @return Every id the chapter file already knows
  QSet<QString> chapterIds() const;

  /**
     @brief Ask which picture this is, out of the names the page asks for.

     Only those: a picture is wanted by the text, never invented at the window. An id typed here
     would produce a row no page references, which is waste the writer then has to reap.
   */
  QString askForId() const;

  void tag();

  /**
     @brief Ask which part of the running application the writer means.

     Starts at the widget under the mouse and walks up to the whole window, offering every step
     that a recipe can find again by objectName.

     @return The chosen widget, or nullptr if the writer cancelled
   */
  QWidget* chooseLivePart(CMainWindow* main) const;

  /**
     @brief Show the writer the picture and let them throw it away again.

     @return true to keep it; on false the image is deleted and no recipe is written
   */
  bool confirmResult(const QString& id) const;

  QString imagePath(const QString& id) const;

  /// @brief Tell the writer which one line a developer has to add for an unexposed class
  void reportUnexposed(QWidget* target) const;

  /// @brief The chapter file F9 appends to
  QString chapterPath() const;

  /// @brief A scenario's own configuration: `doc/shots/<chapter>/<scenario>.ini`
  QString scenarioConfigPath(const QString& scenario) const;

  /**
     @brief Write the settings the application is running with into a scenario's own file.

     The whole configuration, not what it changes about the base: a scenario that stored a
     difference would move whenever the base did, and the base is a starting point, not a layer.
     The base is copied once, when a scenario is first recorded, and never consulted again.

     Machine-specific keys are left out; `shots.py` injects the fixture's paths at compose time.

     @return How many keys were stored
   */
  int storeScenarioConfig(const QString& scenario);

  /**
     @brief Make what the application is running with the configuration every chapter starts from.

     The base is what a chapter opens on and what a newly recorded scenario copies its settings
     from. It is not a layer under them: a scenario that already has its own file keeps it, so
     storing a base never moves a picture that has already been taken.

     Nothing that names a place on this machine goes in - `shots.py` injects the fixture's own
     paths when it composes a run, and the map list rebuilds itself from them.
   */
  void storeBaseConfig();

  /**
     @brief How far the settings on screen have drifted from the ones a scenario is shot with.

     A picture taken in documentation mode comes out of the running application, so a writer who
     has changed a setting since is accepting a picture the build will not reproduce.

     @return The number of keys that differ
   */
  int settingsDrift(const QString& scenario) const;

  /// @return A sentence to append to the status when the settings have drifted, else empty
  QString driftWarning(const QString& scenario) const;

  /**
     @brief Put everything the application is in right now into the selected scenario.

     The arrangement, the map and the settings: all three decide what a picture looks like, so all
     three belong to the state it is taken in. With the base row selected they become the base,
     which is asked about first.
   */
  void updateScenario(const QString& target);

  /**
     @brief Record what the writer performs, and on the second press store it as a scenario.

     The way out of a registry that never closes: instead of picking a recipe someone wrote per
     picture, the writer puts the application into the state by hand and what they did becomes
     chapter data. What is stored is meaning - a geographic point, an item's name path, a driven
     input - never the input events.
   */
  void toggleRecording();

  /// @brief Let this chapter's shots resolve a scenario name against its own recordings
  void syncScenarios();

  /// @return A name for the recording that this chapter does not use yet
  QString suggestedScenarioName() const;

  /// @brief Perform a scenario in an application that has just come up in it, with nothing on top
  void setUpScenario(const QString& name);

  /// @brief Begin recording. Always from the base, so what is stored is the scenario's whole state
  void startRecording();

  /// @brief Store what was recorded under this name; the supervisor is what asked for it
  void storeRecording(const QString& name);

  /// What a stopped recording produced, until the supervisor comes back with a name for it
  QJsonArray pendingActions;

  /**
     @brief Which state a picture is taken in.

     What the chapter says, when it has an entry for the id - an empty string there is a deliberate
     "as the application starts", not an unanswered question. Otherwise the scenario the writer has
     selected, which is what a picture taken for the first time gets.
   */
  QString scenarioFor(const QString& id) const;

  /**
     @brief Photograph a rectangle the writer drags over the window.

     For what no single widget is: a docker and the map beside it, one corner of a dialog, a detail
     worth showing on its own. The scenario is built first and stays standing while the writer
     drags, so the same part is cut out of the same state next time.
   */
  void takeRegion();

  /// @return The chapter's entry for this id, empty when it has none
  QJsonObject shotOf(const QString& id) const;

  QString chapter;

  /// What this process was started in; empty is the base. It is the whole of what it is for.
  QString ownScenario;

  /// The picture the writer has selected in the panel, which F9 offers first - taken or not
  QString wantedShot;

  /**
     The base as this process started in it: the arrangement and the view, taken once the fixture
     is up and before anything is replayed on top.

     Captured, never stored. A per-chapter copy of the start state can go stale; the start state
     cannot - which is the whole reason the base is a row and not a scenario.
   */
  QJsonArray baseState;

  QDir repo;
  CShotWriter* writer = nullptr;
  CShotContext* ctx = nullptr;
  class CShotRecorder* recorder = nullptr;
  QLocalSocket* channel = nullptr;
  /// Guards against re-entering tag() from the dialogs tag() itself opens
  bool tagging = false;
};

#endif  // CSHOTDOCMODE_H
