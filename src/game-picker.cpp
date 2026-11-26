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

#include "game-picker.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>

#include "process-scanner.hpp"

namespace {

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Remember the last folder for the session; games usually live side by side. */
QString lastBrowseDir;

} // namespace

namespace GamePicker {

QString browseForExecutable(QWidget *parent)
{
	const QString path = QFileDialog::getOpenFileName(parent, tr_("Settings.Browse.Title"), lastBrowseDir,
							  tr_("Settings.Browse.Filter") + QStringLiteral(" (*.exe)"));
	if (path.isEmpty())
		return {};

	const QFileInfo info(path);
	lastBrowseDir = info.absolutePath();
	/* Games are matched by process name, so only the file name is kept. */
	return info.fileName();
}

QString pickRunningProgram(QWidget *parent)
{
	bool ok = false;
	const QString chosen = QInputDialog::getItem(parent, tr_("Settings.AddRunning.Title"),
						     tr_("Settings.AddRunning.Label"),
						     ProcessScanner::runningExecutableNames(), 0, true, &ok);
	return ok ? chosen.trimmed() : QString();
}

} // namespace GamePicker
