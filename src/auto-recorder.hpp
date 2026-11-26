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

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <obs-frontend-api.h>
#include <obs-hotkey.h>

#include "config.hpp"
#include "recording-path.hpp"

/*
 * Watches running processes and starts/stops OBS recordings for configured
 * games. Everything runs on the Qt UI thread: a QTimer drives the scan and
 * OBS frontend events report what the recording output actually did.
 */
class AutoRecorder : public QObject {
	Q_OBJECT

public:
	enum class Phase {
		Idle,          /* No configured game running (or OBS is recording on its own). */
		Pending,       /* Game detected, waiting out the start delay. */
		Starting,      /* Asked OBS to start; waiting for it to confirm. */
		Recording,     /* Recording a game this plugin started. */
		Stopping,      /* Game closed, waiting out the stop delay. */
		StopRequested, /* Asked OBS to stop; waiting for it to confirm. */
	};

	struct GameState {
		QString executable;
		bool running = false;
	};

	struct Status {
		Phase phase = Phase::Idle;
		bool automationEnabled = true;
		bool manualRecording = false; /* OBS is recording, but not because of this plugin. */
		QString game;                 /* Executable name of the game being handled. */
		QString outputDir;            /* Folder the current recording is written to. */
		int secondsRemaining = 0;     /* For Pending/Stopping countdowns. */
		qint64 recordingSeconds = -1; /* Length of the current recording, -1 if not recording. */
		QList<GameState> games;       /* Every configured game, in settings order. */
	};

	explicit AutoRecorder(QObject *parent = nullptr);
	~AutoRecorder() override;

	/* Called once OBS has finished loading scenes and profiles. */
	void start();
	/* Called when OBS is exiting: saves settings and stops timers. */
	void shutdown();

	void handleFrontendEvent(enum obs_frontend_event event);

	const PluginConfig &config() const { return cfg; }
	void setConfig(const PluginConfig &config);

	bool automationEnabled() const { return cfg.automationEnabled; }
	void setAutomationEnabled(bool enabled);

	Status status() const;

signals:
	void statusChanged();
	void configChanged();

private:
	void tick();
	void restartTimer();
	void saveConfig();
	void resetToIdle();
	void beginRecording(const GameEntry &game);
	void endRecording();
	void switchToGameScene(const GameEntry &game);
	void pointCaptureAtGame(const GameEntry &game);
	void returnToPreviousScene();
	const GameEntry *firstRunningGame(const QSet<QString> &running) const;
	int remainingSeconds(int delaySeconds) const;

	static void toggleHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	PluginConfig cfg;
	QTimer timer;
	obs_hotkey_id toggleHotkey = OBS_INVALID_HOTKEY_ID;
	bool started = false;

	Phase phase = Phase::Idle;
	QString activeGame;     /* Executable name as configured. */
	QString suppressedGame; /* Lower-case exe the user stopped manually; ignored until it exits. */
	QString outputDir;
	QElapsedTimer phaseClock;
	QElapsedTimer recordingClock; /* Valid while a recording this plugin started is running. */
	int countdownDelay = 0;
	QSet<QString> lastRunning; /* Lower-case exe names seen in the latest scan. */
	RecordingPathOverride pathOverride;

	/* Scene to go back to once the game's session is over. */
	QString previousScene;
	QString gameScene;
	QString sceneOwner; /* Lower-case exe whose session switched the scene. */
};
