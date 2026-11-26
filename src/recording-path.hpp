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

#include <string>

/*
 * Points the current profile's recording path at a per-game subfolder for
 * the duration of one recording start, then puts the user's path back.
 *
 * OBS reads the recording path from the profile when a recording starts, so
 * the override only needs to live from just before
 * obs_frontend_recording_start() until OBS reports the recording started (or
 * failed). The change is never written to disk by this class.
 */
class RecordingPathOverride {
public:
	~RecordingPathOverride();

	/* Recording directory of the current profile, or empty if it doesn't record to a file. */
	static QString baseDirectory();

	/*
	 * Cleans a user-entered subfolder name so it is safe to append to a path:
	 * strips characters Windows forbids, "." and ".." segments, and trailing dots/spaces.
	 * Nested folders ("Game/Clips") are kept. Returns empty if nothing usable is left.
	 */
	static QString sanitizeSubfolder(const QString &subfolder);

	/*
	 * Creates <base>/<subfolder> and makes the profile record into it.
	 * On success returns true and sets resolvedDir; otherwise sets error.
	 */
	bool apply(const QString &subfolder, QString &resolvedDir, QString &error);

	/* Puts the original recording path back. Safe to call when nothing is applied. */
	void restore();

	bool active() const { return applied; }

private:
	bool applied = false;
	const char *section = nullptr;
	const char *key = nullptr;
	std::string originalValue;
};
