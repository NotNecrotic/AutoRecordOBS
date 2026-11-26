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

#include <QList>
#include <QString>

#include <obs-data.h>

struct GameEntry {
	/* Executable name as shown in Task Manager, e.g. "Game.exe". Matched case-insensitively. */
	QString executable;
	/* Scene to switch to before recording starts. Empty = leave the current scene alone. */
	QString scene;
	/* Subfolder (relative to the OBS recording path) this game records into. Empty = OBS's path. */
	QString subfolder;
	/* Game/Window Capture source to point at this game's window. Empty = the shared default. */
	QString captureSource;
};

struct PluginConfig {
	int checkInterval = 5;
	int startDelay = 0;
	int stopDelay = 0;
	bool automationEnabled = true;
	/* Game/Window Capture source every game is pointed at unless it names its own. Empty = none. */
	QString captureSource;
	QList<GameEntry> games;

	/* The capture source to retarget for a game, or empty for none. */
	QString captureSourceFor(const GameEntry &game) const;

	/* Looks a game up by executable name, case-insensitively. */
	const GameEntry *findGame(const QString &executable) const;
};

namespace Config {

/* Full path of the v3 config file inside OBS's plugin_config directory. */
QString filePath();

/*
 * Loads the config. If no v3 config exists yet, a v2 config from
 * %APPDATA%\AutoRecordOBS\config.json is imported when present.
 * When toggleHotkey is not null it receives the saved hotkey bindings
 * (caller releases), or null if none are stored.
 */
PluginConfig load(obs_data_array_t **toggleHotkey);

/* Saves the config together with the hotkey bindings (which may be null). */
bool save(const PluginConfig &config, obs_data_array_t *toggleHotkey);

} // namespace Config
