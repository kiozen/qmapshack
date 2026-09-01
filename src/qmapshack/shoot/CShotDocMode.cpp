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
#include <QDataStream>
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
#include <QLocalSocket>
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
#include <QScreen>
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
   @brief QWidget::saveGeometry() with what only holds on this machine taken out.

   The record carries the screen the window was on and how wide that screen was, and
   QWidget::restoreGeometry() throws the whole record away when the width it is replayed against
   differs by more than a quarter (Qt 6.10.2: `factor < 0.8 || factor > 1.25` returns false, and
   nothing is applied). A width of 0 is what Qt 5.3 and earlier stored and takes the other branch,
   which only rejects a window wider than one and a half screens - so a base stored on one machine
   still sizes the window on the next. The position is left alone: restoreGeometry() moves a window
   that would land off screen back onto it.

   @return The record to store, or what came in when it is not one this knows
 */
QByteArray portableGeometry(const QByteArray& geometry) {
  QDataStream in(geometry);
  in.setVersion(QDataStream::Qt_4_0);

  quint32 magic = 0;
  quint16 major = 0;
  quint16 minor = 0;
  QRect frame;
  QRect normal;
  qint32 screen = 0;
  quint8 maximized = 0;
  quint8 fullScreen = 0;
  qint32 screenWidth = 0;
  QRect rect;
  in >> magic >> major >> minor >> frame >> normal >> screen >> maximized >> fullScreen >> screenWidth >> rect;
  if (QDataStream::Ok != in.status() || 0x1D9D0CB != magic || 3 != major) {
    return geometry;
  }

  QByteArray out;
  QDataStream stream(&out, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_4_0);
  stream << magic << major << minor << frame << normal << qint32(0) << maximized << fullScreen << qint32(0) << rect;
  return out;
}

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

CShotDocMode::CShotDocMode(const QDir& repo, const QString& chapter, const QString& scenario, QObject* parent)
    : QObject(parent),
      chapter(chapter.isEmpty() ? "scratch" : chapter),
      // The launcher spells the base out, because an option that is simply absent is a mistake it
      // cannot tell from a state it meant.
      ownScenario(CShotChapter::kBaseScenario == scenario ? QString() : scenario),
      repo(repo) {
  writer = new CShotWriter(QDir(repo.absoluteFilePath("doc/images")), "en");
  ctx = new CShotContext(*writer, "en");
  recorder = new CShotRecorder(*ctx, this);
  qApp->installEventFilter(this);
}

CShotDocMode::~CShotDocMode() {
  delete ctx;
  delete writer;
}

void CShotDocMode::start() {
  // The launcher is listening before it starts us, so the channel is up by the time the fixture is.
  if (!qlOpts->docChannel.isEmpty()) {
    channel = new QLocalSocket(this);
    connect(channel, &QLocalSocket::readyRead, this, [this]() {
      while (channel->canReadLine()) {
        obey(QString::fromUtf8(channel->readLine()).trimmed());
      }
    });
    // The state process belongs to the supervisor. If that goes - closed, killed, crashed - this
    // one has nothing left to be a state for, and an orphan holding a map and a window is worse
    // than no state at all.
    connect(channel, &QLocalSocket::disconnected, qApp, []() { qApp->exit(0); });
    channel->connectToServer(qlOpts->docChannel);
  }

  QTimer::singleShot(0, this, &CShotDocMode::slotBuildFixture);
}

void CShotDocMode::slotBuildFixture() {
  // CMainWindow defers slotLateInit() and slotSanityTest() by 100 ms; the fixture must not land in
  // front of them.
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 300) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
  }

  CShotFixture::build(*ctx);

  // Before anything is replayed on top: what a recording starts from, and what the base is.
  baseState = QJsonArray{CShotRecorder::layoutOf(*ctx), CShotRecorder::viewOf(*ctx)};

  // Applied again, after the docks have been populated: what CMainWindow restored in its
  // constructor is a size the layout then grows past, so the window the writer set up as the base
  // came back 120 pixels taller than the base says.
  {
    SETTINGS;
    cfg.beginGroup("MainWindow");
    const QByteArray& geometry = cfg.value("geometry").toByteArray();
    cfg.endGroup();
    if (!geometry.isEmpty() && !CMainWindow::isNull()) {
      CMainWindow::self().restoreGeometry(geometry);
    }
  }

  syncScenarios();
  setUpScenario(ownScenario);
  // Last, so nothing a scenario replays moves the window again.
  moveToWritersScreen();
  qInfo() << "doc: state" << (ownScenario.isEmpty() ? QString("(base)") : ownScenario) << "chapter" << chapter;
}

void CShotDocMode::moveToWritersScreen() {
  // A stored geometry carries a position on the whole desktop, so on a multi-screen desk the
  // application landed wherever that pointed - away from the panel, and again on every scenario
  // change. Only the size is a record; where the window sits is the writer's screen.
  if (qlOpts->docScreen.isEmpty() || CMainWindow::isNull()) {
    return;
  }

  const QList<QScreen*>& screens = QGuiApplication::screens();
  const QScreen* target = nullptr;
  for (const QScreen* screen : screens) {
    if (screen->name() == qlOpts->docScreen) {
      target = screen;
      break;
    }
  }
  if (nullptr == target) {
    return;
  }

  // move() places the frame, so the frame is what is centred.
  CMainWindow& window = CMainWindow::self();
  QRect frame = window.frameGeometry();
  frame.moveCenter(target->availableGeometry().center());
  window.move(frame.topLeft());
  qInfo() << "doc: window on" << target->name() << window.frameGeometry();
}

void CShotDocMode::obey(const QString& line) {
  if ("region" == line) {
    takeRegion();
  } else if (line.startsWith("update ")) {
    updateScenario(line.mid(7));
  } else if ("record" == line || "stop" == line) {
    toggleRecording();
  } else if (line.startsWith("name ")) {
    storeRecording(line.mid(5));
  } else if ("discard" == line) {
    pendingActions = QJsonArray();
  } else if (line.startsWith("select ")) {
    wantedShot = line.mid(7);
  } else if ("select" == line) {
    wantedShot.clear();
  } else if ("sync" == line) {
    syncScenarios();
  } else {
    qWarning() << "doc: the launcher asked for" << line << "which this build does not know";
  }
}

void CShotDocMode::send(const QString& line) {
  if (nullptr == channel || QLocalSocket::ConnectedState != channel->state()) {
    qInfo() << "doc:" << line;
    return;
  }
  channel->write(line.toUtf8() + '\n');
  channel->flush();
}

void CShotDocMode::report(const QString& status) { send("status " + status); }

bool CShotDocMode::eventFilter(QObject* watched, QEvent* event) {
  // The writer closing the application ends the state, and the supervisor ends the session with it.
  // Said outright rather than left to quitOnLastWindowClosed, which did not end this process.
  if (QEvent::Close == event->type() && !CMainWindow::isNull() && watched == &CMainWindow::self()) {
    qInfo() << "doc: the application window was closed; the state ends";
    qApp->exit(0);
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
        report(tr("Stop the recording first - a picture cannot be taken while one runs."));
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
void CShotDocMode::setUpScenario(const QString& name) {
  if (name.isEmpty()) {
    CShotRecorder::replay(baseState, *ctx);
    send("ready " + tr("The window is in the base. Point at what to photograph and press Ctrl+Shift+F9."));
    return;
  }

  const QJsonObject& recorded = CShotChapter::scenariosOf(chapterPath());
  if (!recorded.contains(name)) {
    report(tr("This chapter has no scenario called %1.").arg(name));
    return;
  }

  // The application came up for this scenario, so nothing is on top of it and the steps are simply
  // performed. Nothing takes them down again either: seeing the state is why the writer asked.
  const int failures = CShotRecorder::replay(recorded.value(name).toArray(), *ctx);
  send("ready " + (failures > 0
                       ? tr("%1 cannot be set up here. See the log.").arg(name)
                       : tr("The window is in %1. Point at what to photograph and press Ctrl+Shift+F9.").arg(name)));
}

QString CShotDocMode::scenarioFor(const QString& id) const {
  // An entry with an empty scenario is a deliberate "as the application starts", not a gap, so the
  // entry decides whenever there is one.
  const QJsonObject& shot = shotOf(id);
  return shot.isEmpty() ? ownScenario : shot["scenario"].toString();
}

QString CShotDocMode::askForId() const {
  const QSet<QString>& referenced = pageReferences();
  const QSet<QString>& known = chapterIds();

  QStringList missing(referenced.begin(), referenced.end());
  missing.removeIf([&known](const QString& id) { return known.contains(id); });
  missing.sort();

  // What the writer has selected first, whether it still needs a picture or is getting a new one,
  // then the ones the page asks for and the chapter has not got, then the rest. A picture that
  // already exists has to be offered too: retaking one replaces its image, and that is most of
  // what a writer does.
  QStringList choices;
  if (referenced.contains(wantedShot)) {
    choices << wantedShot;
  }
  for (const QString& id : std::as_const(missing)) {
    if (id != wantedShot) {
      choices << id;
    }
  }
  QStringList taken(referenced.begin(), referenced.end());
  taken.sort();
  for (const QString& id : std::as_const(taken)) {
    if (!choices.contains(id)) {
      choices << id;
    }
  }

  if (choices.isEmpty()) {
    QMessageBox::information(ctx->parent(), tr("Take a picture"),
                             tr("Your page asks for no picture at all. Put an image line where you "
                                "want one, and its name appears here."));
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

  CMainWindow* main = ctx->mainWindow();

  // A dialog is a window of its own. It is a child of the main window while it is open, so it has
  // a live address - but that address resolves to nothing once it is closed, which is the state
  // every retake starts from. Such a target has to come from the exposure catalog. A menu is a
  // window too and is exempt: it is a member of what owns it and is found again.
  const bool ownWindow = target->isWindow() && target != main && nullptr == qobject_cast<QMenu*>(target);

  // A window a scenario put here is photographed where it stands: the scenario opens it again, and
  // the shot records which window it expects so a scenario that stops opening it fails loudly.
  const bool fromScenario = ownWindow && !ownScenario.isEmpty();

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
  send("tagged " + id);
  report(tr("Took %1.").arg(id) + driftWarning(scenario));
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
    report(tr("Setting %1 up...").arg(scenario));
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

  report(tr("Drag out the part you want. Escape cancels.") + trouble);

  QRect region;
  {
    // Scoped: the pane has to be gone before the window is photographed.
    CShotRegionPicker picker(main);
    region = picker.pick();
  }
  CShotWriter::settle(main);

  constexpr int kMinSide = 8;
  if (region.width() < kMinSide || region.height() < kMinSide) {
    report(tr("Nothing taken."));
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
  send("tagged " + id);
  report(tr("Took %1.").arg(id) + driftWarning(scenario));
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

void CShotDocMode::startRecording() {
  recorder->start();
  send("recording");
  report(
      tr("Recording from the base. Do what the scenario is - load, select, expand, zoom the "
         "map, click an item on it - then press Stop."));
}

void CShotDocMode::toggleRecording() {
  if (!recorder->isRecording()) {
    // The supervisor only asks for this in a process it started in the base, so a recording is
    // always a whole state and never a difference from another one that is not stored with it.
    startRecording();
    return;
  }

  pendingActions = recorder->stop();
  if (pendingActions.isEmpty()) {
    send("recorded-none");
    report(
        tr("Nothing was recorded. What is kept is state - a selection, an expanded "
           "project, a control you changed, the map area, a click on an item. A button "
           "press that leaves no state behind is not one of them."));
    return;
  }

  // The name is the supervisor's to ask for: its window is the one the writer pressed Stop on.
  send("recorded-pending " + suggestedScenarioName());
}

void CShotDocMode::storeRecording(const QString& name) {
  if (pendingActions.isEmpty()) {
    return;
  }
  const QJsonArray actions = pendingActions;
  pendingActions = QJsonArray();

  if (name.isEmpty()) {
    return;
  }

  CShotChapter::storeScenario(chapterPath(), name, actions);
  storeScenarioConfig(name);
  syncScenarios();
  qInfo().noquote() << name << QString::fromUtf8(QJsonDocument(actions).toJson(QJsonDocument::Compact));

  // The supervisor answers this by starting a process in the scenario: what was recorded is only a
  // state once something comes up in it.
  send("recorded " + name);
}

int CShotDocMode::storeScenarioConfig(const QString& scenario) {
  SETTINGS;

  // CMainWindow writes these in its destructor, which has not run yet, and a fresh process needs
  // them before any scenario has been replayed.
  CMainWindow* main = ctx->mainWindow();
  if (nullptr != main) {
    cfg.beginGroup("MainWindow");
    cfg.setValue("state", main->saveState());
    cfg.setValue("geometry", portableGeometry(main->saveGeometry()));
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
  // Asked about by the supervisor, on the window the button is on.
  SETTINGS;

  // CMainWindow writes these in its destructor, which has not run yet, and the arrangement is half
  // of what the base is for.
  CMainWindow* main = ctx->mainWindow();
  if (nullptr != main) {
    cfg.beginGroup("MainWindow");
    cfg.setValue("state", main->saveState());
    cfg.setValue("geometry", portableGeometry(main->saveGeometry()));
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

  report(tr("%1 settings are the base now; %2 that name a place on this machine were left "
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

void CShotDocMode::updateScenario(const QString& target) {
  // Named by the supervisor and checked here: the settings about to be stored are this process's
  // own, so storing them anywhere but its own state would describe a state nobody was looking at.
  const QString& named = (CShotChapter::kBaseScenario == target) ? QString() : target;
  if (named != ownScenario) {
    report(tr("This window is %1, not %2 - nothing was stored.")
               .arg(ownScenario.isEmpty() ? tr("the base") : ownScenario, named.isEmpty() ? tr("the base") : named));
    return;
  }

  // The base row: what a chapter opens on and what the next recording copies.
  if (ownScenario.isEmpty()) {
    storeBaseConfig();
    return;
  }

  // The arrangement and the map are actions of the scenario: they decide how big a picture comes
  // out and what it looks at.
  QJsonArray actions = CShotChapter::scenariosOf(chapterPath()).value(ownScenario).toArray();
  // Replaced in place, or the scenario would grow an arrangement every time this is pressed.
  for (qsizetype i = actions.size() - 1; i >= 0; i--) {
    const QString& what = actions.at(i).toObject()["do"].toString();
    if ("layout" == what || "view" == what) {
      actions.removeAt(i);
    }
  }
  actions.prepend(CShotRecorder::viewOf(*ctx));
  actions.prepend(CShotRecorder::layoutOf(*ctx));
  CShotChapter::storeScenario(chapterPath(), ownScenario, actions);
  syncScenarios();

  // The settings are the scenario's own file: they are read in constructors, so they can only take
  // effect in a process started with them.
  const int stored = storeScenarioConfig(ownScenario);

  report(tr("%1 now has this arrangement, this map and these %2 settings.").arg(ownScenario).arg(stored));
}
