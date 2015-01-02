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

#include "gis/trk/filter/CFilterDelete.h"
#include "gis/trk/CGisItemTrk.h"

CFilterDelete::CFilterDelete(CGisItemTrk &trk, QWidget *parent)
    : QWidget(parent)
    , trk(trk)
{
    setupUi(this);

    connect(toolApply, SIGNAL(clicked()), this, SLOT(slotApply()));
}

CFilterDelete::~CFilterDelete()
{

}

void CFilterDelete::slotApply()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    trk.filterDelete();
    QApplication::restoreOverrideCursor();
}
