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
#include <QTimer>
#include <QWidget>

class AutoRecorder;
class QLabel;

/*
 * Compact "Autopilot: …" indicator in OBS's bottom status bar, next to the
 * REC timer. Clicking it opens a menu to pause automation, open settings or
 * show the dock.
 */
class StatusBarItem : public QWidget {
	Q_OBJECT

public:
	StatusBarItem(AutoRecorder *recorder, const char *dockId, QWidget *parent = nullptr);

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	void refresh();
	void loadThemeColors();
	void showMenu();

	QPointer<AutoRecorder> recorder;
	QString dockId;
	QTimer clock; /* Keeps countdowns ticking every second. */
	QLabel *icon = nullptr;
	QLabel *text = nullptr;
	bool updatingTheme = false;

	QColor warning;
	QColor muted;
};
