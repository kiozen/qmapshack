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

#include "shoot/CShotRegistry.h"

#include <QDebug>
#include <QWidget>

CShotRegistry& CShotRegistry::self() {
  // Function-local static: the tables are filled from other translation units' static initializers,
  // so a file-scope instance would be an initialization order race.
  static CShotRegistry instance;
  return instance;
}

bool CShotRegistry::addExposure(const QString& id, const std::type_info* type, const QString& className,
                                exposure_factory_t factory, const QString& description) {
  if (exposures.contains(id)) {
    qWarning() << "shoot: duplicate exposure id" << id;
    return false;
  }
  exposures.insert(id, {factory, description, type, className});
  return true;
}

QStringList CShotRegistry::exposureIds() const { return exposures.keys(); }

QString CShotRegistry::exposureDescription(const QString& id) const { return exposures.value(id).description; }

QString CShotRegistry::exposureForWidget(const QWidget* widget) const {
  if (nullptr == widget) {
    return {};
  }
  for (auto it = exposures.constBegin(); it != exposures.constEnd(); ++it) {
    if (nullptr != it.value().type && *it.value().type == typeid(*widget)) {
      return it.key();
    }
  }
  return {};
}

QString CShotRegistry::exposureClassName(const QString& id) const { return exposures.value(id).className; }

QWidget* CShotRegistry::buildExposure(const QString& id, CShotContext& ctx, QWidget* parent) const {
  const entry_t& entry = exposures.value(id);
  return entry.exposure ? entry.exposure(ctx, parent) : nullptr;
}
