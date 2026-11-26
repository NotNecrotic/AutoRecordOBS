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

#include <obs-module.h>

#include <QMenu>
#include <QString>

/* The two ways to add a game, shared by the settings window and the dock. */
namespace GamePicker {

/* File picker for a game's .exe; returns just the file name, or empty if cancelled. */
QString browseForExecutable(QWidget *parent);

/* Lets the user pick (or type) a running program; returns its executable name, or empty if cancelled. */
QString pickRunningProgram(QWidget *parent);

/* Fills a menu with "Browse for Game…" and "Add Running Program…"; onPicked receives the chosen executable. */
template<typename Callback> void fillAddMenu(QMenu *menu, QWidget *parent, Callback onPicked)
{
	menu->addAction(QString::fromUtf8(obs_module_text("Settings.AddFromFile")), parent, [parent, onPicked]() {
		const QString name = browseForExecutable(parent);
		if (!name.isEmpty())
			onPicked(name);
	});
	menu->addAction(QString::fromUtf8(obs_module_text("Settings.AddRunning")), parent, [parent, onPicked]() {
		const QString name = pickRunningProgram(parent);
		if (!name.isEmpty())
			onPicked(name);
	});
}

} // namespace GamePicker
