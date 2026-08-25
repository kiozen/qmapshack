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
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

#include "theme/CUiTheme.h"

namespace {
/// What a picture's combo box and the first row of the scenario list call the application as the
/// configuration starts it. It is not a scenario: nothing stores it, and a shot taken in it simply
/// has no scenario of its own.
const QString kBaseRow = QStringLiteral("(base)");

enum column_e { eColumnId, eColumnState, eColumnScenario };
}  // namespace

CShotDocPanel::CShotDocPanel(const QString& chapter, QWidget* parent)
    // No WindowStaysOnTopHint: it kept the panel above the modal dialogs the main window opens
    // too, and one of those centred under it could neither be reached, closed nor moved out from
    // under it - the application had to be killed. A parented Qt::Tool already floats above the
    // window it belongs to, which is all this needs.
    : QDialog(parent, Qt::Tool | Qt::WindowDoesNotAcceptFocus | Qt::CustomizeWindowHint | Qt::WindowTitleHint) {
  setWindowTitle(tr("Documentation mode"));
  setAttribute(Qt::WA_ShowWithoutActivating);
  // Never the reason the application stays alive: closing the main window has to end the session
  // even though this panel is still up.
  setAttribute(Qt::WA_QuitOnClose, false);

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

  QPushButton* store = new QPushButton(tr("Update"), this);
  store->setToolTip(
      tr("Put the window arrangement, the map and the settings you have now into the selected "
         "scenario, replacing the ones it had. On (base) it stores them as what every chapter "
         "starts from, which asks first."));
  connect(store, &QPushButton::clicked, this, [this]() {
    if (storeLayout) {
      storeLayout();
    }
  });
  scenarioButtons->addWidget(store);
  whileIdle << store;

  layout->addLayout(scenarioButtons);

  // --- the pictures ----------------------------------------------------------------------------

  layout->addWidget(new QLabel(tr("<b>Pictures</b> — point at one and press <b>Ctrl+Shift+F9</b>:"), this));

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

  preview = new QLabel(this);
  preview->setMinimumHeight(200);
  preview->setAlignment(Qt::AlignCenter);
  preview->setFrameShape(QFrame::StyledPanel);
  layout->addWidget(preview);

  QHBoxLayout* actions = new QHBoxLayout;

  QPushButton* region = new QPushButton(tr("Take a region..."), this);
  region->setToolTip(
      tr("Drag a rectangle over the window for what no single part of it is. The picture's own "
         "scenario is built first, so the same part is cut out of the same state next time."));
  connect(region, &QPushButton::clicked, this, [this]() {
    if (takeRegion) {
      takeRegion();
    }
  });
  actions->addWidget(region);
  whileIdle << region;

  QPushButton* again = new QPushButton(tr("Take all again"), this);
  again->setToolTip(tr("Take every picture of this chapter again and report what changed."));
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

  layout->addLayout(actions);

  status = new QLabel(this);
  status->setWordWrap(true);
  layout->addWidget(status);

  resize(460, 760);

  // Out of the way of the main window, on the screen that window is actually on - the primary
  // screen can be a different one.
  const QWidget* anchor = (nullptr != parent) ? parent->window() : nullptr;
  const QScreen* screen =
      (nullptr != anchor && nullptr != anchor->screen()) ? anchor->screen() : QGuiApplication::primaryScreen();
  if (nullptr != screen) {
    const QRect& available = screen->availableGeometry();
    move(available.right() - width() - 20, available.top() + 20);
  }
}

void CShotDocPanel::setScenarios(const QStringList& names, const QString& current) {
  scenarioNames = names;

  scenarios->clear();
  // The base first, and it is not in `names`: it is not stored, so it cannot go stale, which is the
  // whole reason it is a row here rather than a scenario every chapter carries a copy of.
  QListWidgetItem* base = new QListWidgetItem(kBaseRow, scenarios);
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
  combo->addItem(kBaseRow);
  combo->addItems(scenarioNames);
  // A shot naming a scenario the chapter has not got is a broken chapter file, not a reason to
  // silently rebind it to something else.
  if (!entry.scenario.isEmpty() && !scenarioNames.contains(entry.scenario)) {
    combo->addItem(entry.scenario);
  }
  combo->setCurrentText(entry.scenario.isEmpty() ? kBaseRow : entry.scenario);

  const QString& id = entry.id;
  connect(combo, &QComboBox::currentTextChanged, this, [this, id](const QString& chosen) {
    if (populating || !rebind) {
      return;
    }
    // Queued: answering this rebuilds the list, which deletes the combo box that is emitting.
    const QString& scenario = (kBaseRow == chosen) ? QString() : chosen;
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

void CShotDocPanel::showPreview() {
  const int row = shots->indexOfTopLevelItem(shots->currentItem());
  const entry_t& entry = entries.value(row);
  if (entry.imagePath.isEmpty()) {
    preview->setPixmap({});
    preview->setText(entry.id.isEmpty() ? tr("Select a picture.") : tr("No image yet."));
    return;
  }

  const QPixmap picture(entry.imagePath);
  preview->setText(QString());
  preview->setPixmap(
      picture.scaled(preview->width() - 8, preview->height() - 8, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CShotDocPanel::setStatus(const QString& text) { status->setText(text); }

void CShotDocPanel::setRecording(bool on) {
  recordButton->setText(on ? tr("Stop recording") : tr("Record..."));
  for (QPushButton* button : std::as_const(whileIdle)) {
    button->setEnabled(!on);
  }
  scenarios->setEnabled(!on);
  shots->setEnabled(!on);
  // Its own enabled state is the reap button's to decide; setShots() has the count.
  if (!on) {
    reapButton->setEnabled(false);
  }
}

void CShotDocPanel::closeEvent(QCloseEvent* event) {
  // The writer cannot close it, the application can.
  if (QCoreApplication::closingDown()) {
    event->accept();
    return;
  }
  event->ignore();
}

void CShotDocPanel::reject() {}

QString CShotDocPanel::label(state_e state) {
  switch (state) {
    case eNoImage:
      return tr("no image");
    case eNotUsed:
      return tr("not used");
    case eMissing:
      return tr("missing");
    case eTaken:
      break;
  }
  return tr("taken");
}

QString CShotDocPanel::currentId() const {
  const QTreeWidgetItem* item = shots->currentItem();
  return (nullptr == item) ? QString() : item->text(eColumnId);
}

void CShotDocPanel::setPage(const QString& path, bool exists) {
  page->setText(exists ? tr("Page: %1").arg(path)
                       : CUiTheme::span(CUiTheme::Role::eWarn, tr("There is no page %1 yet.").arg(path)));
}
