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

#ifndef CSHOTDOCPANEL_H
#define CSHOTDOCPANEL_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/**
   @brief The writer's cheat sheet while documentation mode is running.

   Two sections, because a chapter is made in two steps. The scenarios are recorded, renamed and
   deleted at the top; the pictures the page asks for are listed below, each with the scenario it is
   taken in. A picture with no scenario cannot be taken - there is no state to take it in.

   A tool window of its own, so it never ends up in a picture: a shot renders one widget, not the
   screen. It takes no focus, or F9 would target the panel instead of the application.
 */
class CShotDocPanel : public QDialog {
  Q_OBJECT
 public:
  /// What a picture's row says about it. Compared, never displayed - the label is translated.
  enum state_e {
    eTaken,    ///< the image exists and the page uses it
    eNoImage,  ///< the chapter knows it, no file on disk
    eNotUsed,  ///< the image exists, no page references it
    eMissing,  ///< the page references it, the chapter has no shot
  };

  /// @brief What the writer needs to know about one picture
  struct entry_t {
    QString id;
    /// The scenario it is taken in, empty when it has none yet
    QString scenario;
    QString note;
    state_e state = eTaken;
    /// The last retake produced a different image than the one on disk before it
    bool changed = false;
    /// Empty when there is no image on disk yet
    QString imagePath;
  };

  CShotDocPanel(const QString& chapter, QWidget* parent);

  /**
     @brief Fill the scenario list. Call before setShots(): the rows' combo boxes offer these.

     A row for the base comes first and is not one of @p names: it is not stored anywhere, it is
     the application as the configuration starts it, and its name is an empty string.
   */
  void setScenarios(const QStringList& names, const QString& current);

  void setShots(const QList<entry_t>& shots);

  /// @return The id of the selected picture, or an empty string
  QString currentId() const;

  // --- what the panel asks documentation mode to do ------------------------------------------

  /// @brief A picture row was clicked; put the state it was taken in back on screen
  void setPickedHandler(std::function<void(const QString&)> handler) { picked = handler; }

  /// @brief A scenario row was clicked; put that state on screen
  void setScenarioPickedHandler(std::function<void(const QString&)> handler) { scenarioPicked = handler; }

  /// @brief Start recording a scenario, and again to stop it
  void setRecordHandler(std::function<void()> handler) { record = handler; }

  void setRenameHandler(std::function<void()> handler) { rename = handler; }
  void setDeleteScenarioHandler(std::function<void()> handler) { deleteScenario = handler; }

  /// @brief The writer chose another scenario for a picture in its own combo box
  void setRebindHandler(std::function<void(const QString&, const QString&)> handler) { rebind = handler; }

  /// @brief The writer wants a rectangle of the window instead of one of its widgets
  void setTakeRegionHandler(std::function<void()> handler) { takeRegion = handler; }

  void setReapHandler(std::function<void()> handler) { reap = handler; }

  void setRetakeHandler(std::function<void()> handler) { retake = handler; }
  void setStoreLayoutHandler(std::function<void()> handler) { storeLayout = handler; }

  /// @brief Show what the last thing the writer did produced
  void setStatus(const QString& text);

  /// @brief Name the page whose image lines drive the list
  void setPage(const QString& path, bool exists);

  /**
     @brief Say whether a recording is running.

     Everything else is disabled while one is: what the writer does to the panel is not part of the
     scenario, and half of it would change the state being recorded.
   */
  void setRecording(bool on);

 protected:
  /// @brief The panel lives as long as documentation mode does
  void closeEvent(QCloseEvent* event) override;

  /// @brief Swallow Escape, which would otherwise close the dialog
  void reject() override;

 private:
  void showPreview();

  /// @brief The combo box a picture's row carries, offering every scenario and "none"
  void buildScenarioCell(QTreeWidgetItem* row, const entry_t& entry);

  static QString label(state_e state);

  std::function<void(const QString&)> picked;
  std::function<void(const QString&)> scenarioPicked;
  std::function<void()> record;
  std::function<void()> rename;
  std::function<void()> deleteScenario;
  std::function<void(const QString&, const QString&)> rebind;
  std::function<void()> takeRegion;
  std::function<void()> reap;
  std::function<void()> retake;
  std::function<void()> storeLayout;

  QListWidget* scenarios = nullptr;
  QTreeWidget* shots = nullptr;
  QPushButton* recordButton = nullptr;
  QPushButton* reapButton = nullptr;
  /// Everything a recording has to keep the writer away from
  QList<QPushButton*> whileIdle;
  QLabel* page = nullptr;
  QLabel* status = nullptr;
  QLabel* preview = nullptr;

  QStringList scenarioNames;
  QList<entry_t> entries;
  /// Guards the combo boxes while the list is rebuilt: setting one is not the writer choosing it
  bool populating = false;
};

#endif  // CSHOTDOCPANEL_H
