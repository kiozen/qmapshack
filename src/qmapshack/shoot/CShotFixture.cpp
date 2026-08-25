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

#include "shoot/CShotFixture.h"

#include <QDateTime>
#include <QDebug>
#include <QPolygonF>
#include <QTimeZone>
#include <QtMath>

#include "CMainWindow.h"
#include "gis/CGisListWks.h"
#include "gis/IGisLine.h"
#include "gis/ovl/CGisItemOvlArea.h"
#include "gis/qms/CQmsProject.h"
#include "gis/rte/CGisItemRte.h"
#include "gis/trk/CGisItemTrk.h"
#include "gis/trk/CTrackData.h"
#include "gis/wpt/CGisItemWpt.h"
#include "map/CMapItem.h"
#include "map/CMapList.h"
#include "shoot/CShotContext.h"

namespace {
/// The map every shot is taken on; shots.py points the map path at the directory holding it.
const QString kMapFile = "osm.tms";

/// Fixed so a shot never depends on the wall clock.
const QDateTime kEpoch = QDateTime(QDate(2026, 5, 17), QTime(8, 0, 0), QTimeZone::UTC);

constexpr qreal kLon = 11.0;
constexpr qreal kLat = 47.5;

/// @brief A short synthetic track with elevation and timestamps around the fixture location
CTrackData demoTrackData() {
  CTrackData data;
  data.name = "Demo Track";
  data.desc = "A synthetic track for the documentation shots.";

  CTrackData::trkseg_t seg;
  constexpr int N = 120;
  for (int i = 0; i < N; i++) {
    CTrackData::trkpt_t pt;
    pt.lon = kLon + 0.02 * i / N;
    pt.lat = kLat + 0.01 * qSin(2 * M_PI * i / N);
    pt.ele = qint32(700 + 250 * qSin(M_PI * i / N) + 30 * qSin(6 * M_PI * i / N));
    pt.time = kEpoch.addSecs(i * 45);
    seg.pts << pt;
  }
  data.segs << seg;
  return data;
}

/// @brief A closed polygon in [rad], which is what SGisLine carries
SGisLine demoLine(qreal dLon, qreal dLat, bool close) {
  QPolygonF poly;
  poly << QPointF(kLon, kLat) << QPointF(kLon + dLon, kLat) << QPointF(kLon + dLon, kLat + dLat)
       << QPointF(kLon, kLat + dLat);
  if (close) {
    poly << QPointF(kLon, kLat);
  }
  for (QPointF& pt : poly) {
    pt *= DEG_TO_RAD;
  }
  return SGisLine(poly);
}

/**
   @brief Activate the fixture map.

   Not a nicety: with no map active the canvas covers itself with the welcome help, and every
   picture of the map area is that text instead of a map.
 */
void activateMap() {
  CMapList* list = CMainWindow::self().findChild<CMapList*>();
  if (nullptr == list) {
    qWarning() << "shoot: no map list";
    return;
  }
  for (int i = 0; i < list->count(); i++) {
    CMapItem* item = list->item(i);
    if (item->getFilename().endsWith(kMapFile)) {
      // The configuration can have activated it already, and activating twice is not free.
      if (!item->isActivated()) {
        item->activate(true);
      }
      return;
    }
  }
  qWarning() << "shoot:" << kMapFile << "is not in the map list; is the map path set?";
}
}  // namespace

void CShotFixture::build(CShotContext& ctx) {
  // The filename has no .qms suffix, so CQmsProject treats it as the name of a new project.
  IGisProject* project = new CQmsProject("Shoot Demo", ctx.wksList());

  CTrackData data = demoTrackData();
  CGisItemTrk* trk = new CGisItemTrk(data, project);
  trk->setActivity(CTrackData::trkpt_t::eAct20Bike);

  // This constructor takes degrees; the SGisLine below takes radians.
  CGisItemWpt* wpt = new CGisItemWpt(QPointF(kLon, kLat), 812, kEpoch, "Demo Waypoint", "Flag, Blue", project);
  wpt->setDescription("A synthetic waypoint for the documentation shots.");

  CGisItemRte* rte = new CGisItemRte(demoLine(0.03, 0.02, false), "Demo Route", project, NOIDX);
  CGisItemOvlArea* area = new CGisItemOvlArea(demoLine(0.02, 0.015, true), "Demo Area", project, NOIDX);

  ctx.setFixture(project, trk, wpt, rte, area);

  activateMap();
}
