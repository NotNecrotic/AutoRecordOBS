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

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QStringList>

#include "config.hpp"

class AutoRecorder;
class UpdateChecker;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

/*
 * Settings window laid out like OBS's own: a page list on the left
 * (General, Games) and the page on the right. The Games page follows
 * OBS's Filters window: a list with +/trash/up/down buttons beneath it,
 * and the selected game's details beside it.
 */
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	/* Opens the settings dialog, or brings the already-open one to the front. */
	static void showFor(AutoRecorder *recorder, QWidget *parent);

private:
	SettingsDialog(AutoRecorder *recorder, QWidget *parent);

	QWidget *buildGeneralPage();
	QWidget *buildGamesPage();

	void load(const PluginConfig &config);
	void refreshList();
	void selectGame(int index);
	void showSelected();
	void updatePathPreview();
	void updateButtons();
	void updateDefaultCaptureLabel();
	void updateUpdateStatus();
	int captureIndex(QComboBox *combo, const QString &source);

	void addGame(const QString &executable);
	void removeSelected();
	void moveSelected(int delta);

	bool collect(PluginConfig &config);
	bool apply();
	void markDirty();
	void setDirty(bool changed);

	QPointer<AutoRecorder> recorder;
	QPointer<UpdateChecker> updates;
	QStringList sceneNames;
	QString baseDirectory;
	QStringList captureSources;

	QList<GameEntry> gameList; /* Working copy edited by the Games page. */
	int current = -1;
	bool loading = false;
	bool dirty = false;

	QListWidget *nav = nullptr;
	QStackedWidget *pages = nullptr;
	QPushButton *applyButton = nullptr;

	QSpinBox *checkInterval = nullptr;
	QSpinBox *startDelay = nullptr;
	QSpinBox *stopDelay = nullptr;
	QComboBox *defaultCaptureCombo = nullptr;
	QCheckBox *checkForUpdates = nullptr;
	QLabel *updateStatus = nullptr;
	QPushButton *checkNowButton = nullptr;

	QListWidget *gamesView = nullptr;
	QPushButton *removeButton = nullptr;
	QPushButton *upButton = nullptr;
	QPushButton *downButton = nullptr;
	QStackedWidget *details = nullptr;
	QLineEdit *executableEdit = nullptr;
	QComboBox *sceneCombo = nullptr;
	QLineEdit *subfolderEdit = nullptr;
	QComboBox *gameCaptureCombo = nullptr;
	QLabel *subfolderHelp = nullptr; /* Help icon whose tooltip shows the resulting folder. */
};
