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

#include "process-scanner.hpp"

#include <QMap>

#include <functional>

#include <windows.h>
#include <tlhelp32.h>

namespace {

void forEachProcess(const std::function<void(const QString &)> &callback)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);

	if (Process32FirstW(snapshot, &entry)) {
		do {
			callback(QString::fromWCharArray(entry.szExeFile));
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
}

} // namespace

namespace ProcessScanner {

QSet<QString> runningExecutables()
{
	QSet<QString> result;
	forEachProcess([&result](const QString &name) { result.insert(name.toLower()); });
	return result;
}

QStringList runningExecutableNames()
{
	/* Keyed by lower-case name so duplicates collapse and sorting ignores case. */
	QMap<QString, QString> names;
	forEachProcess([&names](const QString &name) {
		if (name.endsWith(".exe", Qt::CaseInsensitive))
			names.insert(name.toLower(), name);
	});
	return names.values();
}

} // namespace ProcessScanner
