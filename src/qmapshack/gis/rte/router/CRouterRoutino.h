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

#ifndef CROUTERROUTINO_H
#define CROUTERROUTINO_H

#include <routino.h>

#include <QPoint>
#include <atomic>

#include "gis/rte/router/IRouter.h"
#include "ui_IRouterRoutino.h"

class CProgressDialog;

class CRouterRoutino : public IRouter, private Ui::IRouterRoutino {
  Q_OBJECT
 public:
  CRouterRoutino(QWidget* parent);
  static CRouterRoutino& self() { return *pSelf; }

  void calcRoute(const IGisItem::key_t& key) override;
  int calcRoute(const QPointF& p1, const QPointF& p2, QPolygonF& coords, qreal* costs) override;
  void calcRouteAsync(const QPointF& p1, const QPointF& p2, RouteCallback callback) override;

  bool hasFastRouting() override;

  QString getOptions() override;

  // Written by ProgressFunc (worker thread), read by main-thread poll timer.
  static std::atomic<int> progressValue;
  // Set by progress dialog reject (main thread), read by ProgressFunc (worker thread).
  static std::atomic<bool> cancelRequested;

  void setupPath(const QString& path);

 private slots:
  void slotSetupPaths();

 private:
  virtual ~CRouterRoutino();
  void buildDatabaseList();
  void freeDatabaseList();
  int loadProfiles(const QString& profilesPath);
  void updateHelpText();
  QString xlateRoutinoError(int err);
  static CRouterRoutino* pSelf;

  QStringList dbPaths;
  QString currentProfilesPath;

  QMutex mutex;
};

#endif  // CROUTERROUTINO_H
