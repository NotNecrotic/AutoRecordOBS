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

#include "capture-target.hpp"

#include <obs.hpp>
#include <util/dstr.h>
#include <util/windows/window-helpers.h>

#include <windows.h>

#include <algorithm>
#include <string>

namespace {

constexpr const char *kGameCapture = "game_capture";
constexpr const char *kWindowCapture = "window_capture";

/* Same encoding OBS uses for the "title:class:exe" window setting. */
std::string encodeWindowPart(const char *text)
{
	std::string out;
	for (const char *c = text ? text : ""; *c; c++) {
		if (*c == '#')
			out += "#22";
		else if (*c == ':')
			out += "#3A";
		else
			out += *c;
	}
	return out;
}

struct WindowSearch {
	QString executable;
	HWND best = nullptr;
	long long bestArea = -1;
};

BOOL CALLBACK considerWindow(HWND window, LPARAM param)
{
	auto *search = reinterpret_cast<WindowSearch *>(param);

	/* Same idea as OBS's own window list: visible, top-level, not a tool window. */
	if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER))
		return TRUE;
	const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
	const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
	if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW))
		return TRUE;

	struct dstr exe = {};
	const bool haveExe = ms_get_window_exe(&exe, window);
	const bool matches = haveExe &&
			     search->executable.compare(QString::fromUtf8(exe.array), Qt::CaseInsensitive) == 0;
	dstr_free(&exe);
	if (!matches)
		return TRUE;

	/* A game may own several windows (launcher, splash); the biggest one is the game. */
	RECT rect = {};
	GetClientRect(window, &rect);
	const long long area = static_cast<long long>(rect.right - rect.left) * (rect.bottom - rect.top);
	if (area > search->bestArea) {
		search->best = window;
		search->bestArea = area;
	}
	return TRUE;
}

bool collectCaptureSource(void *param, obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	if (id && (strcmp(id, kGameCapture) == 0 || strcmp(id, kWindowCapture) == 0))
		static_cast<QStringList *>(param)->append(QString::fromUtf8(obs_source_get_name(source)));
	return true;
}

} // namespace

namespace CaptureTarget {

QStringList captureSourceNames()
{
	QStringList names;
	obs_enum_sources(collectCaptureSource, &names);
	std::sort(names.begin(), names.end(),
		  [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
	return names;
}

bool retarget(const QString &sourceName, const QString &executable, QString &windowTitle, QString &error)
{
	windowTitle.clear();

	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source) {
		error = QStringLiteral("source '%1' was not found").arg(sourceName);
		return false;
	}

	const char *id = obs_source_get_unversioned_id(source);
	const bool isGameCapture = id && strcmp(id, kGameCapture) == 0;
	const bool isWindowCapture = id && strcmp(id, kWindowCapture) == 0;
	if (!isGameCapture && !isWindowCapture) {
		error = QStringLiteral("'%1' is not a Game Capture or Window Capture source").arg(sourceName);
		return false;
	}

	WindowSearch search;
	search.executable = executable;
	EnumWindows(considerWindow, reinterpret_cast<LPARAM>(&search));

	struct dstr title = {};
	struct dstr cls = {};
	if (search.best) {
		ms_get_window_title(&title, search.best);
		ms_get_window_class(&cls, search.best);
		windowTitle = QString::fromUtf8(title.array ? title.array : "");
	}

	/* With exe priority OBS matches any window of this executable, so the title and
	   class only break ties; leaving them empty still works before the window opens. */
	const std::string window = encodeWindowPart(title.array) + ":" + encodeWindowPart(cls.array) + ":" +
				   encodeWindowPart(executable.toUtf8().constData());
	dstr_free(&title);
	dstr_free(&cls);

	OBSDataAutoRelease settings = obs_data_create();
	if (isGameCapture)
		obs_data_set_string(settings, "capture_mode", "window");
	obs_data_set_string(settings, "window", window.c_str());
	obs_data_set_int(settings, "priority", WINDOW_PRIORITY_EXE);
	obs_source_update(source, settings);
	return true;
}

} // namespace CaptureTarget
