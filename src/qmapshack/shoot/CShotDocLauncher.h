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

#ifndef CSHOTDOCLAUNCHER_H
#define CSHOTDOCLAUNCHER_H

#include <QDir>
#include <QObject>
#include <QSet>
#include <QString>

class CShotDocPanel;
class QLocalServer;
class QLocalSocket;
class QProcess;
class QTemporaryDir;

/**
   @brief The panel, and the state process it starts.

   Documentation mode is two processes. This one owns the panel and nothing else: it reads the
   chapter file, shows the pictures, and does the operations that are file operations. Everything
   that needs a running application - replaying a scenario, taking a picture, recording - happens in
   a state process it starts and throws away.

   That split is the whole point. A state is entered by starting a process in it, never by taking
   the last one back down, and the panel is not in that process, so switching states cannot cost the
   writer their window, their selection, or a session.
 */
class CShotDocLauncher : public QObject {
  Q_OBJECT
 public:
  /// @param repo     the checkout the panel reads and writes
  /// @param chapter  which chapter is being worked on
  CShotDocLauncher(const QDir& repo, const QString& chapter, QObject* parent);
  virtual ~CShotDocLauncher();

  /// @brief Show the panel and start the state process in the base
  void start();

 private:
  /// @brief Start a state process in this scenario, replacing whatever is running
  void enterScenario(const QString& scenario, bool startRecording = false);

  /// @brief The address the state process connects back on, unique to this launcher
  QString channelName() const;

  /// @brief End the running state process and wait for it to be gone
  void stopState();

  /// @brief Ask for the recording's name and tell the state process what to do with it
  void nameRecording(const QString& suggestion);

  /// @brief One line to the state process; nothing when none is running
  void command(const QString& line);

  /// @brief Tell the state process which picture the writer has selected, so F9 offers it first
  void sendSelection();

  /// @brief One line back from it: `status`, `tagged`, `recorded`, `recording`
  void handleReport(const QString& line);

  /// @brief The scenario's whole configuration, composed by `shots.py` so both paths use one
  /// @param error  what went wrong, when nothing was written
  /// @return The file, or an empty string when composing failed
  QString composeConfig(const QString& scenario, QString& error);

  /// @brief Say outright that the session cannot go on, in a box the writer cannot miss.
  ///
  /// A Windows build is a GUI subsystem binary, so nothing written to the console arrives and a
  /// failure that only logs looks like a panel that opens onto nothing.
  void reportFailure(const QString& text);

  /// @brief The command line this process was started with, with the config and the state replaced
  QStringList childArguments(const QString& config, const QString& scenario) const;

  /**
     @brief Take every picture of this chapter again - as a build, not in the running application

     A picture is taken in a process started in its scenario. This one is in whatever state the
     writer has left it in, so taking them here would answer a question nothing asks: `shots.py
     chapter` is the same command the build runs, over the same configurations, onto the same
     files, and what comes out of it is what the build reproduces.
   */
  void retakeChapter();

  /// @brief Re-read the chapter file and show its shots
  void refreshPanel(const QString& status = QString());

  QString chapterPath() const;

  // --- what the panel asks for, and none of it needs a running application ----------------------

  void renameScenario();
  void deleteScenario();
  void rebindShot(const QString& id, const QString& scenario);
  void reapUnused();

  /// @brief Show the picture, and enter its state when this one is not it already
  void showShot(const QString& id);

  QDir repo;
  QString chapter;

  /// The state the running process was started in; empty is the base
  QString liveScenario;

  /// What the writer has selected in the panel; a new picture is taken in it
  QString selectedScenario;

  /// The picture the writer has selected. Ours, not the panel's: the list is rebuilt from the
  /// chapter file on every refresh, and a view cannot be the record of what the writer chose.
  QString selectedShot;

  bool recording = false;

  /// True while we are the reason the state process is going. A state that ends without it is the
  /// writer closing the application, which ends the session.
  bool killing = false;

  /// Asked for before the state process is up, so it is sent as soon as it connects
  bool recordOnStart = false;

  /// Ids whose image the last retake changed; the state process reports them
  QSet<QString> changedShots;

  CShotDocPanel* panel = nullptr;
  QProcess* state = nullptr;
  /// The build a retake runs; one at a time
  QProcess* retake = nullptr;
  QLocalServer* server = nullptr;
  QLocalSocket* channel = nullptr;
  QTemporaryDir* scratch = nullptr;
};

#endif  // CSHOTDOCLAUNCHER_H
