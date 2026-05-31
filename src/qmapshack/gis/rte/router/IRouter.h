/**********************************************************************************************
    Copyright (C) 2014 Oliver Eichler <oliver.eichler@gmx.de>

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

#ifndef IROUTER_H
#define IROUTER_H

#include <QWidget>
#include <functional>

#include "gis/IGisItem.h"

// Callback invoked on the main thread when an async segment route completes.
// result >= 0: success (number of coords); result < 0: failure or cancellation.
using RouteCallback = std::function<void(int result, QPolygonF coords)>;

class IRouter : public QWidget {
  Q_OBJECT
 public:
  IRouter(bool fastRouting, QWidget* parent);
  virtual ~IRouter();

  virtual void calcRoute(const IGisItem::key_t& key) = 0;
  virtual int calcRoute(const QPointF& p1, const QPointF& p2, QPolygonF& coords, qreal* costs = nullptr) = 0;

  // Async variant used by ILineOp for segment routing. Calls callback on the
  // main thread when done. Default implementation falls back to the sync path.
  virtual void calcRouteAsync(const QPointF& p1, const QPointF& p2, RouteCallback callback);

  virtual bool hasFastRouting() { return fastRouting; }

  virtual QString getOptions() = 0;

  virtual void routerSelected() {}

 private:
  bool fastRouting;
};

#endif  // IROUTER_H
