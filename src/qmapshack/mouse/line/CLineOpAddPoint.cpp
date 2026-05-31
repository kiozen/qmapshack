/**********************************************************************************************
    Copyright (C) 2014-2015 Oliver Eichler <oliver.eichler@gmx.de>
    Copyright (C) 2018 Norbert Truchsess <norbert.truchsess@t-online.de>

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

#include "mouse/line/CLineOpAddPoint.h"

#include <QtWidgets>

#include "canvas/CCanvas.h"
#include "gis/CGisDraw.h"
#include "mouse/line/IMouseEditLine.h"

CLineOpAddPoint::CLineOpAddPoint(SGisLine& points, CGisDraw* gis, CCanvas* canvas, IMouseEditLine* parent)
    : ILineOp(points, gis, canvas, parent) {
  cursor = QCursor(QPixmap(":/cursors/cursorPointAdd.png"), 0, 0);
}

CLineOpAddPoint::~CLineOpAddPoint() {}

void CLineOpAddPoint::append() {
  // this is called on construction when creating a complete new line
  // A new point is appended to what ever line already exists,
  // and add point mode is entered immediately.
  idxFocus = points.size();
  points.insert(idxFocus, IGisLine::point_t(points.last()));
  addPoint = true;
  isPoint = true;
  // make sure that when starting the line-edit on-the-fly-routing will
  // not trigger before the mouse has been moved a bit away from last point of line
  startMouseMove(points.last().pixel);
}

bool CLineOpAddPoint::abortStep() {
  if (addPoint) {
    cancelDelayedRouting();
    ++routingGeneration;  // discard any in-flight routing callbacks
    isRouting = false;
    parentHandler->restoreFromHistory(points);

    addPoint = false;
    idxFocus = NOIDX;

    canvas->slotTriggerCompleteUpdate(CCanvas::eRedrawMouse);

    return true;
  }
  return false;
}

void CLineOpAddPoint::leftClick(const QPoint& pos) {
  if (idxFocus == NOIDX || isRouting) {
    return;
  }

  if (addPoint) {
    QPointF coord = pos;
    gis->convertPx2Rad(coord);

    // Fix the draft point at the clicked position. mouseMove is blocked while
    // isRouting, so the coord won't drift during async routing (issue #1093).
    points[idxFocus].coord = coord;

    isRouting = true;
    const bool capturedIsPoint = isPoint;
    const qint32 capturedIdx = idxFocus;

    startRouting(idxFocus, [this, coord, capturedIsPoint, capturedIdx]() {
      isRouting = false;

      // If abortStep fired during routing, routingGeneration was incremented
      // and startRouting discarded the callback — we won't reach here. But
      // check capturedIdx validity as a belt-and-suspenders guard.
      if (capturedIdx >= points.size()) {
        canvas->slotTriggerCompleteUpdate(CCanvas::eRedrawMouse);
        return;
      }

      if (capturedIsPoint) {
        qint32 newIdx = capturedIdx;
        if (newIdx == (points.size() - 1)) {
          newIdx++;
        }
        idxFocus = newIdx;

        parentHandler->storeToHistory(points);
        points.insert(idxFocus, IGisLine::point_t(coord));
      } else {
        parentHandler->storeToHistory(points);
        addPoint = false;
        idxFocus = NOIDX;
      }
      canvas->slotTriggerCompleteUpdate(CCanvas::eRedrawMouse);
    });

    canvas->slotTriggerCompleteUpdate(CCanvas::eRedrawMouse);
    return;
  }

  if (isPoint) {
    // as isPoint is set, add a new point either at the start or end of the line
    if (idxFocus == (points.size() - 1)) {
      idxFocus++;
    }

    QPointF coord = pos;
    gis->convertPx2Rad(coord);
    points.insert(idxFocus, IGisLine::point_t(coord));

    addPoint = true;
  } else if (idxFocus != NOIDX) {
    // clear current line segment
    points[idxFocus].subpts.clear();

    // add a new point to line segment
    QPointF coord = pos;
    gis->convertPx2Rad(coord);

    idxFocus++;
    points.insert(idxFocus, IGisLine::point_t(coord));

    addPoint = true;
  }
  canvas->slotTriggerCompleteUpdate(CCanvas::eRedrawMouse);
}

void CLineOpAddPoint::mouseMove(const QPoint& pos) {
  ILineOp::mouseMove(pos);
  if (addPoint && !isRouting) {
    QPointF coord = pos;
    gis->convertPx2Rad(coord);

    IGisLine::point_t& pt = points[idxFocus];
    // update position of point
    pt.coord = coord;

    // clear subpoints, as they have to be recalculated
    // by the routing, if any
    pt.subpts.clear();
    if (idxFocus > 0) {
      points[idxFocus - 1].subpts.clear();
    }

    // retrigger delayed routing
    startDelayedRouting();
  } else if (!isRouting) {
    isPoint = false;
    // find line segment close to cursor
    idxFocus = isCloseToLine(pos);
    // if none is found try to find point
    if (idxFocus == NOIDX) {
      // if no line segment is found but a point
      // it is either first or the last point in the line
      idxFocus = isCloseTo(pos);

      if ((idxFocus == 0) || (idxFocus == (points.size() - 1))) {
        isPoint = true;
      }
    }
  }
  canvas->slotTriggerCompleteUpdate(CCanvas::eRedrawMouse);
}

void CLineOpAddPoint::rightButtonDown(const QPoint& pos) {
  abortStep();
  idxFocus = NOIDX;
  canvas->slotTriggerCompleteUpdate(CCanvas::eRedrawMouse);
}

void CLineOpAddPoint::drawFg(QPainter& p) {
  if (idxFocus == NOIDX) {
    return;
  }

  if (addPoint) {
    const IGisLine::point_t& pt = points[idxFocus];
    drawSinglePointSmall(pt.pixel, p);
  } else if (isPoint) {
    const IGisLine::point_t& pt = points[idxFocus];
    drawSinglePointLarge(pt.pixel, p);
  } else if (idxFocus < (points.size() - 1)) {
    QPolygonF line;
    const IGisLine::point_t& pt1 = points[idxFocus];
    const IGisLine::point_t& pt2 = points[idxFocus + 1];

    if (pt1.subpts.isEmpty()) {
      line << pt1.pixel << pt2.pixel;
    } else {
      line << pt1.pixel;
      for (const IGisLine::subpt_t& pt : pt1.subpts) {
        line << pt.pixel;
      }
      line << pt2.pixel;
    }

    p.setPen(penBgPoint);
    p.setBrush(brushBgPoint);

    rectPoint.moveCenter(pt1.pixel.toPoint());
    p.drawRect(rectPoint);
    rectPoint.moveCenter(pt2.pixel.toPoint());
    p.drawRect(rectPoint);

    p.setPen(QPen(Qt::white, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(line);

    p.setPen(penFgPoint);
    p.setBrush(brushFgPoint);

    rectPoint.moveCenter(pt1.pixel.toPoint());
    p.drawRect(rectPoint);
    rectPoint.moveCenter(pt2.pixel.toPoint());
    p.drawRect(rectPoint);

    p.setPen(QPen(Qt::red, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(line);
  }
}
