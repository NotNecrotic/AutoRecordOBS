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

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class AutoRecorder;
class QThread;

/*
 * Asks GitHub for the latest Autopilot release and compares it with the
 * running version. Automatic checks run shortly after OBS starts and then
 * once a day, unless turned off in settings. Nothing is downloaded or
 * installed: an available update is only shown, with a link to its release
 * page.
 */
class UpdateChecker : public QObject {
	Q_OBJECT

public:
	enum class State { Idle, Checking, UpToDate, UpdateAvailable, Failed };

	explicit UpdateChecker(AutoRecorder *recorder, QObject *parent = nullptr);
	~UpdateChecker() override;

	/* The checker created by the plugin, or null before load / after exit. */
	static UpdateChecker *instance();

	/* The running plugin version, e.g. "3.0.0". */
	static QString currentVersion();

	/* Starts automatic checks (if enabled in settings). Call once OBS has finished loading. */
	void start();

	/* Checks right away, whether or not automatic checks are enabled. */
	void checkNow();

	State state() const { return currentState; }
	QString latestVersion() const { return latest; }
	QString releaseUrl() const { return url; }
	QString errorText() const { return error; }

	/* Opens the latest release page (or the releases list) in the browser. */
	void openReleasePage() const;

signals:
	void stateChanged();

private:
	struct Response {
		int status = 0;
		QByteArray body;
		QString error;
	};

	static Response fetchLatestRelease();
	void applyConfig();
	void handleResponse(const Response &response);
	void setState(State state);

	QPointer<AutoRecorder> recorder;
	QTimer dailyTimer;
	QTimer startupDelay;
	QThread *worker = nullptr;
	bool started = false;

	State currentState = State::Idle;
	QString latest;
	QString url;
	QString error;
};
