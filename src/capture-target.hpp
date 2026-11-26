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

#include <QString>
#include <QStringList>

/* Points OBS Game Capture / Window Capture sources at a game's window. */
namespace CaptureTarget {

/* Names of every Game Capture and Window Capture source, sorted. */
QStringList captureSourceNames();

/*
 * Sets the named source to capture the window of the given executable,
 * matching by executable so it keeps working if the game changes its title.
 * Works even if the game hasn't opened its window yet. On success returns true
 * and sets windowTitle (empty if no window was found); otherwise sets error.
 */
bool retarget(const QString &sourceName, const QString &executable, QString &windowTitle, QString &error);

} // namespace CaptureTarget
