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

#include "recording-path.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <QDir>
#include <QStringList>

#include "plugin-support.h"

namespace {

struct PathKey {
	const char *section = nullptr;
	const char *key = nullptr;
};

/* Finds which profile setting holds the recording directory for the active output mode. */
PathKey recordingPathKey(config_t *profile)
{
	if (!profile)
		return {};

	const char *mode = config_get_string(profile, "Output", "Mode");
	if (!mode || astrcmpi(mode, "Advanced") != 0)
		return {"SimpleOutput", "FilePath"};

	const char *recType = config_get_string(profile, "AdvOut", "RecType");
	if (recType && astrcmpi(recType, "FFmpeg") == 0) {
		/* Custom FFmpeg output can target a URL instead of a file; nothing to redirect then. */
		if (!config_get_bool(profile, "AdvOut", "FFOutputToFile"))
			return {};
		return {"AdvOut", "FFFilePath"};
	}

	return {"AdvOut", "RecFilePath"};
}

bool isReservedName(const QString &segment)
{
	static const QStringList reserved = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
					     "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
					     "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
	const QString stem = segment.section('.', 0, 0).trimmed();
	return reserved.contains(stem, Qt::CaseInsensitive);
}

} // namespace

RecordingPathOverride::~RecordingPathOverride()
{
	restore();
}

QString RecordingPathOverride::baseDirectory()
{
	config_t *profile = obs_frontend_get_profile_config();
	const PathKey path = recordingPathKey(profile);
	if (!path.section)
		return {};

	return QDir::cleanPath(QString::fromUtf8(config_get_string(profile, path.section, path.key)));
}

QString RecordingPathOverride::sanitizeSubfolder(const QString &subfolder)
{
	static const QString forbidden = QStringLiteral("<>:\"|?*");

	QStringList segments;
	const QStringList parts = QString(subfolder).replace('\\', '/').split('/', Qt::SkipEmptyParts);
	for (const QString &part : parts) {
		QString segment;
		for (const QChar ch : part) {
			if (ch.unicode() < 0x20 || forbidden.contains(ch))
				continue;
			segment.append(ch);
		}

		segment = segment.trimmed();
		while (segment.endsWith('.') || segment.endsWith(' '))
			segment.chop(1);

		if (segment.isEmpty() || segment == "." || segment == "..")
			continue;
		if (isReservedName(segment))
			segment.append('_');

		segments.append(segment);
	}

	return segments.join('/');
}

bool RecordingPathOverride::apply(const QString &subfolder, QString &resolvedDir, QString &error)
{
	restore();

	config_t *profile = obs_frontend_get_profile_config();
	const PathKey path = recordingPathKey(profile);
	if (!path.section) {
		error = QStringLiteral("the current output mode does not record to a file");
		return false;
	}

	const QString cleanSubfolder = sanitizeSubfolder(subfolder);
	if (cleanSubfolder.isEmpty()) {
		error = QStringLiteral("subfolder name '%1' has no usable characters").arg(subfolder);
		return false;
	}

	const char *current = config_get_string(profile, path.section, path.key);
	const QString base = QDir::cleanPath(QString::fromUtf8(current ? current : ""));
	if (base.isEmpty() || base == ".") {
		error = QStringLiteral("no recording path is set in Settings > Output");
		return false;
	}

	const QString target = QDir::cleanPath(base + '/' + cleanSubfolder);
	if (os_mkdirs(target.toUtf8().constData()) == MKDIR_ERROR) {
		error = QStringLiteral("could not create folder '%1'").arg(QDir::toNativeSeparators(target));
		return false;
	}

	section = path.section;
	key = path.key;
	originalValue = current ? current : "";
	applied = true;

	config_set_string(profile, section, key, target.toUtf8().constData());
	resolvedDir = target;
	return true;
}

void RecordingPathOverride::restore()
{
	if (!applied)
		return;

	applied = false;
	config_t *profile = obs_frontend_get_profile_config();
	if (profile)
		config_set_string(profile, section, key, originalValue.c_str());
}
