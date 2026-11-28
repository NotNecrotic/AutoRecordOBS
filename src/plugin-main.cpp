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

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QBoxLayout>
#include <QMainWindow>
#include <QPointer>
#include <QStatusBar>

#include "auto-recorder.hpp"
#include "plugin-support.h"
#include "settings-dialog.hpp"
#include "status-bar-item.hpp"
#include "status-dock.hpp"
#include "update-checker.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
OBS_MODULE_AUTHOR("Necrotic")

namespace {

/* OBS saves dock placement under this ID; changing it resets everyone's layout. */
constexpr const char *kDockId = "AutopilotDock";

AutoRecorder *recorder = nullptr;
UpdateChecker *updateChecker = nullptr;
QPointer<StatusBarItem> statusBarItem;

void addStatusBarItem()
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main || !recorder || statusBarItem)
		return;
	QStatusBar *bar = main->statusBar();
	statusBarItem = new StatusBarItem(recorder, kDockId, bar);

	/* OBS's status bar is one widget: a stretching message area, then the network,
	   stream and REC indicators. Sit at the start of that indicator group. */
	if (auto *message = bar->findChild<QWidget *>(QStringLiteral("messageFrame"))) {
		if (auto *row = qobject_cast<QBoxLayout *>(message->parentWidget()->layout())) {
			const int index = row->indexOf(message);
			if (index >= 0) {
				row->insertWidget(index + 1, statusBarItem);
				return;
			}
		}
	}
	/* Layout not recognised (other OBS version): fall back to the left edge. */
	bar->insertPermanentWidget(0, statusBarItem);
}

QWidget *mainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

void openSettings(void *)
{
	if (recorder)
		SettingsDialog::showFor(recorder, mainWindow());
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!recorder)
		return;

	recorder->handleFrontendEvent(event);

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		addStatusBarItem();
		recorder->start();
		if (updateChecker)
			updateChecker->start();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		/* Tear down while OBS and Qt are still fully alive. */
		delete statusBarItem;
		delete updateChecker;
		updateChecker = nullptr;
		recorder->shutdown();
		delete recorder;
		recorder = nullptr;
		break;
	default:
		break;
	}
}

} // namespace

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("PluginName");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("PluginDescription");
}

bool obs_module_load(void)
{
	recorder = new AutoRecorder();
	updateChecker = new UpdateChecker(recorder);

	auto *dock = new StatusDock(recorder, mainWindow());
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Dock.Title"), dock)) {
		obs_log(LOG_WARNING, "could not add the status dock");
		delete dock;
	}

	obs_frontend_add_tools_menu_item(obs_module_text("Menu.Settings"), openSettings, nullptr);
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	delete updateChecker;
	updateChecker = nullptr;
	delete recorder;
	recorder = nullptr;
	obs_log(LOG_INFO, "plugin unloaded");
}
