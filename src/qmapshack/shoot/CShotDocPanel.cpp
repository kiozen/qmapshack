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

#include "shoot/CShotDocPanel.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMoveEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSize>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWindow>
#include <utility>

#include "theme/CUiTheme.h"

namespace {
enum column_e { eColumnId, eColumnState, eColumnScenario };
}  // namespace

CShotDocPanel::CShotDocPanel(const QString& chapter, QWidget* parent)
    // A window of the supervisor, which is never photographed: an ordinary one, with the title bar
    // and the close button a writer expects, and it takes focus like any other.
    : QDialog(parent, Qt::Window) {
  setWindowTitle(tr("Documentation mode"));

  QVBoxLayout* layout = new QVBoxLayout(this);

  QLabel* title = new QLabel(tr("Chapter <b>%1</b>").arg(chapter), this);
  layout->addWidget(title);

  page = new QLabel(this);
  page->setWordWrap(true);
  layout->addWidget(page);

  // --- the scenarios ---------------------------------------------------------------------------

  layout->addWidget(new QLabel(tr("<b>Scenarios</b> — the states your pictures are taken in:"), this));

  scenarios = new QListWidget(this);
  scenarios->setAlternatingRowColors(true);
  scenarios->setMaximumHeight(120);
  // A click, not every change of the current row: putting a state back costs a map render, and the
  // arrow keys have to stay usable for looking through the list.
  connect(scenarios, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (!scenarioPicked || nullptr == item) {
      return;
    }
    // Queued: answering this rebuilds both lists, and clearing a view from inside its own click
    // leaves the view using items it has just deleted.
    const QString& name = item->data(Qt::UserRole).toString();
    QTimer::singleShot(0, this, [this, name]() {
      if (scenarioPicked) {
        scenarioPicked(name);
      }
    });
  });
  layout->addWidget(scenarios);

  QHBoxLayout* scenarioButtons = new QHBoxLayout;

  recordButton = new QPushButton(tr("Record..."), this);
  recordButton->setToolTip(
      tr("Carry on from the selected scenario: the application is put into it, you do what is "
         "still missing - load, select, zoom, click on the map - and press Stop. What comes out "
         "is a scenario of its own, complete, not tied to the one it started from."));
  connect(recordButton, &QPushButton::clicked, this, [this]() {
    if (record) {
      record();
    }
  });
  scenarioButtons->addWidget(recordButton);

  QPushButton* renameButton = new QPushButton(tr("Rename..."), this);
  renameButton->setToolTip(tr("Give the selected scenario another name. No picture is lost by it."));
  connect(renameButton, &QPushButton::clicked, this, [this]() {
    if (rename) {
      rename();
    }
  });
  scenarioButtons->addWidget(renameButton);
  whileIdle << renameButton;

  QPushButton* deleteButton = new QPushButton(tr("Delete"), this);
  deleteButton->setToolTip(
      tr("Throw the selected scenario away. Every picture taken in it loses how it was taken and "
         "has to be done again from scratch."));
  connect(deleteButton, &QPushButton::clicked, this, [this]() {
    if (deleteScenario) {
      deleteScenario();
    }
  });
  scenarioButtons->addWidget(deleteButton);
  whileIdle << deleteButton;

  QPushButton* store = new QPushButton(tr("Save config"), this);
  store->setToolTip(
      tr("Save the window arrangement, the size, the map and the settings you have now as the "
         "configuration of the state you are in, replacing the one it had. In the base it becomes "
         "what every chapter starts from, which asks first."));
  connect(store, &QPushButton::clicked, this, [this]() {
    if (storeLayout) {
      storeLayout();
    }
  });
  scenarioButtons->addWidget(store);
  whileIdle << store;

  layout->addLayout(scenarioButtons);

  // --- the pictures ----------------------------------------------------------------------------

  layout->addWidget(
      new QLabel(tr("<b>Pictures</b> — select one, then point in QMapShack at what it should show and press "
                    "<b>Ctrl+Shift+F9</b>:"),
                 this));

  shots = new QTreeWidget(this);
  shots->setRootIsDecorated(false);
  shots->setAlternatingRowColors(true);
  shots->setColumnCount(3);
  shots->setHeaderLabels({tr("Picture"), tr("State"), tr("Taken in")});
  shots->header()->setSectionResizeMode(eColumnId, QHeaderView::Stretch);
  shots->header()->setSectionResizeMode(eColumnState, QHeaderView::ResizeToContents);
  shots->header()->setSectionResizeMode(eColumnScenario, QHeaderView::ResizeToContents);
  connect(shots, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) { showPreview(); });
  connect(shots, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
    const QString& id = (nullptr == item) ? QString() : item->text(eColumnId);
    if (!picked || id.isEmpty()) {
      return;
    }
    // Queued, for the same reason as the scenario list above.
    QTimer::singleShot(0, this, [this, id]() {
      if (picked) {
        picked(id);
      }
    });
  });
  layout->addWidget(shots);

  // Between the list and the picture, because that is where they read: they act on the row above
  // and what they do shows below. Nothing selected means nothing to act on, so all three are dead
  // until a row is picked - which also puts that picture's own state on screen, which is what they
  // need to be right.
  QHBoxLayout* shotActions = new QHBoxLayout;
  const int side = fontMetrics().height() + 12;
  const auto add = [&](const QString& icon, const QString& text, const QString& hint,
                       std::function<void(const QString&)>* call) {
    QToolButton* button = new QToolButton(this);
    // A .svgt through the icon engine, never a pixmap: it follows the colour scheme and is drawn
    // at the screen's own resolution.
    button->setIcon(QIcon(icon));
    button->setIconSize(QSize(side - 10, side - 10));
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setToolTip(hint);
    button->setEnabled(false);
    connect(button, &QToolButton::clicked, this, [this, call]() {
      const QString& id = currentId();
      if (*call && !id.isEmpty()) {
        (*call)(id);
      }
    });
    shotActions->addWidget(button);
    return button;
  };

  againButton =
      add(":/icons/Screenshot.svgt", tr("Take again"),
          tr("Take this picture again, exactly the way a build would, in the state that is on screen."), &retakeShot);
  regionButton = add(":/icons/SelectArea.svgt", tr("Region"),
                     tr("Drag a rectangle for this picture instead of pointing at one widget."), &takeRegion);
  revertButton = add(":/icons/Reset.svgt", tr("Revert"),
                     tr("Throw away the picture you just took; the one the project carries stays."), &resetShot);
  shotActions->addStretch();
  layout->addLayout(shotActions);

  preview = new QLabel(this);
  preview->setMinimumHeight(200);
  preview->setAlignment(Qt::AlignCenter);
  preview->setFrameShape(QFrame::StyledPanel);
  layout->addWidget(preview);

  QHBoxLayout* actions = new QHBoxLayout;

  QPushButton* again = new QPushButton(tr("Take all again"), this);
  again->setToolTip(
      tr("Check that every picture of this chapter can still be taken from what was recorded. It "
         "renders beside them and changes nothing: whether a picture should change is yours to say, "
         "one picture at a time."));
  connect(again, &QPushButton::clicked, this, [this]() {
    if (retake) {
      retake();
    }
  });
  actions->addWidget(again);
  whileIdle << again;

  reapButton = new QPushButton(tr("Remove unused"), this);
  reapButton->setToolTip(tr("Delete the pictures of this chapter that no page references."));
  connect(reapButton, &QPushButton::clicked, this, [this]() {
    if (reap) {
      reap();
    }
  });
  actions->addWidget(reapButton);
  whileIdle << reapButton;

  QPushButton* reloadButton = new QPushButton(tr("Reload page"), this);
  reloadButton->setToolTip(
      tr("Read the page again. The list of pictures is built from its image lines when this window "
         "opens and not again, so a line you have just added or removed shows up here only after "
         "this."));
  connect(reloadButton, &QPushButton::clicked, this, [this]() {
    if (reload) {
      reload();
    }
  });
  actions->addWidget(reloadButton);
  whileIdle << reloadButton;

  QPushButton* publishButton = new QPushButton(tr("Publish"), this);
  publishButton->setToolTip(
      tr("Put the pictures you took again into the project. Only those: a picture nobody retook is "
         "left exactly as it is, because a picture drawn on another machine differs in its pixels "
         "without showing anything different."));
  connect(publishButton, &QPushButton::clicked, this, [this]() {
    if (publish) {
      publish();
    }
  });
  actions->addWidget(publishButton);
  whileIdle << publishButton;

  layout->addLayout(actions);

  status = new QLabel(this);
  status->setWordWrap(true);
  layout->addWidget(status);

  resize(460, 760);
}

void CShotDocPanel::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  if (placed) {
    return;
  }
  placed = true;

  // Deferred, and once. A decorated window's geometry is the window manager's until it has mapped
  // it, and this desktop's answer was 1200x996 whatever the constructor asked for; the resize only
  // sticks after that has happened. The writer's own resizing is left alone.
  QTimer::singleShot(0, this, [this]() {
    resize(460, 760);
    const QScreen* screen = (nullptr != windowHandle()) ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    if (nullptr != screen) {
      const QRect& available = screen->availableGeometry();
      move(available.right() - width() - 20, available.top() + 20);
    }
  });
}

void CShotDocPanel::setScenarios(const QStringList& names, const QString& current) {
  scenarioNames = names;

  scenarios->clear();
  // The base first, and it is not in `names`: it is not stored, so it cannot go stale, which is the
  // whole reason it is a row here rather than a scenario every chapter carries a copy of.
  QListWidgetItem* base = new QListWidgetItem(CShotDocPanel::kBaseRow, scenarios);
  base->setData(Qt::UserRole, QString());
  for (const QString& name : names) {
    (new QListWidgetItem(name, scenarios))->setData(Qt::UserRole, name);
  }

  for (int row = 0; row < scenarios->count(); row++) {
    if (scenarios->item(row)->data(Qt::UserRole).toString() == current) {
      scenarios->setCurrentRow(row);
      return;
    }
  }
}

void CShotDocPanel::buildScenarioCell(QTreeWidgetItem* row, const entry_t& entry) {
  QComboBox* combo = new QComboBox(shots);
  combo->addItem(CShotDocPanel::kBaseRow);
  combo->addItems(scenarioNames);
  // A shot naming a scenario the chapter has not got is a broken chapter file, not a reason to
  // silently rebind it to something else.
  if (!entry.scenario.isEmpty() && !scenarioNames.contains(entry.scenario)) {
    combo->addItem(entry.scenario);
  }
  combo->setCurrentText(entry.scenario.isEmpty() ? CShotDocPanel::kBaseRow : entry.scenario);

  const QString& id = entry.id;
  connect(combo, &QComboBox::currentTextChanged, this, [this, id](const QString& chosen) {
    if (populating || !rebind) {
      return;
    }
    // Queued: answering this rebuilds the list, which deletes the combo box that is emitting.
    const QString& scenario = (CShotDocPanel::kBaseRow == chosen) ? QString() : chosen;
    QTimer::singleShot(0, this, [this, id, scenario]() {
      if (rebind) {
        rebind(id, scenario);
      }
    });
  });
  shots->setItemWidget(row, eColumnScenario, combo);
}

void CShotDocPanel::setShots(const QList<entry_t>& shots_) {
  const QString& current = currentId();

  populating = true;
  entries = shots_;
  shots->clear();

  for (const entry_t& entry : entries) {
    const QString& text = label(entry.state);
    const QString& state = entry.changed ? tr("%1, changed by the retake").arg(text) : text;

    QTreeWidgetItem* row = new QTreeWidgetItem(shots);
    row->setText(eColumnId, entry.id);
    row->setText(eColumnState, state);
    row->setToolTip(eColumnId, entry.note.isEmpty() ? entry.id : entry.note);
    // Only what needs the writer's attention is coloured; every other row keeps the palette.
    if (entry.changed || eMissing == entry.state || eNoImage == entry.state) {
      row->setForeground(eColumnState, CUiTheme::foreground(CUiTheme::Role::eWarn));
    }
    buildScenarioCell(row, entry);
  }
  populating = false;

  int unused = 0;
  for (const entry_t& entry : entries) {
    unused += (eNotUsed == entry.state) ? 1 : 0;
  }
  reapButton->setEnabled(unused > 0);
  reapButton->setText(unused > 0 ? tr("Remove %1 unused").arg(unused) : tr("Remove unused"));

  for (int row = 0; row < entries.size(); row++) {
    if (entries.at(row).id == current) {
      shots->setCurrentItem(shots->topLevelItem(row));
      return;
    }
  }
  showPreview();
}

void CShotDocPanel::centreWaiting() {
  if (nullptr == waiting || !waiting->isVisible()) {
    return;
  }
  waiting->adjustSize();
  const QPoint& centre = frameGeometry().center();
  waiting->move(centre.x() - waiting->width() / 2, centre.y() - waiting->height() / 2);
  waiting->raise();
}

void CShotDocPanel::moveEvent(QMoveEvent* event) {
  QDialog::moveEvent(event);
  centreWaiting();
}

void CShotDocPanel::resizeEvent(QResizeEvent* event) {
  QDialog::resizeEvent(event);
  centreWaiting();
}

void CShotDocPanel::closeEvent(QCloseEvent* event) {
  if (closeRequest && !closeRequest()) {
    event->ignore();
    return;
  }
  event->accept();
  if (closed) {
    closed();
  }
}

void CShotDocPanel::updateShotActions() {
  const int row = shots->indexOfTopLevelItem(shots->currentItem());
  const entry_t& entry = entries.value(row);
  const bool picked_ = !entry.id.isEmpty();

  // Taking one again needs a shot to take it from: a picture the page asks for and nothing has ever
  // photographed has no address to re-use, so it has to be pointed at or dragged first.
  againButton->setEnabled(picked_ && (eTaken == entry.state || eNoImage == entry.state));
  regionButton->setEnabled(picked_);
  revertButton->setEnabled(picked_ && entry.changed);
}

void CShotDocPanel::showPreview() {
  updateShotActions();

  const int row = shots->indexOfTopLevelItem(shots->currentItem());
  const entry_t& entry = entries.value(row);
  if (entry.imagePath.isEmpty()) {
    preview->setPixmap({});
    preview->setText(entry.id.isEmpty() ? tr("Select a picture.") : tr("No image yet."));
    return;
  }

  preview->setText(QString());

  // Nothing was retaken, or the project has nothing to hold it against: one picture, as large as
  // the panel allows.
  if (!entry.changed || entry.publishedPath.isEmpty()) {
    const QPixmap picture(entry.imagePath);
    preview->setPixmap(
        picture.scaled(preview->width() - 8, preview->height() - 8, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return;
  }

  preview->setPixmap(comparison(entry.publishedPath, entry.imagePath));
}

QPixmap CShotDocPanel::comparison(const QString& before, const QString& after) const {
  // Side by side and captioned, because that is the whole question the writer is being asked: two
  // machines never draw one picture the same way, so only a person can say whether what changed is
  // the widget or the way its letters were drawn.
  const int gap = 8;
  const int caption = fontMetrics().height() + 2;
  const int half = (preview->width() - 3 * gap) / 2;
  const int tall = preview->height() - 2 * gap - caption;
  if (half < 16 || tall < 16) {
    return {};
  }

  const QPixmap left = QPixmap(before).scaled(half, tall, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const QPixmap right = QPixmap(after).scaled(half, tall, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  QPixmap sheet(preview->width() - 2, preview->height() - 2);
  sheet.fill(Qt::transparent);

  QPainter paint(&sheet);
  paint.setPen(palette().color(QPalette::WindowText));

  const QRect leftBox(gap, gap + caption, half, tall);
  const QRect rightBox(2 * gap + half, gap + caption, half, tall);
  paint.drawText(QRect(gap, gap, half, caption), Qt::AlignHCenter, tr("in the project"));
  paint.drawText(QRect(2 * gap + half, gap, half, caption), Qt::AlignHCenter, tr("just taken"));
  paint.drawPixmap(leftBox.x() + (half - left.width()) / 2, leftBox.y(), left);
  paint.drawPixmap(rightBox.x() + (half - right.width()) / 2, rightBox.y(), right);

  return sheet;
}

void CShotDocPanel::setStatus(const QString& text) { status->setText(text); }

void CShotDocPanel::setBusy(bool on, const QString& what) {
  // Starting a state is a whole application coming up, seven seconds of it. The box says so and,
  // being window modal, keeps the panel out of reach while it happens - so nothing else here has to
  // be disabled or dressed up.
  if (on) {
    if (nullptr == waiting) {
      waiting = new QMessageBox(QMessageBox::Information, tr("Documentation mode"), what, QMessageBox::NoButton, this);
      // Window modal and shown, never exec()'d: the panel has to keep answering the state process
      // while it waits, and a nested event loop would stop it.
      waiting->setWindowModality(Qt::WindowModal);
    }
    waiting->setText(what);
    waiting->show();
    // After show(), because QMessageBox puts an OK back whenever it is about to be shown with an
    // empty button box - and there is nothing here for the writer to answer.
    waiting->setStandardButtons(QMessageBox::NoButton);
    // Deferred as well as immediate: the panel's own geometry is still the window manager's until
    // it has mapped it, so the first placement can be against a position the panel no longer has.
    centreWaiting();
    QTimer::singleShot(0, waiting, [this]() { centreWaiting(); });
    QTimer::singleShot(250, waiting, [this]() { centreWaiting(); });
    return;
  }

  if (nullptr != waiting) {
    waiting->hide();
    waiting->deleteLater();
    waiting = nullptr;
  }
}

void CShotDocPanel::setRecording(bool on) {
  recordButton->setText(on ? tr("Stop recording") : tr("Record..."));
  for (QPushButton* button : std::as_const(whileIdle)) {
    button->setEnabled(!on);
  }
  scenarios->setEnabled(!on);
  shots->setEnabled(!on);
  // Not in whileIdle, which is a list of QPushButton. On the way out of a recording the selection
  // decides again.
  if (on) {
    againButton->setEnabled(false);
    regionButton->setEnabled(false);
    revertButton->setEnabled(false);
  } else {
    updateShotActions();
  }
  // Its own enabled state is the reap button's to decide; setShots() has the count.
  if (!on) {
    reapButton->setEnabled(false);
  }
}

void CShotDocPanel::reject() {}

QString CShotDocPanel::label(state_e state) {
  switch (state) {
    case eNoImage:
      return tr("no image");
    case eNotUsed:
      return tr("not used");
    case eMissing:
      return tr("not taken");
    case eUnregistered:
      return tr("not registered");
    case eTaken:
      break;
  }
  return tr("taken");
}

QString CShotDocPanel::currentId() const {
  const QTreeWidgetItem* item = shots->currentItem();
  return (nullptr == item) ? QString() : item->text(eColumnId);
}

void CShotDocPanel::setCurrentShot(const QString& id) {
  for (int row = 0; row < entries.size(); row++) {
    if (entries.at(row).id == id) {
      shots->setCurrentItem(shots->topLevelItem(row));
      return;
    }
  }
}

void CShotDocPanel::setPage(const QString& path, bool exists) {
  page->setText(exists ? tr("Page: %1").arg(path)
                       : CUiTheme::span(CUiTheme::Role::eWarn, tr("There is no page %1 yet.").arg(path)));
}
