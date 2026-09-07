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

#include "shoot/CShotContext.h"

#include <QDebug>
#include <QImage>
#include <QWidget>

#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "gis/CGisListWks.h"
#include "gis/CGisWorkspace.h"
#include "gis/ovl/CGisItemOvlArea.h"
#include "gis/prj/IGisProject.h"
#include "gis/rte/CGisItemRte.h"
#include "gis/trk/CGisItemTrk.h"
#include "gis/wpt/CGisItemWpt.h"
#include "shoot/CShotWriter.h"

CShotContext::CShotContext(CShotWriter& writer, const QString& lang) : writer(writer), lang_(lang) {}

CMainWindow* CShotContext::mainWindow() const { return CMainWindow::isNull() ? nullptr : &CMainWindow::self(); }

QWidget* CShotContext::parent() const { return CMainWindow::getBestWidgetForParent(); }

CCanvas* CShotContext::canvas() const {
  CMainWindow* w = mainWindow();
  return nullptr == w ? nullptr : w->getVisibleCanvas();
}

CGisWorkspace* CShotContext::workspace() const { return &CGisWorkspace::self(); }

CGisListWks* CShotContext::wksList() const { return &CGisWorkspace::self().getWksList(); }

QList<IGisItem::key_t> CShotContext::keys() const {
  QList<IGisItem::key_t> keys;
  for (IGisItem* item : {static_cast<IGisItem*>(trk_), static_cast<IGisItem*>(wpt_), static_cast<IGisItem*>(rte_),
                         static_cast<IGisItem*>(area_)}) {
    if (nullptr != item) {
      keys << item->getKey();
    }
  }
  return keys;
}

void CShotContext::setFixture(IGisProject* project, CGisItemTrk* trk, CGisItemWpt* wpt, CGisItemRte* rte,
                              CGisItemOvlArea* area) {
  project_ = project;
  trk_ = trk;
  wpt_ = wpt;
  rte_ = rte;
  area_ = area;
}

void CShotContext::beginRecipe(const QString& id) {
  recipeId = id;
  frameNo = 0;
}

QString CShotContext::stemFor(const QString& variant) const {
  return variant.isEmpty() ? recipeId : (recipeId + "-" + variant);
}

QImage CShotContext::cropped(const QImage& img) {
  if (!crop.isValid()) {
    return img;
  }
  if (!img.rect().contains(crop)) {
    // The whole picture, and a failure: a rectangle measured in one arrangement and cut out of
    // another frames nothing anyone chose. Silence here is what makes that look like it worked.
    qWarning() << "shoot:" << recipeId << "region" << crop << "is not inside" << img.rect();
    cropMiss = true;
    return img;
  }
  return img.copy(crop);
}

void CShotContext::shot(QWidget* w, const QSize& size, const QString& variant) {
  writer.write(cropped(CShotWriter::render(w, size)), stemFor(variant));
}

void CShotContext::shot(const QImage& img, const QString& variant) { writer.write(cropped(img), stemFor(variant)); }

void CShotContext::frame(QWidget* w, const QSize& size) { frame(CShotWriter::render(w, size)); }

void CShotContext::frame(const QImage& img) {
  writer.write(cropped(img), QString("%1.%2").arg(recipeId).arg(frameNo++, 4, 10, QChar('0')));
}
