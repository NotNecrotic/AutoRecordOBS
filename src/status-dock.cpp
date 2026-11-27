/*
Autopilot
Copyright (C) 2026 Necrotic

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "status-dock.hpp"

#include <obs-module.h>

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFocusEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

#include "auto-recorder.hpp"
#include "game-picker.hpp"
#include "settings-dialog.hpp"
#include "theme.hpp"

namespace {

constexpr int kIconSize = 16;

/* Used only when the active theme doesn't define OBS's text-* classes (e.g. the System theme). */
const QColor kFallbackWarning(0xE5, 0xAF, 0x24);
const QColor kFallbackSuccess(0x37, 0xD2, 0x47);
const QColor kFallbackMuted(0x99, 0x99, 0x99);

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QAction *addToolAction(QToolBar *toolbar, const QString &tip)
{
	QAction *action = toolbar->addAction(tip);
	action->setToolTip(tip);
	return action;
}

} // namespace

StatusDock::StatusDock(AutoRecorder *recorder_, QWidget *parent) : QWidget(parent), recorder(recorder_)
{
	/* OBS themes the dock body via "OBSDock > QWidget"; custom widgets only paint that if asked to. */
	setAttribute(Qt::WA_StyledBackground, true);

	/* Same widget type as the Scenes list, so spacing, hover and selection follow the theme. */
	games = new QListWidget(this);
	games->setSelectionMode(QAbstractItemView::SingleSelection);
	games->setFocusPolicy(Qt::ClickFocus);
	games->setIconSize(QSize(kIconSize, kIconSize));
	games->setFrameShape(QFrame::NoFrame);
	games->installEventFilter(this);
	games->viewport()->installEventFilter(this);
	/* Drag to reorder, like scenes. The list order is the game priority. */
	games->setDragDropMode(QAbstractItemView::InternalMove);
	games->setDefaultDropAction(Qt::MoveAction);

	noGames = new QLabel(tr_("Dock.NoGames"), this);
	noGames->setWordWrap(true);
	noGames->setAlignment(Qt::AlignCenter);
	Theme::setClasses(noGames, "text-muted");

	/* Bottom toolbar laid out like the Scenes dock's: + 🗑 | ↑ ↓ | ⏸ | ⚙.
	   Remove and the arrows only appear while a game is selected. */
	toolbar = new QToolBar(this);
	toolbar->setMovable(false);
	toolbar->setFloatable(false);
	toolbar->setIconSize(QSize(kIconSize, kIconSize));
	addAction = addToolAction(toolbar, tr_("Dock.AddGame"));
	removeAction = addToolAction(toolbar, tr_("Dock.RemoveGame"));
	editSeparator = toolbar->addSeparator();
	upAction = addToolAction(toolbar, tr_("Dock.MoveUp"));
	downAction = addToolAction(toolbar, tr_("Dock.MoveDown"));
	toolbar->addSeparator();
	pauseAction = addToolAction(toolbar, tr_("Dock.Pause"));
	pauseAction->setCheckable(true);
	toolbar->addSeparator();
	settingsAction = addToolAction(toolbar, tr_("Dock.Settings"));

	/* Same 1px inset OBS's own docks use, so the theme's side borders stay visible. */
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(1, 0, 1, 1);
	layout->setSpacing(0);
	layout->addWidget(games, 1);
	layout->addWidget(noGames, 1);
	layout->addWidget(toolbar);

	connect(addAction, &QAction::triggered, this, &StatusDock::showAddMenu);
	connect(removeAction, &QAction::triggered, this, &StatusDock::removeSelected);
	connect(upAction, &QAction::triggered, this, [this]() { moveSelected(-1); });
	connect(downAction, &QAction::triggered, this, [this]() { moveSelected(1); });
	connect(pauseAction, &QAction::toggled, this, [this](bool paused) {
		if (recorder)
			recorder->setAutomationEnabled(!paused);
	});
	connect(settingsAction, &QAction::triggered, this, [this]() {
		if (recorder)
			SettingsDialog::showFor(recorder, window());
	});
	connect(games, &QListWidget::itemSelectionChanged, this, &StatusDock::updateToolbar);
	/* Queued so the config is saved after the list has finished handling the drop. */
	connect(games->model(), &QAbstractItemModel::rowsMoved, this, &StatusDock::applyDraggedOrder,
		Qt::QueuedConnection);
	connect(recorder, &AutoRecorder::statusChanged, this, &StatusDock::refresh);

	loadTheme();
	refresh();
	updateToolbar();
}

void StatusDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	/* OBS swaps the application stylesheet when the theme changes; re-read its colours and icons. */
	if (!updatingTheme && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)) {
		loadTheme();
		refresh();
	}
}

bool StatusDock::eventFilter(QObject *watched, QEvent *event)
{
	/* Clicking empty space in the list, or anywhere outside it, deselects the game. */
	if (watched == games->viewport() && event->type() == QEvent::MouseButtonPress) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (!games->itemAt(mouse->position().toPoint()))
			games->clearSelection();
	} else if (watched == games && event->type() == QEvent::FocusOut) {
		/* Keep the selection while our own menus or confirmation dialogs are open. */
		const auto reason = static_cast<QFocusEvent *>(event)->reason();
		if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason)
			games->clearSelection();
	}
	return QWidget::eventFilter(watched, event);
}

void StatusDock::loadTheme()
{
	updatingTheme = true;
	warning = Theme::classColor(this, "text-warning", kFallbackWarning);
	success = Theme::classColor(this, "text-success", kFallbackSuccess);
	muted = Theme::classColor(this, "text-muted", kFallbackMuted);
	addAction->setIcon(Theme::classIcon(this, "icon-plus", ":/res/images/plus.svg"));
	removeAction->setIcon(Theme::classIcon(this, "icon-trash", ":/res/images/trash.svg"));
	upAction->setIcon(Theme::classIcon(this, "icon-up", ":/res/images/up.svg"));
	downAction->setIcon(Theme::classIcon(this, "icon-down", ":/res/images/down.svg"));
	pauseAction->setIcon(Theme::classIcon(this, "icon-media-pause", ":/res/images/media-pause.svg"));
	settingsAction->setIcon(Theme::classIcon(this, "icon-gear", ":/res/images/settings/general.svg"));
	updatingTheme = false;
}

int StatusDock::selectedRow() const
{
	const QList<QListWidgetItem *> selected = games->selectedItems();
	return selected.isEmpty() ? -1 : games->row(selected.first());
}

void StatusDock::updateToolbar()
{
	const int row = selectedRow();
	const bool selected = row >= 0;
	removeAction->setVisible(selected);
	editSeparator->setVisible(selected);
	upAction->setVisible(selected);
	downAction->setVisible(selected);
	upAction->setEnabled(selected && row > 0);
	downAction->setEnabled(selected && row < games->count() - 1);
}

void StatusDock::showAddMenu()
{
	QMenu menu(this);
	GamePicker::fillAddMenu(&menu, window(), [this](const QString &exe) { addGame(exe); });
	QWidget *button = toolbar->widgetForAction(addAction);
	const QPoint at = button ? button->mapToGlobal(QPoint(0, 0)) : QCursor::pos();
	/* Open upwards: the toolbar sits at the bottom of the dock. */
	menu.exec(at - QPoint(0, menu.sizeHint().height()));
}

void StatusDock::addGame(const QString &executable)
{
	if (!recorder)
		return;

	PluginConfig config = recorder->config();
	for (int i = 0; i < config.games.size(); i++) {
		if (config.games[i].executable.compare(executable, Qt::CaseInsensitive) == 0) {
			games->setCurrentRow(i);
			QMessageBox::information(window(), tr_("Dock.AddGame"),
						 tr_("Dock.AlreadyAdded").arg(config.games[i].executable));
			return;
		}
	}

	GameEntry game;
	game.executable = executable;
	config.games.append(game);
	recorder->setConfig(config);
	games->setCurrentRow(games->count() - 1);
}

void StatusDock::removeSelected()
{
	const int row = selectedRow();
	if (!recorder || row < 0 || row >= recorder->config().games.size())
		return;

	PluginConfig config = recorder->config();
	const QString name = config.games[row].executable;
	const auto answer = QMessageBox::question(window(), tr_("Dock.RemoveConfirm.Title"),
						  tr_("Dock.RemoveConfirm.Text").arg(name));
	if (answer != QMessageBox::Yes)
		return;

	config.games.removeAt(row);
	recorder->setConfig(config);
	games->clearSelection();
}

void StatusDock::moveSelected(int delta)
{
	const int row = selectedRow();
	if (!recorder || row < 0)
		return;

	PluginConfig config = recorder->config();
	const int target = row + delta;
	if (row >= config.games.size() || target < 0 || target >= config.games.size())
		return;

	config.games.swapItemsAt(row, target);
	recorder->setConfig(config);
	games->setCurrentRow(target);
}

void StatusDock::applyDraggedOrder()
{
	if (!recorder)
		return;

	/* The list was reordered by a drop; put the games in the config in the same order. */
	PluginConfig config = recorder->config();
	QList<GameEntry> reordered;
	for (int i = 0; i < games->count(); i++) {
		const QString executable = games->item(i)->text();
		const auto match = std::find_if(config.games.cbegin(), config.games.cend(), [&](const GameEntry &game) {
			return game.executable.compare(executable, Qt::CaseInsensitive) == 0;
		});
		if (match == config.games.cend())
			break;
		reordered.append(*match);
	}

	if (reordered.size() != config.games.size()) {
		/* The list no longer matches the config (it changed mid-drag); redraw from the config. */
		refresh();
		return;
	}

	config.games = reordered;
	recorder->setConfig(config);
}

void StatusDock::refresh()
{
	if (!recorder) {
		setEnabled(false);
		return;
	}

	using Phase = AutoRecorder::Phase;
	const AutoRecorder::Status s = recorder->status();
	const bool ownsRecording = s.phase != Phase::Idle && s.phase != Phase::Pending;

	while (games->count() > s.games.size())
		delete games->takeItem(games->count() - 1);
	while (games->count() < s.games.size())
		games->addItem(new QListWidgetItem());

	for (int i = 0; i < s.games.size(); i++) {
		const AutoRecorder::GameState &game = s.games[i];
		QListWidgetItem *item = games->item(i);
		const bool active = game.executable.compare(s.game, Qt::CaseInsensitive) == 0;

		/* The dot says it all; the status bar item carries the details. */
		item->setText(game.executable);
		const bool stopping = s.phase == Phase::Stopping || s.phase == Phase::StopRequested;
		if (active && stopping) {
			/* Game closed, stop delay running: amber, matching the status bar. */
			item->setIcon(Theme::glyph("waiting", warning));
			item->setToolTip(s.phase == Phase::Stopping ? tr_("Dock.GameStopsIn").arg(s.secondsRemaining)
								    : tr_("Dock.GameStopping"));
		} else if (active && ownsRecording) {
			item->setIcon(Theme::recordingIcon());
			item->setToolTip(
				s.outputDir.isEmpty()
					? tr_("Dock.GameRecording")
					: tr_("Dock.GameRecordingTo").arg(QDir::toNativeSeparators(s.outputDir)));
		} else if (active && s.phase == Phase::Pending) {
			item->setIcon(Theme::glyph("waiting", warning));
			item->setToolTip(tr_("Dock.GameStartsIn").arg(s.secondsRemaining));
		} else if (game.running) {
			item->setIcon(Theme::glyph("record", success));
			item->setToolTip(tr_("Dock.GameRunning"));
		} else {
			item->setIcon(Theme::glyph("idle", muted));
			item->setToolTip(tr_("Dock.GameNotRunning"));
		}
	}

	games->setVisible(!s.games.isEmpty());
	noGames->setVisible(s.games.isEmpty());
	updateToolbar();

	{
		const QSignalBlocker blocker(pauseAction);
		pauseAction->setChecked(!s.automationEnabled);
	}
	const QString pauseText = s.automationEnabled ? tr_("Dock.Pause") : tr_("Dock.Resume");
	pauseAction->setText(pauseText);
	pauseAction->setToolTip(pauseText);
}
