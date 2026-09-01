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

#include "shoot/CShotDocMode.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QCursor>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMetaType>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <utility>

#include "CMainWindow.h"
#include "gis/CGisListWks.h"
#include "gis/prj/IGisProject.h"
#include "helpers/CSettings.h"
#include "shoot/CShotChapter.h"
#include "shoot/CShotContext.h"
#include "shoot/CShotDocPanel.h"
#include "shoot/CShotFixture.h"
#include "shoot/CShotRecorder.h"
#include "shoot/CShotRegistry.h"
#include "shoot/CShotWriter.h"

namespace {
/**
   The documentation key. Ctrl+Shift+F9, because documentation mode swallows what it binds and a
   bare function key would take it away from the application - F1, F8 and F11 are bound there
   already, and every one of them is user configurable.
 */
constexpr Qt::KeyboardModifiers kModifiers = Qt::ControlModifier | Qt::ShiftModifier;
/// The only key. Everything else the writer does is a button on the panel, which the mouse is
/// free to reach - it is not busy pointing at the picture.
constexpr Qt::Key kTagKey = Qt::Key_F9;

/**
   @brief A pane over the main window the writer drags a rectangle on.

   A child widget on top takes the mouse events by itself - nothing has to be redirected. It paints
   only the dimmed surround, so the application stays visible underneath, and it is gone before
   anything is photographed.
 */
class CShotRegionPicker : public QWidget {
 public:
  explicit CShotRegionPicker(QWidget* parent) : QWidget(parent) {
    setGeometry(parent->rect());
    // No background of its own: what it does not paint is the application, still on screen.
    setAttribute(Qt::WA_NoSystemBackground);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    show();
    raise();
    setFocus();
  }

  /// @return The rectangle in the parent's coordinates, empty if the writer cancelled
  QRect pick() {
    loop.exec();
    return region.normalized();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const QRect& chosen = region.normalized();
    const QColor dim(0, 0, 0, 90);
    // Four rectangles around the selection rather than one over everything: the writer has to see
    // what they are framing.
    p.fillRect(QRect(0, 0, width(), chosen.top()), dim);
    p.fillRect(QRect(0, chosen.bottom() + 1, width(), height() - chosen.bottom() - 1), dim);
    p.fillRect(QRect(0, chosen.top(), chosen.left(), chosen.height()), dim);
    p.fillRect(QRect(chosen.right() + 1, chosen.top(), width() - chosen.right() - 1, chosen.height()), dim);
    if (!chosen.isEmpty()) {
      p.setPen(QPen(Qt::white, 1, Qt::DashLine));
      p.drawRect(chosen.adjusted(0, 0, -1, -1));
    }
  }

  void mousePressEvent(QMouseEvent* e) override {
    origin = e->pos();
    region = QRect(origin, QSize());
    update();
  }

  void mouseMoveEvent(QMouseEvent* e) override {
    if (origin.isNull()) {
      return;
    }
    region = QRect(origin, e->pos());
    update();
  }

  void mouseReleaseEvent(QMouseEvent*) override { loop.quit(); }

  void keyPressEvent(QKeyEvent* e) override {
    if (Qt::Key_Escape == e->key()) {
      region = QRect();
      loop.quit();
    }
  }

 private:
  QEventLoop loop;
  QPoint origin;
  QRect region;
};

/**
   @brief What a scenario produces, shown so the writer can recognise it in the list.

   Only looked at: a rectangle is dragged over the window itself, after the scenario has put what
   it shows there.
 */
class CShotPreviewLabel : public QLabel {
 public:
  explicit CShotPreviewLabel(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setFrameShape(QFrame::StyledPanel);
    setWordWrap(true);
  }

  void setImage(const QImage& img) {
    image = img;
    setText(QString());
    update();
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    QLabel::paintEvent(event);
    if (image.isNull()) {
      return;
    }
    QRect box(QPoint(), image.size().scaled(contentsRect().size(), Qt::KeepAspectRatio));
    box.moveCenter(contentsRect().center());
    QPainter p(this);
    p.drawImage(box, image);
  }

 private:
  QImage image;
};

/// Every chapter's configuration starts from this one, and stores only what it changes.
const QString kBaseConfig = "doc/shots/fixture/shots.ini";

/**
   @brief Does the value name a place on this machine?

   Such a key cannot be committed: it is only right here. `shots.py` injects the fixture's own paths
   when it composes a run, and the map, DEM and POI lists rebuild themselves from those.
 */
bool namesAPlace(const QVariant& value, const QString& path) {
  if (path.isEmpty() || QMetaType::QByteArray == value.typeId()) {
    return false;
  }
  if (QMetaType::QStringList == value.typeId()) {
    const QStringList& entries = value.toStringList();
    for (const QString& entry : entries) {
      if (entry.contains(path)) {
        return true;
      }
    }
    return false;
  }
  return value.toString().contains(path);
}

/**
   @brief Is the widget part of the running application?

   Not QWidget::isAncestorOf(): that stops at the first window boundary, and an open menu carries
   Qt::Popup, so every menu would count as foreign.
 */
bool isPartOf(const QWidget* main, const QWidget* widget) {
  for (const QWidget* w = widget; nullptr != w; w = w->parentWidget()) {
    if (w == main) {
      return true;
    }
  }
  return false;
}

/// @brief `Demo Track` -> `demo-track`, so a recording is named after what it is about
QString slug(const QString& text) {
  QString out;
  for (const QChar c : text) {
    if (c.isLetterOrNumber()) {
      out += c.toLower();
    } else if (!out.isEmpty() && !out.endsWith('-')) {
      out += '-';
    }
  }
  while (out.endsWith('-')) {
    out.chop(1);
  }
  return out;
}

}  // namespace

CShotDocMode::CShotDocMode(const QDir& repo, const QString& chapter, QObject* parent)
    : QObject(parent), chapter(chapter.isEmpty() ? "scratch" : chapter), repo(repo) {
  writer = new CShotWriter(QDir(repo.absoluteFilePath("doc/images")), "en");
  ctx = new CShotContext(*writer, "en");
  recorder = new CShotRecorder(*ctx, this);
  qApp->installEventFilter(this);
}

CShotDocMode::~CShotDocMode() {
  delete ctx;
  delete writer;
}

void CShotDocMode::start() { QTimer::singleShot(0, this, &CShotDocMode::slotBuildFixture); }

void CShotDocMode::slotBuildFixture() {
  // CMainWindow defers slotLateInit() and slotSanityTest() by 100 ms; the fixture must not land in
  // front of them.
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 300) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
  }

  CShotFixture::build(*ctx);

  // Before anything is replayed on top: this is what the (base) row puts back.
  baseState = QJsonArray{CShotRecorder::layoutOf(*ctx), CShotRecorder::viewOf(*ctx)};

  // Parented on the main window, not ctx->parent(): that one is the visible canvas, a page inside
  // the central tab widget.
  panel = new CShotDocPanel(chapter, CMainWindow::isNull() ? nullptr : &CMainWindow::self());
  panel->setPickedHandler([this](const QString& id) { showShot(id); });
  panel->setScenarioPickedHandler([this](const QString& name) {
    selectedScenario = name;
    showScenario(name);
  });
  panel->setRecordHandler([this]() { toggleRecording(); });
  panel->setRenameHandler([this]() { renameScenario(); });
  panel->setDeleteScenarioHandler([this]() { deleteScenario(); });
  panel->setRebindHandler([this](const QString& id, const QString& scenario) { rebindShot(id, scenario); });
  panel->setTakeRegionHandler([this]() { takeRegion(); });
  panel->setReapHandler([this]() { reapUnused(); });
  panel->setRetakeHandler([this]() { retakeChapter(); });
  panel->setStoreLayoutHandler([this]() { updateScenario(); });
  // What the writer does to the panel is not part of a scenario.
  recorder->setIgnored(panel);
  panel->show();
  panel->raise();
  refreshPanel(tr("Ready."));
  qInfo() << "doc: panel" << panel->geometry() << "visible" << panel->isVisible() << "chapter" << chapter;
}

bool CShotDocMode::eventFilter(QObject* watched, QEvent* event) {
  // The panel is a window of its own and would outlive the main window, keeping the process alive
  // with nothing to look at. It goes when the application does.
  if (QEvent::Close == event->type() && nullptr != panel && !CMainWindow::isNull() && watched == &CMainWindow::self()) {
    panel->hide();
    panel->deleteLater();
    panel = nullptr;
  }

  if (QEvent::KeyPress == event->type() && !tagging) {
    const QKeyEvent* key = static_cast<QKeyEvent*>(event);
    if (kModifiers !=
        (key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))) {
      return QObject::eventFilter(watched, event);
    }
    if (kTagKey == key->key()) {
      // Not while a scenario is being recorded: taking a picture drives the application itself,
      // and every step of that would land in the recording as something the writer performed.
      if (nullptr != recorder && recorder->isRecording()) {
        refreshPanel(tr("Stop the recording first - a picture cannot be taken while one runs."));
        return true;
      }
      tagging = true;
      tag();
      tagging = false;
      return true;
    }
  }
  return QObject::eventFilter(watched, event);
}

void CShotDocMode::refreshPanel(const QString& status) {
  if (nullptr == panel) {
    return;
  }

  // What the page asks for. A picture the page uses but the chapter does not know is the one the
  // writer still has to take.
  const QString& pagePath = "doc/pages/" + chapter + ".md";
  const bool hasPage = QFileInfo::exists(repo.absoluteFilePath(pagePath));
  panel->setPage(pagePath, hasPage);
  const QSet<QString>& referenced = pageReferences();

  // The scenarios first: every picture's row carries a combo box built out of this list.
  panel->setScenarios(CShotChapter::scenarioNames(chapterPath()), selectedScenario);

  QList<CShotDocPanel::entry_t> entries;
  QSet<QString> known;

  QFile file(chapterPath());
  if (file.open(QIODevice::ReadOnly)) {
    const QJsonArray& shots = QJsonDocument::fromJson(file.readAll()).object()["shots"].toArray();
    for (const QJsonValue& value : shots) {
      const QJsonObject& shot = value.toObject();

      CShotDocPanel::entry_t entry;
      entry.id = shot["id"].toString();
      entry.scenario = shot["scenario"].toString();
      entry.note = shot["note"].toString();

      const QString& path = imagePath(entry.id);
      const bool exists = QFileInfo::exists(path);
      entry.imagePath = exists ? path : QString();

      if (!referenced.contains(entry.id)) {
        entry.state = CShotDocPanel::eNotUsed;
      } else if (!exists) {
        entry.state = CShotDocPanel::eNoImage;
      } else {
        entry.state = CShotDocPanel::eTaken;
      }

      entry.changed = changedShots.contains(entry.id);
      known << entry.id;
      entries << entry;
    }
  }

  QStringList missing = QStringList(referenced.begin(), referenced.end());
  missing.sort();
  for (const QString& id : std::as_const(missing)) {
    if (known.contains(id)) {
      continue;
    }
    CShotDocPanel::entry_t entry;
    entry.id = id;
    entry.note = tr("the page uses it, the chapter does not have it");
    entry.state = CShotDocPanel::eMissing;
    entries << entry;
  }

  panel->setShots(entries);

  // What just happened wins: it is the answer to what the writer did, and it is the only place a
  // step of theirs is acknowledged at all. A missing page is the reason an empty list is empty and
  // is worth saying, but only when there is nothing more recent to say.
  if (!status.isEmpty()) {
    panel->setStatus(status);
  } else if (!hasPage) {
    panel->setStatus(tr("Write %1 first and put an image line where you want a picture. Its names "
                        "are what appears here.")
                         .arg(pagePath));
  } else if (entries.isEmpty()) {
    panel->setStatus(tr("%1 references no picture yet. Add an image line to it.").arg(pagePath));
  }
}

QWidget* CShotDocMode::activeTarget() {
  // An open menu is a popup, not the active window, and it is a documentation image in its own
  // right - so it is asked for first.
  if (QWidget* popup = QApplication::activePopupWidget(); nullptr != popup) {
    return popup;
  }
  if (QWidget* modal = QApplication::activeModalWidget(); nullptr != modal) {
    return modal;
  }
  if (QWidget* active = QApplication::activeWindow(); nullptr != active) {
    return active;
  }
  QWidget* focus = QApplication::focusWidget();
  return nullptr == focus ? nullptr : focus->window();
}

QSet<QString> CShotDocMode::pageReferences() const {
  QSet<QString> referenced;
  QFile page(repo.absoluteFilePath("doc/pages/" + chapter + ".md"));
  if (page.open(QIODevice::ReadOnly | QIODevice::Text)) {
    static const QRegularExpression pattern("images/([\\w./-]+)\\.png");
    QRegularExpressionMatchIterator matches = pattern.globalMatch(QString::fromUtf8(page.readAll()));
    while (matches.hasNext()) {
      referenced << matches.next().captured(1);
    }
  }
  return referenced;
}

QSet<QString> CShotDocMode::chapterIds() const {
  QSet<QString> ids;
  QFile file(chapterPath());
  if (file.open(QIODevice::ReadOnly)) {
    const QJsonArray& shots = QJsonDocument::fromJson(file.readAll()).object()["shots"].toArray();
    for (const QJsonValue& value : shots) {
      ids << value.toObject()["id"].toString();
    }
  }
  return ids;
}

QJsonObject CShotDocMode::shotOf(const QString& id) const {
  QFile file(chapterPath());
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QJsonArray& shots = QJsonDocument::fromJson(file.readAll()).object()["shots"].toArray();
  for (const QJsonValue& value : shots) {
    if (value.toObject()["id"].toString() == id) {
      return value.toObject();
    }
  }
  return {};
}

void CShotDocMode::showShot(const QString& id) {
  const QJsonObject& shot = shotOf(id);
  if (shot.isEmpty()) {
    refreshPanel(tr("%1 is a picture your page asks for and this chapter has not taken yet. Pick "
                    "the state to take it in, or take it in the base.")
                     .arg(id));
    return;
  }
  // An entry with no scenario key is one taken in the base, which is a state like any other.
  selectedScenario = shot["scenario"].toString();
  showScenario(selectedScenario);
}

void CShotDocMode::showScenario(const QString& name) {
  if (name.isEmpty()) {
    CShotRecorder::reset(*ctx);
    CShotRecorder::replay(baseState, *ctx);
    refreshPanel(
        tr("The window is back in the base. Point at what to photograph and press "
           "Ctrl+Shift+F9."));
    return;
  }

  const QJsonObject& recorded = CShotChapter::scenariosOf(chapterPath());
  if (!recorded.contains(name)) {
    refreshPanel(tr("This chapter has no scenario called %1.").arg(name));
    return;
  }

  refreshPanel(tr("Setting %1 up...").arg(name));
  // From the chapter's start, never on top of what is on screen. A scenario is a state, not a
  // difference from whatever the last one left - and documentation mode holds that state on purpose,
  // so nothing else takes it down. Replaying over it flips every step that toggles: picking the same
  // scenario twice would put the window somewhere the scenario never describes.
  CShotRecorder::reset(*ctx);
  // Nothing takes it back down again: seeing the state is half of why the writer clicked.
  const int failures = CShotRecorder::replay(recorded.value(name).toArray(), *ctx);
  refreshPanel(failures > 0
                   ? tr("%1 cannot be set up here. See the log.").arg(name)
                   : tr("The window is in %1. Point at what to photograph and press Ctrl+Shift+F9.").arg(name));
}

QString CShotDocMode::scenarioFor(const QString& id) const {
  // An entry with an empty scenario is a deliberate "as the application starts", not a gap, so the
  // entry decides whenever there is one.
  const QJsonObject& shot = shotOf(id);
  return shot.isEmpty() ? selectedScenario : shot["scenario"].toString();
}

QString CShotDocMode::askForId() const {
  const QSet<QString>& referenced = pageReferences();
  const QSet<QString>& known = chapterIds();

  QStringList missing(referenced.begin(), referenced.end());
  missing.removeIf([&known](const QString& id) { return known.contains(id); });
  missing.sort();

  // Whatever is selected in the panel comes first, whether it still needs a picture or is getting
  // a new one. Retaking an existing name replaces that entry's image.
  QStringList choices;
  const QString& selected = (nullptr == panel) ? QString() : panel->currentId();
  if (!selected.isEmpty()) {
    choices << selected;
  }
  for (const QString& id : std::as_const(missing)) {
    if (id != selected) {
      choices << id;
    }
  }

  if (choices.isEmpty()) {
    QMessageBox::information(ctx->parent(), tr("Take a picture"),
                             tr("Your page asks for no picture that has not been taken. Put an "
                                "image line where you want one, and its name appears here."));
    return {};
  }

  // Not editable: the page is what says a picture exists. A name invented at the window becomes a
  // row no page references, which is waste the writer then has to reap.
  bool ok = false;
  const QString& id =
      QInputDialog::getItem(ctx->parent(), tr("Take a picture"), tr("Which picture is this?"), choices, 0, false, &ok);
  return ok ? id : QString();
}

void CShotDocMode::tag() {
  syncScenarios();

  QWidget* target = activeTarget();
  if (nullptr == target) {
    return;
  }
  if (nullptr != panel && (target == panel || panel->isAncestorOf(target))) {
    // The writer clicked the cheat sheet. They mean the application behind it.
    target = ctx->mainWindow();
  }

  CMainWindow* main = ctx->mainWindow();

  // A dialog is a window of its own. It is a child of the main window while it is open, so it has
  // a live address - but that address resolves to nothing once it is closed, which is the state
  // every retake starts from. Such a target has to come from the exposure catalog. A menu is a
  // window too and is exempt: it is a member of what owns it and is found again.
  const bool ownWindow = target->isWindow() && target != main && nullptr == qobject_cast<QMenu*>(target);

  // A window a scenario put here is photographed where it stands: the scenario opens it again, and
  // the shot records which window it expects so a scenario that stops opening it fails loudly.
  const bool fromScenario = ownWindow && !selectedScenario.isEmpty();

  QString exposure;
  if (ownWindow && !fromScenario) {
    exposure = CShotRegistry::self().exposureForWidget(target);
    if (exposure.isEmpty()) {
      reportUnexposed(target);
      return;
    }
  }

  const bool live = !fromScenario && exposure.isEmpty() && (nullptr != main) && isPartOf(main, target);
  if (!live && !fromScenario && exposure.isEmpty()) {
    reportUnexposed(target);
    return;
  }

  if (live) {
    target = chooseLivePart(main);
    if (nullptr == target) {
      return;
    }
  }

  const QString& id = askForId();
  if (id.isEmpty()) {
    return;
  }

  // An empty scenario is the base: the application as the configuration starts it, with nothing
  // performed on top. It is a state like any other, so nothing has to be said about it.
  const QString& scenario = scenarioFor(id);

  QJsonObject shot;
  shot["id"] = id;
  if (!scenario.isEmpty()) {
    shot["scenario"] = scenario;
  }

  // Which tab is open is part of the picture and nothing else records it. An unnamed tab widget
  // that is the target itself is keyed by the bare property name.
  QJsonObject set;
  if (const QTabWidget* tabs = qobject_cast<const QTabWidget*>(target); nullptr != tabs) {
    set["currentIndex"] = tabs->currentIndex();
  }
  const QList<QTabWidget*>& children = target->findChildren<QTabWidget*>();
  for (const QTabWidget* tabs : children) {
    const QString& address = CShotChapter::addressOf(target, tabs);
    if (!address.isEmpty()) {
      set[address + ".currentIndex"] = tabs->currentIndex();
    }
  }
  if (!set.isEmpty()) {
    shot["set"] = set;
  }

  if (fromScenario) {
    shot["widget"] = QString();
    shot["window"] = QString::fromLatin1(target->metaObject()->className());
    if (!target->windowTitle().isEmpty()) {
      shot["note"] = target->windowTitle();
    }
  } else if (live) {
    shot["widget"] = CShotChapter::addressOf(main, target);
    // The window's size, whatever part of it is photographed. A docker or a tab cannot be resized
    // on its own - the layout decides - but the layout is decided by the window, so the picture
    // depends on this number even when it is not the picture's own size. It is what the replay puts
    // the window at before the scenario runs; render() never applies it to anything but a window.
    shot["size"] = QJsonArray({main->width(), main->height()});
    if (!target->windowTitle().isEmpty()) {
      shot["note"] = target->windowTitle();
    }
  } else {
    // Rendered once at the default rule; a size is recorded only when the writer's window differs,
    // because that is the only case the default cannot reproduce.
    QWidget* probe = CShotRegistry::self().buildExposure(exposure, *ctx, ctx->parent());
    const QSize& defaultSize = CShotWriter::render(probe, {}).size();
    delete probe;

    shot["exposure"] = exposure;
    shot["note"] = CShotRegistry::self().exposureDescription(exposure);
    if (defaultSize != target->size()) {
      shot["size"] = QJsonArray({target->width(), target->height()});
    }
  }

  // The same call the headless run makes, so the picture the writer accepts is the picture the
  // chapter reproduces.
  CShotChapter::shootOne(shot, *ctx);

  // The picture is written first and thrown away again on Discard: the writer judges the image, not
  // a description of it.
  if (!confirmResult(id)) {
    QFile::remove(imagePath(id));
    return;
  }

  CShotChapter::store(chapterPath(), shot);
  changedShots.remove(id);
  refreshPanel(tr("Took %1.").arg(id) + driftWarning(scenario));
  qInfo().noquote() << QString::fromUtf8(QJsonDocument(shot).toJson(QJsonDocument::Compact));
}

QWidget* CShotDocMode::chooseLivePart(CMainWindow* main) const {
  // Where the writer is pointing, not where the keyboard focus happens to be: a docker is chosen by
  // looking at it, and clicking a label to focus it first is not something a writer should have to
  // know.
  QWidget* start = QApplication::widgetAt(QCursor::pos());
  if (nullptr == start || !isPartOf(main, start)) {
    start = QApplication::focusWidget();
  }

  QList<QWidget*> parts;
  QStringList labels;
  for (QWidget* w = start; nullptr != w && w != main; w = w->parentWidget()) {
    // Only what a recipe can find again.
    const QString& address = CShotChapter::addressOf(main, w);
    if (address.isEmpty()) {
      continue;
    }
    const QString& title =
        w->windowTitle().isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : w->windowTitle();
    parts << w;
    labels << QString("%1 (%2)").arg(title, address);
  }
  parts << main;
  labels << tr("The whole application");

  if (parts.size() == 1) {
    return main;
  }

  bool ok = false;
  const QString& chosen = QInputDialog::getItem(main, tr("Take a picture of"), tr("Part:"), labels, 0, false, &ok);
  if (!ok) {
    return nullptr;
  }

  const qsizetype index = labels.indexOf(chosen);
  return parts.value(index, main);
}

QString CShotDocMode::imagePath(const QString& id) const {
  return QDir(repo.absoluteFilePath("doc/images")).absoluteFilePath(id + ".png");
}

bool CShotDocMode::confirmResult(const QString& id) const {
  const QImage image(imagePath(id));

  QDialog preview(ctx->parent());
  preview.setWindowTitle(tr("Picture taken"));
  QVBoxLayout* layout = new QVBoxLayout(&preview);

  QLabel* shot = new QLabel(&preview);
  shot->setPixmap(QPixmap::fromImage(image.scaled(QSize(720, 540), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  layout->addWidget(shot);

  layout->addWidget(new QLabel(tr("Use it in your page as:"), &preview));
  QPlainTextEdit* usage = new QPlainTextEdit(QString("![](images/%1.png)").arg(id), &preview);
  usage->setReadOnly(true);
  usage->setMaximumHeight(40);
  layout->addWidget(usage);

  QDialogButtonBox* buttons = new QDialogButtonBox(&preview);
  buttons->addButton(tr("Keep"), QDialogButtonBox::AcceptRole);
  buttons->addButton(tr("Throw away"), QDialogButtonBox::RejectRole);
  connect(buttons, &QDialogButtonBox::accepted, &preview, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &preview, &QDialog::reject);
  layout->addWidget(buttons);

  return QDialog::Accepted == preview.exec();
}

void CShotDocMode::reportUnexposed(QWidget* target) const {
  const QString& title = target->windowTitle().isEmpty() ? target->objectName() : target->windowTitle();
  // A dialog without Q_OBJECT reports its base class, so the object name goes along - between the
  // two a developer can always identify it.
  const QString& ticket = tr("Please add \"%1\" to documentation mode.\n\nreported class: %2\nobject name: %3")
                              .arg(title, QString::fromLatin1(target->metaObject()->className()), target->objectName());

  QDialog dialog(ctx->parent());
  dialog.setWindowTitle(tr("Cannot photograph this yet"));
  QVBoxLayout* layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(tr("QMapShack cannot photograph \"%1\" yet. A developer has to add it once.\n"
                                  "Press Copy, paste it into a ticket, and carry on with your next picture.")
                                   .arg(title),
                               &dialog));

  QPlainTextEdit* text = new QPlainTextEdit(ticket, &dialog);
  text->setReadOnly(true);
  layout->addWidget(text);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  QPushButton* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
  connect(copy, &QPushButton::clicked, &dialog, [ticket]() { QApplication::clipboard()->setText(ticket); });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  layout->addWidget(buttons);
  dialog.exec();
}

QString CShotDocMode::chapterPath() const {
  QDir dir(repo.absoluteFilePath("doc/shots"));
  dir.mkpath(".");
  return dir.absoluteFilePath(chapter + ".json");
}

QString CShotDocMode::scenarioConfigPath(const QString& scenario) const {
  QDir dir(repo.absoluteFilePath("doc/shots/" + chapter));
  dir.mkpath(".");
  return dir.absoluteFilePath(scenario + ".ini");
}

namespace {
/// @brief Content hash of a file, or an empty array when it does not exist
QByteArray digest(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Md5);
  hash.addData(&file);
  return hash.result();
}
}  // namespace

void CShotDocMode::retakeChapter() {
  // What each picture looked like before, so the writer is told which ones the retake could not
  // reproduce. A picture that changes without QMapShack changing depended on something the shot
  // does not record - a mouse position, most often.
  QHash<QString, QByteArray> before;
  QFile file(chapterPath());
  if (file.open(QIODevice::ReadOnly)) {
    const QJsonArray& shots = QJsonDocument::fromJson(file.readAll()).object()["shots"].toArray();
    for (const QJsonValue& value : shots) {
      const QString& id = value.toObject()["id"].toString();
      before.insert(id, digest(imagePath(id)));
    }
  }

  // Nothing is put back first: every shot's scenario carries the arrangement it is taken in, so a
  // retake starts from whatever the writer has moved to and still answers the build's question.
  // The same call the headless run makes, in the application the writer is already looking at. It
  // moves the selection and the map, because that is what the shots describe.
  const int failures = CShotChapter::run(chapterPath(), *ctx);

  changedShots.clear();
  for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
    if (!it.value().isEmpty() && digest(imagePath(it.key())) != it.value()) {
      changedShots << it.key();
    }
  }

  QString status;
  if (failures > 0) {
    status += tr("%1 pictures could not be taken. See the log.").arg(failures);
  } else if (changedShots.isEmpty()) {
    status += tr("Every picture was taken again, all unchanged.");
  } else {
    status += tr("%1 of the pictures came out different. Look at them: what a retake cannot "
                 "reproduce has to be taken by hand.")
                  .arg(changedShots.size());
  }
  refreshPanel(status);
}

void CShotDocMode::reapUnused() {
  const QSet<QString>& referenced = pageReferences();

  QFile in(chapterPath());
  if (!in.open(QIODevice::ReadOnly)) {
    return;
  }
  QJsonObject chapter = QJsonDocument::fromJson(in.readAll()).object();
  in.close();

  QJsonArray keep;
  QStringList dropped;
  const QJsonArray& shots = chapter["shots"].toArray();
  for (const QJsonValue& value : shots) {
    const QString& id = value.toObject()["id"].toString();
    if (referenced.contains(id)) {
      keep.append(value);
    } else {
      dropped << id;
    }
  }

  if (dropped.isEmpty()) {
    refreshPanel(tr("Every picture is used by the page."));
    return;
  }

  // It deletes image files, so it asks first.
  if (QMessageBox::Yes != QMessageBox::question(ctx->parent(), tr("Remove unused pictures"),
                                                tr("No page references these. Delete the pictures and their "
                                                   "entries?\n\n%1")
                                                    .arg(dropped.join("\n")),
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) {
    return;
  }

  for (const QString& id : std::as_const(dropped)) {
    QFile::remove(imagePath(id));
  }

  chapter["shots"] = keep;

  // The scenarios are left alone. One with no picture is not waste - it is one the writer has not
  // used yet - and throwing a recording away is the Delete button's job, which says what dies.
  QFile out(chapterPath());
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }
  out.write(QJsonDocument(chapter).toJson(QJsonDocument::Indented));
  out.close();
  syncScenarios();

  refreshPanel(tr("Removed %1 unused.").arg(dropped.size()));
}

void CShotDocMode::takeRegion() {
  syncScenarios();

  CMainWindow* main = ctx->mainWindow();
  if (nullptr == main) {
    return;
  }

  const QString& id = askForId();
  if (id.isEmpty()) {
    return;
  }

  const QString& scenario = scenarioFor(id);

  // The same state the build will start from, arrangement and all, or the rectangle frames one
  // window and the picture is cut out of another. An empty scenario is the base, which is what the
  // window already shows.
  QString trouble;
  if (!scenario.isEmpty()) {
    refreshPanel(tr("Setting %1 up...").arg(scenario));
    const QJsonArray& actions = CShotChapter::scenariosOf(chapterPath()).value(scenario).toArray();
    // Whatever could be built is on screen, even when part of it could not. Refusing here would
    // lock the writer out of the one place a broken scenario can be repaired.
    if (0 != CShotRecorder::replay(actions, *ctx)) {
      trouble = " " + tr("%1 did not come up completely - see the log. The rectangle you drag will "
                         "hold what you can see now.")
                          .arg(scenario);
    }
    CShotWriter::settle(main);
  }

  refreshPanel(tr("Drag out the part you want. Escape cancels.") + trouble);

  QRect region;
  {
    // Scoped: the pane has to be gone before the window is photographed.
    CShotRegionPicker picker(main);
    region = picker.pick();
  }
  CShotWriter::settle(main);

  constexpr int kMinSide = 8;
  if (region.width() < kMinSide || region.height() < kMinSide) {
    refreshPanel(tr("Nothing taken."));
    return;
  }

  QJsonObject shot;
  shot["id"] = id;
  if (!scenario.isEmpty()) {
    shot["scenario"] = scenario;
  }
  // The whole window is photographed and the rectangle cut out of it, so the size the writer
  // dragged on is part of the shot: it is what the rectangle was measured against.
  shot["widget"] = QString();
  shot["size"] = QJsonArray({main->width(), main->height()});
  shot["rect"] = QJsonArray({region.x(), region.y(), region.width(), region.height()});
  shot["note"] = scenario.isEmpty() ? tr("A part of the window") : tr("A part of the window in %1").arg(scenario);

  // From the stored definition, not from what is on screen: the scenario is built again from
  // nothing, exactly as the headless run will build it.
  CShotChapter::shootOne(shot, *ctx);

  if (!confirmResult(id)) {
    QFile::remove(imagePath(id));
    return;
  }

  CShotChapter::store(chapterPath(), shot);
  changedShots.remove(id);
  refreshPanel(tr("Took %1.").arg(id) + driftWarning(scenario));
}

void CShotDocMode::syncScenarios() { ctx->setScenarios(CShotChapter::scenariosOf(chapterPath())); }

QString CShotDocMode::suggestedScenarioName() const {
  const QJsonObject& known = CShotChapter::scenariosOf(chapterPath());

  // Named after what the writer had selected when they stopped, which is what the scenario is
  // about often enough to be worth offering.
  const QString& path = CShotChapter::itemPathOf(ctx->wksList()->currentItem());
  const QString& base = path.isEmpty() ? QString("scenario") : slug(path.section(':', -1));

  QString name = base;
  for (int i = 2; known.contains(name); i++) {
    name = QString("%1-%2").arg(base).arg(i);
  }
  return name;
}

void CShotDocMode::toggleRecording() {
  if (!recorder->isRecording()) {
    // The scenario the writer has selected is where the new one carries on from, so the common part
    // does not have to be performed again. Copied into the recording, never referenced: a scenario
    // stays self-contained, and deleting the one it started from cannot change it afterwards.
    // Whatever the writer has selected; the base row carries nothing, which is the bare start.
    const QJsonArray& carried = CShotChapter::scenariosOf(chapterPath()).value(selectedScenario).toArray();

    CShotRecorder::reset(*ctx);
    // The base carries no steps, so what puts the application into it is the state this process
    // started in - otherwise a recording started from the base would inherit the arrangement and
    // the view of whichever scenario was on screen before it, and take them whole at Stop.
    CShotRecorder::replay(selectedScenario.isEmpty() ? baseState : carried, *ctx);
    recorder->start(carried);

    panel->setRecording(true);
    refreshPanel(tr("Recording, carrying on from %1. Do what is still missing - load, select, "
                    "expand, zoom the map, click an item on it - then press Stop.")
                     .arg(selectedScenario.isEmpty() ? tr("the base") : selectedScenario) +
                 driftWarning(selectedScenario));
    return;
  }

  const QJsonArray& actions = recorder->stop();
  panel->setRecording(false);

  if (actions.isEmpty()) {
    refreshPanel(
        tr("Nothing was recorded. What is kept is state - a selection, an expanded "
           "project, a control you changed, the map area, a click on an item. A button "
           "press that leaves no state behind is not one of them."));
    return;
  }

  bool ok = false;
  const QString& name =
      QInputDialog::getText(ctx->parent(), tr("Record a scenario"), tr("What is this scenario called?"),
                            QLineEdit::Normal, suggestedScenarioName(), &ok);
  if (!ok || name.isEmpty()) {
    refreshPanel(tr("Recording thrown away."));
    return;
  }

  if (CShotChapter::kBaseScenario == name) {
    refreshPanel(tr("%1 is what the pictures taken in no scenario at all are called. Pick another "
                    "name; the recording is still here.")
                     .arg(name));
    return;
  }

  CShotChapter::storeScenario(chapterPath(), name, actions);
  storeScenarioConfig(name);
  syncScenarios();
  selectedScenario = name;
  refreshPanel(tr("Scenario %1 recorded, %2 steps. Give a picture that scenario in its row, then "
                  "point at what to photograph and press Ctrl+Shift+F9.")
                   .arg(name)
                   .arg(actions.size()));
  qInfo().noquote() << name << QString::fromUtf8(QJsonDocument(actions).toJson(QJsonDocument::Compact));
}

void CShotDocMode::renameScenario() {
  if (selectedScenario.isEmpty()) {
    refreshPanel(tr("Select a scenario first."));
    return;
  }

  bool ok = false;
  const QString& to = QInputDialog::getText(ctx->parent(), tr("Rename scenario"), tr("New name:"), QLineEdit::Normal,
                                            selectedScenario, &ok);
  if (!ok || to.isEmpty() || to == selectedScenario) {
    return;
  }
  if (CShotChapter::kBaseScenario == to) {
    refreshPanel(tr("%1 is what the pictures taken in no scenario at all are called.").arg(to));
    return;
  }

  // A rename says nothing about the state, so no picture is invalidated by it.
  if (!CShotChapter::renameScenario(chapterPath(), selectedScenario, to)) {
    refreshPanel(tr("There is a scenario called %1 already.").arg(to));
    return;
  }

  selectedScenario = to;
  syncScenarios();
  refreshPanel(tr("Renamed to %1. Every picture taken in it kept its image.").arg(to));
}

void CShotDocMode::deleteScenario() {
  if (selectedScenario.isEmpty()) {
    refreshPanel(tr("Select a scenario first."));
    return;
  }

  const QString& name = selectedScenario;
  const QStringList& affected = CShotChapter::shotsUsing(chapterPath(), name);

  const QString& question = affected.isEmpty()
                                ? tr("Delete the scenario %1?").arg(name)
                                : tr("Delete the scenario %1?\n\nThese pictures are taken in it. A widget and a "
                                     "rectangle frame something else in another state, so they lose their image and "
                                     "everything that said how they were taken, and have to be done again from "
                                     "scratch:\n\n%2")
                                      .arg(name, affected.join("\n"));
  if (QMessageBox::Yes != QMessageBox::question(ctx->parent(), tr("Delete scenario"), question,
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) {
    return;
  }

  for (const QString& id : affected) {
    QFile::remove(imagePath(id));
    changedShots.remove(id);
  }
  CShotChapter::deleteScenario(chapterPath(), name);

  selectedScenario.clear();
  syncScenarios();
  refreshPanel(affected.isEmpty()
                   ? tr("%1 deleted.").arg(name)
                   : tr("%1 deleted. %2 pictures have to be done again.").arg(name).arg(affected.size()));
}

void CShotDocMode::rebindShot(const QString& id, const QString& scenario) {
  const QJsonObject& shot = shotOf(id);
  const QString& was = shot["scenario"].toString();
  if (was == scenario) {
    return;
  }

  // Only when there is something to lose: a picture that was never taken has nothing to warn about.
  const bool defined = shot.contains("widget") || shot.contains("exposure") || shot.contains("rect");
  if (defined &&
      QMessageBox::Yes != QMessageBox::question(ctx->parent(), tr("Take it in another scenario"),
                                                tr("%1 is taken in %2. A widget and a rectangle frame something else "
                                                   "in another state, so the picture and everything that said how it "
                                                   "was taken are thrown away and it has to be done again from "
                                                   "scratch.\n\nGo on?")
                                                    .arg(id, was),
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) {
    // The refresh puts the row's combo box back the way the chapter file has it.
    refreshPanel(tr("%1 is still taken in %2.").arg(id, was));
    return;
  }

  QFile::remove(imagePath(id));
  CShotChapter::rebindShot(chapterPath(), id, scenario);
  changedShots.remove(id);
  syncScenarios();
  refreshPanel(scenario.isEmpty()
                   ? tr("%1 has no scenario now and cannot be taken.").arg(id)
                   : tr("%1 is taken in %2 now. Point at what it shows and press Ctrl+Shift+F9.").arg(id, scenario));
}

int CShotDocMode::storeScenarioConfig(const QString& scenario) {
  SETTINGS;

  // CMainWindow writes these in its destructor, which has not run yet, and a fresh process needs
  // them before any scenario has been replayed.
  CMainWindow* main = ctx->mainWindow();
  if (nullptr != main) {
    cfg.beginGroup("MainWindow");
    cfg.setValue("state", main->saveState());
    cfg.setValue("geometry", main->saveGeometry());
    cfg.endGroup();
  }
  cfg.sync();

  const QString& scratch = QFileInfo(cfg.fileName()).absolutePath();
  const QString& here = repo.absolutePath();

  // The whole configuration. A scenario that stored only its difference from the base would move
  // whenever the base did, and a scenario has to depend on nothing but itself.
  QSettings own(scenarioConfigPath(scenario), QSettings::IniFormat);
  own.clear();

  int stored = 0;
  const QStringList& keys = cfg.allKeys();
  for (const QString& key : keys) {
    const QVariant& value = cfg.value(key);
    if (namesAPlace(value, here) || namesAPlace(value, scratch)) {
      continue;
    }
    own.setValue(key, value);
    stored++;
  }
  own.sync();
  return stored;
}

QString CShotDocMode::driftWarning(const QString& scenario) const {
  if (scenario.isEmpty()) {
    return {};
  }
  const int drifted = settingsDrift(scenario);
  if (0 == drifted) {
    return {};
  }
  return " " + tr("The settings on screen differ from %1's in %2 places, so the build will not "
                  "produce this picture. Press Update, or set them back.")
                   .arg(scenario)
                   .arg(drifted);
}

void CShotDocMode::storeBaseConfig() {
  if (QMessageBox::Yes != QMessageBox::question(ctx->parent(), tr("Store as base"),
                                                tr("The base is what a chapter opens on, and what a scenario recorded "
                                                   "from here on copies its settings from. Scenarios that already have "
                                                   "their own keep it, so no picture already taken moves.\n\nStore the "
                                                   "arrangement, the paths and the settings you have now as the base?"),
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) {
    return;
  }

  SETTINGS;

  // CMainWindow writes these in its destructor, which has not run yet, and the arrangement is half
  // of what the base is for.
  CMainWindow* main = ctx->mainWindow();
  if (nullptr != main) {
    cfg.beginGroup("MainWindow");
    cfg.setValue("state", main->saveState());
    cfg.setValue("geometry", main->saveGeometry());
    cfg.endGroup();
  }
  cfg.sync();

  const QString& scratch = QFileInfo(cfg.fileName()).absolutePath();
  const QString& here = repo.absolutePath();

  QSettings base(repo.absoluteFilePath(kBaseConfig), QSettings::IniFormat);
  base.clear();

  int stored = 0;
  int dropped = 0;
  const QStringList& keys = cfg.allKeys();
  for (const QString& key : keys) {
    const QVariant& value = cfg.value(key);
    if (namesAPlace(value, here) || namesAPlace(value, scratch)) {
      dropped++;
      continue;
    }
    base.setValue(key, value);
    stored++;
  }
  base.sync();

  refreshPanel(tr("%1 settings are the base now; %2 that name a place on this machine were left "
                  "out. It is what a chapter opens on and what a scenario recorded from here on "
                  "copies. The scenarios you already have keep their own, so no picture moved.")
                   .arg(stored)
                   .arg(dropped));
}

int CShotDocMode::settingsDrift(const QString& scenario) const {
  SETTINGS;

  const QString& path = scenarioConfigPath(scenario);
  if (!QFileInfo::exists(path)) {
    return 0;
  }
  QSettings own(path, QSettings::IniFormat);

  const QString& scratch = QFileInfo(cfg.fileName()).absolutePath();
  const QString& here = repo.absolutePath();

  // The arrangement is the scenario's own action and is written when the picture is taken, so it is
  // not drift; neither is a path, which is injected and never stored.
  int drifted = 0;
  const QStringList& keys = cfg.allKeys();
  for (const QString& key : keys) {
    const QVariant& value = cfg.value(key);
    if (key.startsWith("MainWindow/") || namesAPlace(value, here) || namesAPlace(value, scratch)) {
      continue;
    }
    if (own.value(key) != value) {
      drifted++;
    }
  }
  return drifted;
}

void CShotDocMode::updateScenario() {
  // The base row: what a chapter opens on and what the next recording copies.
  if (selectedScenario.isEmpty()) {
    storeBaseConfig();
    return;
  }

  // The arrangement and the map are actions of the scenario: they decide how big a picture comes
  // out and what it looks at.
  QJsonArray actions = CShotChapter::scenariosOf(chapterPath()).value(selectedScenario).toArray();
  // Replaced in place, or the scenario would grow an arrangement every time this is pressed.
  for (qsizetype i = actions.size() - 1; i >= 0; i--) {
    const QString& what = actions.at(i).toObject()["do"].toString();
    if ("layout" == what || "view" == what) {
      actions.removeAt(i);
    }
  }
  actions.prepend(CShotRecorder::viewOf(*ctx));
  actions.prepend(CShotRecorder::layoutOf(*ctx));
  CShotChapter::storeScenario(chapterPath(), selectedScenario, actions);
  syncScenarios();

  // The settings are the scenario's own file: they are read in constructors, so they can only take
  // effect in a process started with them.
  const int stored = storeScenarioConfig(selectedScenario);

  refreshPanel(tr("%1 now has this arrangement, this map and these %2 settings.").arg(selectedScenario).arg(stored));
}
