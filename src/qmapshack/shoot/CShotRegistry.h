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

#ifndef CSHOTREGISTRY_H
#define CSHOTREGISTRY_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>
#include <typeinfo>

class CShotContext;
class QWidget;

/**
   @brief id -> factory for the widget classes a shot can build from nothing.

   Filled by translation-unit-local statics, so exposing a class touches one file plus the
   CMakeLists source list and never a central switch. There is no table of scenarios: a scenario is
   recorded by the writer and lives in the chapter file.
 */
class CShotRegistry {
 public:
  /// @brief Builds an exposed widget from the fixture and a parent
  using exposure_factory_t = std::function<QWidget*(CShotContext&, QWidget*)>;

  static CShotRegistry& self();

  bool addExposure(const QString& id, const std::type_info* type, const QString& className, exposure_factory_t factory,
                   const QString& description);

  /// @brief All exposure ids, sorted
  QStringList exposureIds() const;

  QString exposureDescription(const QString& id) const;

  /// @return The exposure building exactly this widget's class, or an empty string if none does
  QString exposureForWidget(const QWidget* widget) const;

  QString exposureClassName(const QString& id) const;

  /// @return nullptr if the id is unknown
  QWidget* buildExposure(const QString& id, CShotContext& ctx, QWidget* parent) const;

 private:
  CShotRegistry() = default;

  struct entry_t {
    exposure_factory_t exposure;
    QString description;
    /// Identity of the exposed class. typeid, not the meta object: three exposed dialogs have no
    /// Q_OBJECT and would all report their base class name.
    const std::type_info* type = nullptr;
    QString className;
  };

  QMap<QString, entry_t> exposures;
};

// Token pasting needs two levels to expand __COUNTER__ before joining. __COUNTER__ rather than
// __LINE__, because a macro invocation spanning several lines has no single line number.
#define SHOT_JOIN_(a, b) a##b
#define SHOT_JOIN(a, b) SHOT_JOIN_(a, b)
#define SHOT_UNIQUE(prefix) SHOT_JOIN(prefix, __COUNTER__)

/**
   @brief Teach the framework how to build one widget class from the fixture.

   TYPE is the class FACTORY returns; documentation mode looks the exposure up by it, so a rename
   moves with the class instead of silently orphaning the entry. FACTORY is a lambda
   (CShotContext&, QWidget* parent) -> QWidget*.
 */
#define SHOT_EXPOSE(ID, DESC, TYPE, FACTORY)   \
  static const bool SHOT_UNIQUE(shotExpose_) = \
      CShotRegistry::self().addExposure(ID, &typeid(TYPE), QStringLiteral(#TYPE), FACTORY, DESC)

#endif  // CSHOTREGISTRY_H
