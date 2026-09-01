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

#include "shoot/CShotDocLauncher.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QProcess>
#include <QProxyStyle>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>

#include "setup/CAppOpts.h"
#include "shoot/CShotChapter.h"
#include "shoot/CShotDocPanel.h"

namespace {
/// How long a state process is given to end on its own before it is killed
const int kStopTimeoutMs = 5000;

/// @return Content hash of a file, or an empty array when it does not exist
QByteArray digest(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Md5);
  hash.addData(&file);
  return hash.result();
}

/// @return The interpreter to run `shots.py` with, or an empty string when there is none
QString python() {
  // What shots.py handed over, which is the interpreter it is running under itself. A PATH search
  // is the fallback only: `python3` on Windows is normally the store's app execution alias, which
  // opens the Microsoft Store and exits non-zero, and a session started through the `.py` file
  // association need not have any python on PATH at all.
  if (!qlOpts->docPython.isEmpty() && QFileInfo::exists(qlOpts->docPython)) {
    return qlOpts->docPython;
  }
  for (const QString& name : {QStringLiteral("python3"), QStringLiteral("python")}) {
    const QString& path = QStandardPaths::findExecutable(name);
    if (!path.isEmpty()) {
      return path;
    }
  }
  return QString();
}
}  // namespace

CShotDocLauncher::CShotDocLauncher(const QDir& repo, const QString& chapter, QObject* parent)
    : QObject(parent), repo(repo), chapter(chapter.isEmpty() ? "scratch" : chapter) {
  scratch = new QTemporaryDir;
}

CShotDocLauncher::~CShotDocLauncher() {
  stopState();
  delete scratch;
}

QString CShotDocLauncher::chapterPath() const { return CShotChapter::chapterPath(repo, chapter); }

void CShotDocLauncher::start() {
  // The panel is a window of its own and is the only one this process shows: the main window it
  // needs for its singletons stays hidden.
  panel = new CShotDocPanel(chapter, nullptr);
  panel->setPickedHandler([this](const QString& id) { showShot(id); });
  panel->setScenarioPickedHandler([this](const QString& name) {
    selectedScenario = name;
    enterScenario(name);
  });
  panel->setRecordHandler([this]() {
    if (recording) {
      command("stop");
      return;
    }
    // A recording is always performed from the base, so what it stores is a whole state.
    enterScenario(QString(), true);
  });
  panel->setRenameHandler([this]() { renameScenario(); });
  panel->setDeleteScenarioHandler([this]() { deleteScenario(); });
  panel->setRebindHandler([this](const QString& id, const QString& scenario) { rebindShot(id, scenario); });
  panel->setTakeRegionHandler([this]() { command("region"); });
  panel->setReapHandler([this]() { reapUnused(); });
  panel->setRetakeHandler([this]() { retakeChapter(); });
  panel->setClosedHandler([this]() {
    qInfo() << "doc: the panel was closed; ending the session";
    stopState();
    qApp->exit(0);
  });
  panel->setStoreLayoutHandler([this]() {
    // The state that is running is the authority: the settings being stored are its own, so the
    // panel's selection cannot name a different one. They only differ while a state is starting.
    const QString& target = liveScenario;
    if (!target.isEmpty()) {
      command("update " + target);
      return;
    }
    // The base is what every chapter opens on and what the next recording copies, so it is asked
    // about - here, on the window the button is on.
    if (QMessageBox::Yes == QMessageBox::question(panel, tr("Store as base"),
                                                  tr("The base is what a chapter opens on, and what a scenario "
                                                     "recorded from here on copies its settings from. Scenarios that "
                                                     "already have their own keep it, so no picture already taken "
                                                     "moves.\n\nStore the arrangement, the size, the paths and the "
                                                     "settings the application has now as the base?"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) {
      command("update " + CShotChapter::kBaseScenario);
    }
  });
  panel->show();
  qInfo() << "doc: panel" << panel->geometry() << "hint" << panel->sizeHint() << "min" << panel->minimumSizeHint();

  // Closing either window ends the session. This is the panel's half; the state process's half is
  // the finished handler below, which quits when it was not us that ended it.
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { stopState(); });

  // One server for the life of the panel: a state process connects to it as it comes up, and the
  // next one connects to the same address.
  server = new QLocalServer(this);
  QLocalServer::removeServer(channelName());
  if (!server->listen(channelName())) {
    qWarning() << "doc: no channel to the state process:" << server->errorString();
  }
  connect(server, &QLocalServer::newConnection, this, [this]() {
    QLocalSocket* incoming = server->nextPendingConnection();
    if (nullptr == incoming) {
      return;
    }
    // The state process is one at a time, so a new connection is always the current one.
    channel = incoming;
    connect(incoming, &QLocalSocket::readyRead, this, [this, incoming]() {
      while (channel == incoming && incoming->canReadLine()) {
        const QString& line = QString::fromUtf8(incoming->readLine()).trimmed();
        // Queued, and the socket is the one this connection was made for. A report can replace the
        // state process - and with it this socket - or open a modal dialog, and neither may happen
        // inside the socket's own read: `recorded` did exactly that and took the panel with it.
        QTimer::singleShot(0, this, [this, line]() { handleReport(line); });
      }
    });
  });

  refreshPanel();
  enterScenario(QString());
}

QString CShotDocLauncher::channelName() const {
  return QStringLiteral("qms-doc-%1").arg(QCoreApplication::applicationPid());
}

void CShotDocLauncher::stopState() {
  channel = nullptr;
  if (nullptr == state) {
    return;
  }
  killing = true;
  QProcess* going = state;
  state = nullptr;
  going->terminate();
  if (!going->waitForFinished(kStopTimeoutMs)) {
    going->kill();
    going->waitForFinished(kStopTimeoutMs);
  }
  going->deleteLater();
  killing = false;
}

QString CShotDocLauncher::composeConfig(const QString& scenario, QString& error) {
  const QString& interpreter = python();
  if (interpreter.isEmpty()) {
    error =
        tr("No Python interpreter was found. Start the session through shots.py, which hands the one it runs "
           "under over as --doc-python.");
    return QString();
  }

  const QString& out = QDir(scratch->path()).absoluteFilePath("state.ini");
  QStringList args{repo.absoluteFilePath("doc/tools/shots.py"), "compose", chapter, "--out", out};
  if (!scenario.isEmpty()) {
    args << "--scenario" << scenario;
  }

  QProcess compose;
  compose.start(interpreter, args);
  if (!compose.waitForFinished(kStopTimeoutMs)) {
    error = tr("%1 did not answer: %2").arg(interpreter, compose.errorString());
    return QString();
  }
  if (QProcess::NormalExit != compose.exitStatus() || 0 != compose.exitCode()) {
    error = tr("%1 %2 failed:\n%3")
                .arg(interpreter, args.join(' '), QString::fromUtf8(compose.readAllStandardError()).trimmed());
    return QString();
  }
  if (!QFileInfo::exists(out)) {
    error = tr("%1 wrote no %2").arg(interpreter, out);
    return QString();
  }
  return out;
}

QStringList CShotDocLauncher::childArguments(const QString& config, const QString& scenario) const {
  // Our own command line, so the state process renders with the style, font, scheme and locale the
  // writer's session was started with - the only two it must not inherit are ours to set.
  QStringList args = QCoreApplication::arguments();
  args.removeFirst();

  QStringList out;
  for (int i = 0; i < args.size(); i++) {
    const QString& arg = args.at(i);
    if (arg.startsWith("--config") || arg.startsWith("--doc-scenario") || arg.startsWith("--doc-channel") ||
        arg.startsWith("--doc-screen")) {
      // Both spellings: `--config x` takes the next argument with it, `--config=x` does not.
      if (!arg.contains('=')) {
        i++;
      }
      continue;
    }
    out << arg;
  }

  // Qt takes its own arguments out of arguments(), `-style` among them, so the state process would
  // come up in the desktop's style and render pictures the build does not reproduce. The name is
  // the base style's: CQmsStyle is a QProxyStyle, and a proxy was not made by the factory, so it
  // has no name of its own.
  const QStyle* style = QApplication::style();
  if (const QProxyStyle* proxy = qobject_cast<const QProxyStyle*>(style); nullptr != proxy) {
    style = proxy->baseStyle();
  }
  if (nullptr != style && !style->name().isEmpty()) {
    out << "-style" << style->name();
  }
  out << "--config" << config;
  out << "--doc-scenario" << (scenario.isEmpty() ? CShotChapter::kBaseScenario : scenario);
  out << "--doc-channel" << channelName();
  // Read per start, so a panel the writer has dragged to another screen takes the next state with
  // it.
  if (nullptr != panel && nullptr != panel->screen()) {
    out << "--doc-screen" << panel->screen()->name();
  }
  return out;
}

void CShotDocLauncher::enterScenario(const QString& scenario, bool startRecording) {
  stopState();

  QString error;
  const QString& config = composeConfig(scenario, error);
  if (config.isEmpty()) {
    reportFailure(tr("The configuration for %1 could not be composed.\n\n%2")
                      .arg(scenario.isEmpty() ? tr("the base") : scenario, error));
    return;
  }

  liveScenario = scenario;
  recording = false;
  panel->setRecording(false);

  state = new QProcess(this);
  state->setProcessChannelMode(QProcess::ForwardedChannels);
  recordOnStart = startRecording;
  const QStringList& args = childArguments(config, scenario);
  connect(state, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
    channel = nullptr;
    recording = false;
    // Not our doing, so the writer closed the application window. That ends the session: the panel
    // is a window onto a state, and there is none any more.
    if (!killing) {
      qInfo() << "doc: the state ended on its own; ending the session";
      qApp->exit(0);
      return;
    }
    if (nullptr != panel) {
      panel->setBusy(true, tr("Please wait, switching scenario"));
    }
  });
  connect(state, &QProcess::errorOccurred, this, [this](QProcess::ProcessError problem) {
    // Only a start that never happened. Every other error ends in finished(), which is where a
    // state that goes away is answered - and a failed start emits no finished() at all, so without
    // this the panel waits behind its box for a process that will never report.
    if (QProcess::FailedToStart != problem || nullptr == state) {
      return;
    }
    const QString& why = state->errorString();
    state->deleteLater();
    state = nullptr;
    channel = nullptr;
    panel->setBusy(false, QString());
    reportFailure(tr("The application could not be started.\n\n%1").arg(why));
  });

  // Before the start, not after: a start that fails does so from inside start(), and a box put up
  // afterwards would come up over the failure and stay there.
  panel->setBusy(
      true, tr("Please wait, switching to scenario %1").arg(scenario.isEmpty() ? CShotDocPanel::kBaseRow : scenario));
  state->start(QCoreApplication::applicationFilePath(), args);
}

void CShotDocLauncher::reportFailure(const QString& text) {
  qWarning() << "doc:" << text;
  refreshPanel(text);
  if (nullptr == panel) {
    return;
  }
  // Queued: this is reached from start() too, before the application's own event loop is running,
  // and a modal box there would nest one inside it.
  QTimer::singleShot(0, panel, [this, text]() { QMessageBox::warning(panel, tr("Documentation mode"), text); });
}

void CShotDocLauncher::command(const QString& line) {
  if (nullptr == channel || QLocalSocket::ConnectedState != channel->state()) {
    refreshPanel(tr("Nothing is running to do that in. Pick a state first."));
    return;
  }
  channel->write(line.toUtf8() + '\n');
  channel->flush();
}

void CShotDocLauncher::sendSelection() {
  // Quietly: it is not a thing the writer asked for, and there is nothing to say when no state is
  // running yet.
  if (nullptr == channel || QLocalSocket::ConnectedState != channel->state() || nullptr == panel) {
    return;
  }
  channel->write("select " + selectedShot.toUtf8() + '\n');
  channel->flush();
}

void CShotDocLauncher::handleReport(const QString& line) {
  const int split = line.indexOf(' ');
  const QString& what = (split < 0) ? line : line.left(split);
  const QString& rest = (split < 0) ? QString() : line.mid(split + 1);

  if ("status" == what) {
    if (nullptr != panel) {
      panel->setStatus(rest);
    }
    return;
  }
  if ("ready" == what) {
    panel->setBusy(false, QString());
    refreshPanel(rest);
    // Not when the socket connects: that is before the fixture is built, and a recording started
    // there would have the fixture's own steps in it.
    sendSelection();
    if (recordOnStart) {
      recordOnStart = false;
      command("record");
    }
    return;
  }
  if ("tagged" == what) {
    selectedShot = rest;
    refreshPanel(tr("%1 taken.").arg(rest));
    sendSelection();
    return;
  }
  if ("changed" == what) {
    changedShots << rest;
    return;
  }
  if ("recording" == what) {
    recording = true;
    if (nullptr != panel) {
      panel->setRecording(true);
    }
    return;
  }
  if ("recorded-pending" == what) {
    nameRecording(rest);
    return;
  }
  if ("recorded" == what) {
    recording = false;
    selectedScenario = rest;
    if (nullptr != panel) {
      panel->setRecording(false);
    }
    // The recording was performed in a process that started from the base; the scenario it stored
    // is only really entered by starting one in it.
    enterScenario(rest);
    return;
  }

  qInfo() << "doc: state says" << line;
}

void CShotDocLauncher::nameRecording(const QString& suggestion) {
  bool ok = false;
  const QString& name = QInputDialog::getText(panel, tr("Record a scenario"), tr("What is this scenario called?"),
                                              QLineEdit::Normal, suggestion, &ok);
  if (!ok || name.isEmpty()) {
    command("discard");
    refreshPanel(tr("Recording thrown away."));
    return;
  }
  if (CShotChapter::kBaseScenario == name) {
    command("discard");
    refreshPanel(tr("%1 is what the pictures taken in no scenario at all are called. Record again "
                    "and give it another name.")
                     .arg(name));
    return;
  }
  command("name " + name);
}

void CShotDocLauncher::showShot(const QString& id) {
  selectedShot = id;
  const QJsonObject& shot = CShotChapter::shotOf(chapterPath(), id);
  if (shot.isEmpty()) {
    refreshPanel(tr("%1 is a picture your page asks for and this chapter has not taken yet. Pick "
                    "the state to take it in, or take it in the base.")
                     .arg(id));
    return;
  }

  // An entry with no scenario key is one taken in the base, which is a state like any other.
  selectedScenario = shot["scenario"].toString();
  if (selectedScenario == liveScenario && nullptr != state) {
    refreshPanel(tr("The state is already %1. Point at what to photograph and press Ctrl+Shift+F9.")
                     .arg(liveScenario.isEmpty() ? tr("the base") : liveScenario));
    sendSelection();
    return;
  }
  enterScenario(selectedScenario);
}

void CShotDocLauncher::renameScenario() {
  if (selectedScenario.isEmpty()) {
    refreshPanel(tr("Select a scenario first."));
    return;
  }

  bool ok = false;
  const QString& to =
      QInputDialog::getText(panel, tr("Rename scenario"), tr("New name:"), QLineEdit::Normal, selectedScenario, &ok);
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

  if (liveScenario == selectedScenario) {
    liveScenario = to;
  }
  selectedScenario = to;
  command("sync");
  refreshPanel(tr("Renamed to %1. Every picture taken in it kept its image.").arg(to));
}

void CShotDocLauncher::deleteScenario() {
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
  if (QMessageBox::Yes != QMessageBox::question(panel, tr("Delete scenario"), question,
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) {
    return;
  }

  for (const QString& id : affected) {
    QFile::remove(CShotChapter::imagePath(repo, id));
    changedShots.remove(id);
  }
  CShotChapter::deleteScenario(chapterPath(), name);
  QFile::remove(CShotChapter::scenarioConfigPath(repo, chapter, name));

  selectedScenario.clear();
  refreshPanel(affected.isEmpty()
                   ? tr("%1 deleted.").arg(name)
                   : tr("%1 deleted. %2 pictures have to be done again.").arg(name).arg(affected.size()));

  // The state that was deleted is still on screen, and nothing describes it any more.
  if (liveScenario == name) {
    enterScenario(QString());
  } else {
    command("sync");
  }
}

void CShotDocLauncher::rebindShot(const QString& id, const QString& scenario) {
  const QJsonObject& shot = CShotChapter::shotOf(chapterPath(), id);
  const QString& was = shot["scenario"].toString();
  if (was == scenario) {
    return;
  }

  // Only when there is something to lose: a picture that was never taken has nothing to warn about.
  const bool defined = shot.contains("widget") || shot.contains("exposure") || shot.contains("rect");
  if (defined &&
      QMessageBox::Yes != QMessageBox::question(panel, tr("Take it in another scenario"),
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

  QFile::remove(CShotChapter::imagePath(repo, id));
  CShotChapter::rebindShot(chapterPath(), id, scenario);
  changedShots.remove(id);
  command("sync");
  refreshPanel(scenario.isEmpty()
                   ? tr("%1 has no scenario now and cannot be taken.").arg(id)
                   : tr("%1 is taken in %2 now. Point at what it shows and press Ctrl+Shift+F9.").arg(id, scenario));
}

void CShotDocLauncher::retakeChapter() {
  if (nullptr != retake) {
    return;
  }
  const QString& interpreter = python();
  if (interpreter.isEmpty()) {
    reportFailure(
        tr("No Python interpreter was found. Start the session through shots.py, which hands the one it "
           "runs under over as --doc-python."));
    return;
  }

  // What each picture looked like before, so the writer is told which ones came out different. A
  // picture that changes without QMapShack changing depended on something its shot does not
  // record - a mouse position, most often.
  QHash<QString, QByteArray> before;
  QFile in(chapterPath());
  if (in.open(QIODevice::ReadOnly)) {
    const QJsonArray& shots = QJsonDocument::fromJson(in.readAll()).object()["shots"].toArray();
    for (const QJsonValue& value : shots) {
      const QString& id = value.toObject()["id"].toString();
      before.insert(id, digest(CShotChapter::imagePath(repo, id)));
    }
  }
  changedShots.clear();

  // `--binary` is a global option of shots.py, so it comes before the sub-command.
  const QStringList args{repo.absoluteFilePath("doc/tools/shots.py"), "--binary",
                         QCoreApplication::applicationFilePath(), "chapter", chapter};
  retake = new QProcess(this);
  retake->setProcessChannelMode(QProcess::ForwardedChannels);
  connect(retake, &QProcess::finished, this, [this, before](int code, QProcess::ExitStatus how) {
    retake->deleteLater();
    retake = nullptr;
    panel->setBusy(false, QString());

    if (QProcess::NormalExit != how || 0 != code) {
      refreshPanel(tr("The pictures could not all be taken. The console says which."));
      return;
    }
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
      if (!it.value().isEmpty() && digest(CShotChapter::imagePath(repo, it.key())) != it.value()) {
        changedShots << it.key();
      }
    }
    refreshPanel(changedShots.isEmpty()
                     ? tr("Every picture was taken again, all unchanged.")
                     : tr("%1 of the pictures came out different. Look at them: what a build cannot reproduce "
                          "has to be recorded as a scenario of its own.")
                           .arg(changedShots.size()));
  });
  connect(retake, &QProcess::errorOccurred, this, [this](QProcess::ProcessError problem) {
    if (QProcess::FailedToStart != problem || nullptr == retake) {
      return;
    }
    const QString& why = retake->errorString();
    retake->deleteLater();
    retake = nullptr;
    panel->setBusy(false, QString());
    reportFailure(tr("shots.py could not be started.\n\n%1").arg(why));
  });

  // Before the start: a start that fails does so from inside start().
  panel->setBusy(true, tr("Please wait, taking the chapter's pictures again"));
  retake->start(interpreter, args);
}

void CShotDocLauncher::reapUnused() {
  const QSet<QString>& referenced = CShotChapter::pageReferences(repo, chapter);

  QFile in(chapterPath());
  if (!in.open(QIODevice::ReadOnly)) {
    return;
  }
  QJsonObject file = QJsonDocument::fromJson(in.readAll()).object();
  in.close();

  QJsonArray keep;
  QStringList dropped;
  const QJsonArray& shots = file["shots"].toArray();
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
  if (QMessageBox::Yes != QMessageBox::question(panel, tr("Remove unused pictures"),
                                                tr("No page references these. Delete the pictures and their "
                                                   "entries?\n\n%1")
                                                    .arg(dropped.join("\n")),
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) {
    return;
  }

  for (const QString& id : std::as_const(dropped)) {
    QFile::remove(CShotChapter::imagePath(repo, id));
  }

  file["shots"] = keep;

  // The scenarios are left alone. One with no picture is not waste - it is one the writer has not
  // used yet - and throwing a recording away is the Delete button's job, which says what dies.
  QFile out(chapterPath());
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }
  out.write(QJsonDocument(file).toJson(QJsonDocument::Indented));
  out.close();
  command("sync");

  refreshPanel(tr("Removed %1 unused.").arg(dropped.size()));
}

void CShotDocLauncher::refreshPanel(const QString& status) {
  if (nullptr == panel) {
    return;
  }

  // What the page asks for. A picture the page uses but the chapter does not know is the one the
  // writer still has to take.
  const QString& pagePath = "doc/pages/" + chapter + ".md";
  const bool hasPage = QFileInfo::exists(repo.absoluteFilePath(pagePath));
  panel->setPage(pagePath, hasPage);
  const QSet<QString>& referenced = CShotChapter::pageReferences(repo, chapter);

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

      const QString& path = CShotChapter::imagePath(repo, entry.id);
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
  // Put the writer's picture back afterwards: setShots() has just rebuilt every row, and what it
  // kept was the view's idea of the selection, not ours.
  panel->setCurrentShot(selectedShot);

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
