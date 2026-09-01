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

#ifndef CQMSSTYLE_H
#define CQMSSTYLE_H

#include <QProxyStyle>

/**
   @brief Marks the checked state of toggle tool buttons and menu items.

   Every style draws that state as a small change in shading, which carries almost no contrast on a
   dark palette. Painting it here rather than through a style sheet means it resolves at paint time:
   it follows the palette with nothing to re-apply, cannot go stale, and no widget has to opt in.
 */
class CQmsStyle : public QProxyStyle {
 public:
  /** @brief Wrap the active style, keeping whatever -style or QT_STYLE_OVERRIDE selected. */
  static void install();

  /**
     @brief Answer the hints a platform theme owns from a fixed value instead.

     A documentation run must render the same on screen and off it, and the platform theme is the
     whole difference: the offscreen platform has none, a desktop always has one. Measured with Qt
     6.10.2, Fusion, the generic theme against no theme: of 130 style hints, every class font, the
     palette and the standard icons, only SH_DialogButtonBox_ButtonsHaveIcons differs.
   */
  static void pinThemeIndependentHints();

  void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override;

  void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                   const QWidget* widget) const override;

  int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                QStyleHintReturn* returnData) const override;

 private:
  explicit CQmsStyle(QStyle* base);
};

#endif  // CQMSSTYLE_H
