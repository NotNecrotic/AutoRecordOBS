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

#include "config.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include "plugin-support.h"

namespace {

constexpr int kConfigVersion = 3;
constexpr int kMaxDelay = 3600;
constexpr int kMaxInterval = 60;

int clampDelay(long long value)
{
	return static_cast<int>(std::clamp<long long>(value, 0, kMaxDelay));
}

int clampInterval(long long value)
{
	return static_cast<int>(std::clamp<long long>(value, 1, kMaxInterval));
}

QString takeString(obs_data_t *data, const char *name)
{
	return QString::fromUtf8(obs_data_get_string(data, name)).trimmed();
}

QString legacyConfigPath()
{
	char *path = os_get_config_path_ptr("AutoRecordOBS/config.json");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

bool importLegacy(PluginConfig &config)
{
	const QString path = legacyConfigPath();
	if (!QFileInfo::exists(path))
		return false;

	OBSDataAutoRelease data = obs_data_create_from_json_file(path.toUtf8().constData());
	if (!data) {
		obs_log(LOG_WARNING, "found a v2 config at '%s' but could not parse it", path.toUtf8().constData());
		return false;
	}

	obs_data_set_default_int(data, "check_interval", config.checkInterval);
	obs_data_set_default_int(data, "start_delay", config.startDelay);
	obs_data_set_default_int(data, "stop_delay", config.stopDelay);
	config.checkInterval = clampInterval(obs_data_get_int(data, "check_interval"));
	config.startDelay = clampDelay(obs_data_get_int(data, "start_delay"));
	config.stopDelay = clampDelay(obs_data_get_int(data, "stop_delay"));

	OBSDataAutoRelease games = obs_data_get_obj(data, "games");
	if (games) {
		for (obs_data_item_t *item = obs_data_first(games); item; obs_data_item_next(&item)) {
			const QString executable = QString::fromUtf8(obs_data_item_get_name(item)).trimmed();
			/* Skip the placeholder v2 wrote into fresh configs. */
			if (executable.isEmpty() || executable.compare("example.exe", Qt::CaseInsensitive) == 0)
				continue;

			OBSDataAutoRelease settings = obs_data_item_get_obj(item);
			GameEntry game;
			game.executable = executable;
			if (settings)
				game.scene = takeString(settings, "scene");
			config.games.append(game);
		}
	}

	obs_log(LOG_INFO, "imported %lld game(s) from v2 config '%s'", static_cast<long long>(config.games.size()),
		path.toUtf8().constData());
	return true;
}

void readConfig(obs_data_t *data, PluginConfig &config)
{
	obs_data_set_default_int(data, "check_interval", config.checkInterval);
	obs_data_set_default_int(data, "start_delay", config.startDelay);
	obs_data_set_default_int(data, "stop_delay", config.stopDelay);
	obs_data_set_default_bool(data, "automation_enabled", config.automationEnabled);

	config.checkInterval = clampInterval(obs_data_get_int(data, "check_interval"));
	config.startDelay = clampDelay(obs_data_get_int(data, "start_delay"));
	config.stopDelay = clampDelay(obs_data_get_int(data, "stop_delay"));
	config.automationEnabled = obs_data_get_bool(data, "automation_enabled");
	config.captureSource = takeString(data, "capture_source");

	OBSDataArrayAutoRelease games = obs_data_get_array(data, "games");
	const size_t count = obs_data_array_count(games);
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(games, i);
		GameEntry game;
		game.executable = takeString(item, "executable");
		if (game.executable.isEmpty())
			continue;
		game.scene = takeString(item, "scene");
		game.subfolder = takeString(item, "subfolder");
		game.captureSource = takeString(item, "capture_source");
		config.games.append(game);
	}
}

} // namespace

QString PluginConfig::captureSourceFor(const GameEntry &game) const
{
	return game.captureSource.isEmpty() ? captureSource : game.captureSource;
}

const GameEntry *PluginConfig::findGame(const QString &executable) const
{
	for (const GameEntry &game : games) {
		if (game.executable.compare(executable, Qt::CaseInsensitive) == 0)
			return &game;
	}
	return nullptr;
}

namespace Config {

QString filePath()
{
	char *path = obs_module_config_path("config.json");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

PluginConfig load(obs_data_array_t **toggleHotkey)
{
	if (toggleHotkey)
		*toggleHotkey = nullptr;

	PluginConfig config;
	const QString path = filePath();

	/* Pre-release v3 builds were called AutoRecordOBS and kept their config in a sibling folder. */
	const QString preRenamePath = QFileInfo(path).dir().absoluteFilePath("../AutoRecordOBS/config.json");
	if (!QFileInfo::exists(path) && QFileInfo::exists(preRenamePath)) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		if (QFile::copy(preRenamePath, path))
			obs_log(LOG_INFO, "moved settings from '%s'",
				QDir::cleanPath(preRenamePath).toUtf8().constData());
	}

	if (!QFileInfo::exists(path)) {
		if (importLegacy(config))
			save(config, nullptr);
		return config;
	}

	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(path.toUtf8().constData(), "bak");
	if (!data) {
		obs_log(LOG_WARNING, "could not read '%s', using defaults", path.toUtf8().constData());
		return config;
	}

	readConfig(data, config);
	if (toggleHotkey)
		*toggleHotkey = obs_data_get_array(data, "toggle_hotkey");
	return config;
}

bool save(const PluginConfig &config, obs_data_array_t *toggleHotkey)
{
	const QString path = filePath();
	QDir().mkpath(QFileInfo(path).absolutePath());

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_int(data, "version", kConfigVersion);
	obs_data_set_int(data, "check_interval", config.checkInterval);
	obs_data_set_int(data, "start_delay", config.startDelay);
	obs_data_set_int(data, "stop_delay", config.stopDelay);
	obs_data_set_bool(data, "automation_enabled", config.automationEnabled);
	obs_data_set_string(data, "capture_source", config.captureSource.toUtf8().constData());

	OBSDataArrayAutoRelease games = obs_data_array_create();
	for (const GameEntry &game : config.games) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "executable", game.executable.toUtf8().constData());
		obs_data_set_string(item, "scene", game.scene.toUtf8().constData());
		obs_data_set_string(item, "subfolder", game.subfolder.toUtf8().constData());
		obs_data_set_string(item, "capture_source", game.captureSource.toUtf8().constData());
		obs_data_array_push_back(games, item);
	}
	obs_data_set_array(data, "games", games);

	if (toggleHotkey)
		obs_data_set_array(data, "toggle_hotkey", toggleHotkey);

	const bool ok = obs_data_save_json_pretty_safe(data, path.toUtf8().constData(), "tmp", "bak");
	if (!ok)
		obs_log(LOG_WARNING, "failed to save config to '%s'", path.toUtf8().constData());
	return ok;
}

} // namespace Config
