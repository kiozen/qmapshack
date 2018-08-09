/**********************************************************************************************
    Copyright (C) 2014 Oliver Eichler oliver.eichler@gmx.de

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

#ifndef CFILTERSPEED_H
#define CFILTERSPEED_H

#include "ui_IFilterSpeed.h"
#include <QWidget>

class CGisItemTrk;
class CFilterSpeedConst;
class CFilterSpeedCycle;
class CFilterSpeedHike;

class CFilterSpeed : public QWidget, private Ui::IFilterSpeed
{
    Q_OBJECT
public:
    CFilterSpeed(CGisItemTrk& trk, QWidget * parent);
    virtual ~CFilterSpeed();

    void updateUi();

    struct cycling_type_t
    {
        QString name;
        qreal plainSpeed;
        qreal minSpeed;
        qreal slopeAtMinSpeed;
        qreal maxSpeed;
        qreal slopeAtMaxSpeed;
    };

private slots:
    void slotApply();

    void slotSetActivityType(int type);
    void slotSetCyclingType(int type);
    void slotSetPlainSpeed(double speed);
    void slotSetMinSpeed(double speed);
    void slotSetSlopeAtMinSpeed(double speed);
    void slotSetMaxSpeed(double speed);
    void slotSetSlopeAtMaxSpeed(double slope);
    void slotSetMinMaxSlopes(bool);

private:
    CGisItemTrk& trk;
    const qint32 noOfFixTypes;
    const qint32 noOfCustomTypes;

    QList <cycling_type_t> cyclingTypes;

    CFilterSpeedConst * filterConst;
    CFilterSpeedCycle * filterCycle;
    CFilterSpeedHike  * filterHike;
};

#endif //CFILTERSPEED_H

