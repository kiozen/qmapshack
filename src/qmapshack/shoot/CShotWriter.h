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

#ifndef CSHOTWRITER_H
#define CSHOTWRITER_H

#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QString>

class QWidget;

/**
   @brief Renders a widget and writes the result to a stable path.

   Output is `<id>.<lang>.png` in one flat tree, which is what Sphinx' figure_language_filename
   picks up. English is emitted unsuffixed because that is the base name a page references.
 */
class CShotWriter {
 public:
  CShotWriter(const QDir& outDir, const QString& lang);

  /// @brief Spin the event loop until a freshly built widget has a settled layout
  static void settle(QWidget* w);

  /**
     @brief Spin until the widget renders the same picture twice.

     The tiles of an online map arrive over the network, so a canvas shot taken as soon as the draw
     threads are idle is half drawn. Warns and returns when the timeout runs out.
   */
  static void settleStable(QWidget* w, int timeoutMs = 20000);

  /**
     @brief Polish, size and render a widget with no screen involved.

     Always at a device pixel ratio of 1, so the picture a writer accepts on a HiDPI screen is the
     one a headless build reproduces.

     @param size   explicit size, or an invalid size to use the widget's own sizeHint
   */
  static QImage render(QWidget* w, const QSize& size);

  /// @return The path written, or an empty string on failure
  QString write(const QImage& img, const QString& stem);

  const QJsonArray& manifest() const { return manifest_; }

  int failures() const { return failures_; }

 private:
  QString pathFor(const QString& stem) const;

  QDir outDir;
  QString lang;
  QJsonArray manifest_;
  int failures_ = 0;
};

#endif  // CSHOTWRITER_H
