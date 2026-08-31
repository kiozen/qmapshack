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

#ifndef CSHOTRECORDER_H
#define CSHOTRECORDER_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariant>
#include <functional>

class CCanvas;
class CShotContext;
class QWidget;

/**
   @brief A scenario the writer performs instead of a recipe a developer writes.

   The registry does not close: a recipe is written per picture, so its list grows with the
   documentation and a writer has to read it to know whether an entry fits. Here the writer puts
   the application into the state by hand and what they did becomes the chapter's own data.

   **What is recorded is meaning, never input.** A click on the map is a geographic point, a click
   in the workspace is the item's name path, a control is its address and the value driven into it.
   Pixels, zoom, window size and device pixel ratio never enter the recording, so a GUI change does
   not invalidate it - the same property that lets a JSON shot survive one.

   That is why nothing here watches for a button press. A press is input; what it produced is
   state, and the state is what the diff below finds. A button with no state to change is not
   recordable, and that is the honest boundary rather than a replayed click that means nothing the
   next time the dialog is laid out differently.
 */
class CShotRecorder : public QObject {
  Q_OBJECT
 public:
  explicit CShotRecorder(CShotContext& ctx, QObject* parent = nullptr);
  virtual ~CShotRecorder();

  /**
     @brief Watch the application and take the state it is in now as the baseline.

     @param base  a scenario the recording carries on from. Its steps are copied into the new one
                  and the writer performs the difference on top; `layout` and `view` are left out
                  because both are taken fresh when the recording stops.

     Copied, never referenced: a scenario stays self-contained, so deleting the one it started from
     cannot change it afterwards.
   */
  void start(const QJsonArray& base = {});

  /// @brief Stop watching and hand over what was performed
  QJsonArray stop();

  bool isRecording() const { return recording; }

  qsizetype steps() const { return actions.size(); }

  /// @brief Never record what the writer does on the documentation panel itself
  void setIgnored(QWidget* widget) { ignored = widget; }

  /**
     @brief Put the application into the state a recording describes.

     Every action is resolved against the running application, so a name path that no longer exists
     or a map the canvas cannot show is a failure with the action printed, never a silently wrong
     picture.

     @param whenReady  run after the last step and before the queue lets go. A modal dialog a step
                       opened is still on screen there and nowhere else: it runs its own event
                       loop, so anything after `replay()` returns happens with the dialog closed.

     @return The number of failures
   */
  static int replay(const QJsonArray& actions, CShotContext& ctx, const std::function<void()>& whenReady = {});

  /**
     @brief Take back what a replay left on screen.

     A build takes one picture after another in one application, so a scenario that leaves the
     screen options of an item standing would put them in the next chapter's picture too.
   */
  static void clear(const QJsonArray& actions, CShotContext& ctx);

  /**
     @brief Put the application back into the state a chapter starts in.

     A recording is a difference from what was on screen when it started, and a build replays it
     against the fresh fixture. So a recording performed on top of another scenario's leftovers
     would describe a different difference than the one that is replayed.
   */
  static void reset(CShotContext& ctx);

  /**
     @brief The window arrangement as one action.

     Dockers, their sizes and the window size decide how big a picture comes out, so they belong to
     the state a picture is taken in, not to the chapter. Qt's own `saveState()` blob is what it
     takes: which dockers are visible is expressible in the open, where they sit and how wide they
     are is not, and a docker's width is exactly what decides a picture's width.
   */
  static QJsonObject layoutOf(CShotContext& ctx);

  /**
     @brief Where the map is looking, as one action: the centre in degrees and the zoom level.

     The exact view, not the visible rectangle. `zoomTo()` refits a rectangle to the canvas aspect
     and snaps it to a zoom level, so the centre and the scale both drift and everything on the map
     lands somewhere else - including the pixel a click anchors an item's options to, which is what
     a stored rectangle was measured against.

     Only the view, not `CCanvas::saveConfig()`. Which maps, DEM, POI and grid are on is part of the
     settings, and the scenario's own `.ini` carries those to a process that applies them while it
     starts. Feeding a whole canvas configuration to a *running* canvas is what `CMainWindow` never
     does either: `CMapDraw::loadMapList()` clears and rebuilds the map list and prunes the tile
     cache, with the draw threads live.
   */
  static QJsonObject viewOf(CShotContext& ctx);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  /// @brief One input of the running application: which widget, where it is and what is in it
  struct input_t {
    QWidget* widget = nullptr;
    QString address;
    QString property;
    QVariant value;
  };

  /// @brief Take the state every later diff is measured against
  void snapshot();

  /// @brief Append what changed since the last snapshot, then snapshot again
  void captureChanges();

  /// @return Every input of the main window that a shot can drive
  QList<input_t> inputsOf() const;

  /// @return true when the widget belongs to the panel, which is not the application
  bool isIgnored(const QWidget* widget) const;

  /// @brief Drop the click that opened this item's options, because a later one closed them again
  void forgetClickOn(const QString& item);

  /// @brief Drop the step that opened this item's details, because the writer closed them again
  void forgetDetailsOn(const QString& item);

  /// @brief The workspace items expanded right now, by name path
  QSet<QString> expandedNow() const;

  /**
     @brief The workspace items whose details are a page of the window right now, by name path.

     A track's and a project's details are not dialogs, they are pages the main window keeps until
     they are closed - so they are state, and a picture of one has to be able to say it wants them.
     What opens them is a press, which is exactly what this recorder does not watch, so the page
     they add is found here instead.
   */
  QSet<QString> detailsNow() const;

  /// @return The workspace item's name path, empty when nothing is selected
  QString selectionNow() const;

  CShotContext& ctx;
  QJsonArray actions;
  bool recording = false;
  /// The capture runs from a queued call, so the widget has already handled the click it answers
  bool capturePending = false;
  QWidget* ignored = nullptr;

  QString lastSelection;
  QSet<QString> lastExpanded;
  QSet<QString> lastDetails;
  QHash<QString, QVariant> lastInputs;

  /// What the writer pressed on. An input that changed without being pressed was changed by the
  /// application, and driving that back is driving an output.
  QPointer<QWidget> pressWidget;

  /// Where the writer pressed on the map, and what was under it - read before the application
  /// handles the press, because handling it is what changes the answer
  bool pressedOnCanvas = false;
  QPoint pressPixel;
  QPointF pressCoord;
  QString pressItem;
};

#endif  // CSHOTRECORDER_H
