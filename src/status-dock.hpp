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

#pragma once

#include <QColor>
#include <QPointer>
#include <QWidget>

class AutoRecorder;
class QAction;
class QLabel;
class QListWidget;
class QToolBar;

/*
 * Games dock, built the same way as OBS's Scenes dock: a list of the watched
 * games (a coloured dot shows running / starting / recording) above a toolbar.
 * Selecting a game reveals remove and move up/down, so the list can be managed
 * without opening settings. The overall state lives in the status bar item.
 */
class StatusDock : public QWidget {
	Q_OBJECT

public:
	StatusDock(AutoRecorder *recorder, QWidget *parent = nullptr);

protected:
	void changeEvent(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void refresh();
	void loadTheme();
	void updateToolbar();
	int selectedRow() const;

	void showAddMenu();
	void addGame(const QString &executable);
	void removeSelected();
	void moveSelected(int delta);

	QPointer<AutoRecorder> recorder;
	bool updatingTheme = false;

	QColor warning;
	QColor success;
	QColor muted;

	QListWidget *games = nullptr;
	QLabel *noGames = nullptr;
	QToolBar *toolbar = nullptr;
	QAction *addAction = nullptr;
	QAction *removeAction = nullptr;
	QAction *editSeparator = nullptr;
	QAction *upAction = nullptr;
	QAction *downAction = nullptr;
	QAction *pauseAction = nullptr;
	QAction *settingsAction = nullptr;
};
