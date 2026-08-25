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

#ifndef CSHOTCONTEXT_H
#define CSHOTCONTEXT_H

#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include "gis/IGisItem.h"

class CShotWriter;
class CMainWindow;
class CCanvas;
class CGisWorkspace;
class CGisListWks;
class IGisProject;
class CGisItemTrk;
class CGisItemWpt;
class CGisItemRte;
class CGisItemOvlArea;
class QWidget;
class QImage;

/**
   @brief The fixture a recipe shoots against, plus the way it emits images.

   Accessors are by role, not by name, so a recipe never has to know how the fixture is built.
   Stage 1 builds the fixture in memory (CShotFixture); a later stage swaps in the committed
   sample project without touching a single recipe.
 */
class CShotContext {
 public:
  CShotContext(CShotWriter& writer, const QString& lang);

  // --- the live application ---------------------------------------------------------------

  CMainWindow* mainWindow() const;
  /// @brief Parent for constructed dialogs. Never nullptr once the main window exists.
  QWidget* parent() const;
  CCanvas* canvas() const;
  CGisWorkspace* workspace() const;
  CGisListWks* wksList() const;

  // --- the fixture -------------------------------------------------------------------------

  IGisProject* project() const { return project_; }
  CGisItemTrk* trk() const { return trk_; }
  CGisItemWpt* wpt() const { return wpt_; }
  CGisItemRte* rte() const { return rte_; }
  CGisItemOvlArea* area() const { return area_; }
  QList<IGisItem::key_t> keys() const;

  void setFixture(IGisProject* project, CGisItemTrk* trk, CGisItemWpt* wpt, CGisItemRte* rte, CGisItemOvlArea* area);

  // --- the chapter's recorded scenarios ------------------------------------------------------

  /**
     @brief The scenarios the writer performed, by name.

     A recorded scenario is chapter data, not a registered recipe, so the shot that references one
     can only find it through the chapter it belongs to. Set from the chapter file before its shots
     are taken; empty for a build that has none.
   */
  void setScenarios(const QJsonObject& recorded) { scenarios_ = recorded; }
  const QJsonObject& scenarios() const { return scenarios_; }

  // --- emitting ----------------------------------------------------------------------------

  const QString& lang() const { return lang_; }

  /// @brief Called by the runner before each recipe; resets the frame counter
  void beginRecipe(const QString& id);

  /**
     @brief Cut this rectangle out of everything emitted until it is cleared.

     A rectangle is the writer's answer to "not this widget, that part of the picture". It composes
     with what produced the picture - a widget, an exposure or a scenario - because the scenario is
     what puts the application into the state the rectangle frames.

     @param rect  in the coordinates of the emitted image; an invalid rectangle emits it whole
   */
  void setCrop(const QRect& rect) {
    crop = rect;
    cropMiss = false;
  }

  /// @return true when a crop was set that did not fit the picture, so the whole one was emitted
  bool cropMissed() const { return cropMiss; }

  /**
     @brief Leave the state a scenario builds standing instead of clearing it again.

     Documentation mode sets this so the writer can drag a rectangle over what the scenario put on
     screen. A build never does: there, one shot must not leave anything behind for the next.
   */
  void setHold(bool yes) { hold_ = yes; }
  bool holding() const { return hold_; }

  /// @return Where the last emitted image sits in the main window, empty when it was no widget of it
  const QRect& lastArea() const { return lastArea_; }

  /**
     @brief Emit one still.

     @param variant  appended to the recipe id as `<id>-<variant>`, for a recipe emitting several
                     labelled images
   */
  void shot(QWidget* w, const QSize& size = {}, const QString& variant = QString());
  void shot(const QImage& img, const QString& variant = QString());

  /// @brief Emit the next image of a numbered sequence, `<id>.0000`, `<id>.0001`, ...
  void frame(QWidget* w, const QSize& size = {});
  void frame(const QImage& img);

 private:
  QString stemFor(const QString& variant) const;

  /// @return @p img cut down to the crop, or unchanged when there is none
  QImage cropped(const QImage& img);

  /// @brief Remember where @p w's picture sits in the main window, so a rectangle dragged over the
  ///        window can be turned into one of the picture
  void noteArea(const QWidget* w, const QSize& size);

  CShotWriter& writer;
  QString lang_;
  QJsonObject scenarios_;
  QString recipeId;
  int frameNo = 0;
  QRect crop;
  bool cropMiss = false;
  bool hold_ = false;
  QRect lastArea_;

  IGisProject* project_ = nullptr;
  CGisItemTrk* trk_ = nullptr;
  CGisItemWpt* wpt_ = nullptr;
  CGisItemRte* rte_ = nullptr;
  CGisItemOvlArea* area_ = nullptr;
};

#endif  // CSHOTCONTEXT_H
