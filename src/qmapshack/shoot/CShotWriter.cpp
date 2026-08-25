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

#include "shoot/CShotWriter.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonObject>
#include <QLayout>
#include <QRegion>
#include <QWidget>

#include "canvas/CCanvas.h"

namespace {
/**
   @brief Render a widget at one device pixel per logical pixel, whatever screen it is on.

   `QWidget::grab()` renders at the widget's own device pixel ratio, so a writer on a HiDPI screen
   accepted a picture N times the size the headless build produces and one chapter had two sizes on
   two machines. Qt 6 offers no way to put a real screen back to a ratio of 1, so the picture is
   rendered into an image that carries the ratio instead: widgets paint in logical coordinates and
   the paint device decides what a logical pixel costs.

   Exact for a dialog, a menu or a docker. A canvas is the one thing that is not: its layers are
   drawn into buffers sized by the *widget's* ratio (see IDrawContext), so on a HiDPI screen the
   map arrives here already rendered at that ratio and lands in the picture downscaled. Same size,
   same layout, resampled tiles - and a headless run, where the ratio is 1 throughout, is
   unaffected.
 */
QImage renderAtDpr1(QWidget* w) {
  QImage img(w->size(), QImage::Format_ARGB32_Premultiplied);
  img.setDevicePixelRatio(1.0);
  // What grab() does for a widget that does not paint its whole rect itself.
  img.fill(Qt::transparent);
  w->render(&img, QPoint(), QRegion(), QWidget::DrawWindowBackground | QWidget::DrawChildren);

  // And what it does for one that does: hand back an opaque picture. grab() takes the widget's
  // word for it; here the pixels are asked instead, because they are already in hand. An alpha
  // channel no pixel is transparent in costs a third of the PNG, and every picture ships in the
  // .qch that every user downloads.
  for (int y = 0; y < img.height(); y++) {
    const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < img.width(); x++) {
      if (255 != qAlpha(line[x])) {
        return img;
      }
    }
  }
  return img.convertToFormat(QImage::Format_RGB32);
}
}  // namespace

CShotWriter::CShotWriter(const QDir& outDir, const QString& lang) : outDir(outDir), lang(lang) {}

void CShotWriter::settle(QWidget* w) {
  if (nullptr == w) {
    return;
  }
  w->ensurePolished();
  // Layouts activate from a queued invocation, so one pass is not enough for nested forms.
  for (int i = 0; i < 3; i++) {
    if (nullptr != w->layout()) {
      w->layout()->activate();
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }
}

void CShotWriter::settleStable(QWidget* w, int timeoutMs) {
  if (nullptr == w) {
    return;
  }
  // Wait for events rather than spinning on them: what this waits for arrives over the network.
  constexpr int kStepMs = 250;

  QElapsedTimer timer;
  timer.start();
  QImage last;
  while (timer.elapsed() < timeoutMs) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, kStepMs);
    const QImage& now = renderAtDpr1(w);
    if (now == last) {
      return;
    }
    last = now;
  }
  qWarning() << "shoot: still changing after" << timeoutMs << "ms; is the map reachable?";
}

QImage CShotWriter::render(QWidget* w, const QSize& size) {
  if (nullptr == w) {
    return {};
  }

  settle(w);
  if (size.isValid()) {
    w->resize(size);
  } else {
    // Never adjustSize(): setupUi() applies the Designer geometry, which is usually larger than the
    // sizeHint, and shrinking to the hint clips the form. Grow to the hint, never below the size
    // the widget already chose. A QMenu starts at the default 100x30 and so gets its hint.
    w->resize(w->size().expandedTo(w->sizeHint()));
  }
  settle(w);

  // A map keeps arriving after the layout has settled - its tiles come over the network, and the
  // resize above starts a fresh load for the new size. Nothing else in a picture changes on its
  // own, so only a widget that is or holds a canvas pays for the wait.
  if (nullptr != qobject_cast<CCanvas*>(w) || nullptr != w->findChild<CCanvas*>()) {
    settleStable(w);
  }

  return renderAtDpr1(w);
}

QString CShotWriter::pathFor(const QString& stem) const {
  // English is the base name Sphinx falls back to, so it carries no suffix.
  const QString& suffix = (lang == "en") ? QString(".png") : ("." + lang + ".png");
  return outDir.absoluteFilePath(stem + suffix);
}

QString CShotWriter::write(const QImage& img, const QString& stem) {
  if (img.isNull()) {
    qWarning() << "shoot: empty image for" << stem;
    failures_++;
    return {};
  }

  const QString& path = pathFor(stem);
  // A recipe id may carry a directory part; the flat tree is the language convention, not a ban
  // on grouping.
  QDir().mkpath(QFileInfo(path).absolutePath());

  if (!img.save(path, "PNG")) {
    qWarning() << "shoot: failed to write" << path;
    failures_++;
    return {};
  }

  QJsonObject entry;
  entry["id"] = stem;
  entry["lang"] = lang;
  entry["file"] = outDir.relativeFilePath(path);
  entry["width"] = img.width();
  entry["height"] = img.height();
  manifest_.append(entry);

  qDebug() << "shoot:" << outDir.relativeFilePath(path) << img.size();
  return path;
}
