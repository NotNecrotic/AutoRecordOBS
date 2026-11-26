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

#include "auto-recorder.hpp"

#include <obs-module.h>
#include <obs.hpp>

#include <QDir>
#include <QMetaObject>

#include "capture-target.hpp"
#include "plugin-support.h"
#include "process-scanner.hpp"

namespace {

/* Ticks are scheduled every check_interval seconds; allow a little jitter so a
   2 s delay with a 2 s interval fires on the next tick instead of the one after. */
constexpr qint64 kTickSlackMs = 250;
/* How long to wait for OBS to confirm a start/stop before giving up. */
constexpr qint64 kStartTimeoutMs = 15000;
constexpr qint64 kStopTimeoutMs = 30000;

const char *kHotkeyName = "Autopilot.ToggleAutomation";

QByteArray utf8(const QString &text)
{
	return text.toUtf8();
}

QString currentSceneName()
{
	OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
	return scene ? QString::fromUtf8(obs_source_get_name(scene)) : QString();
}

} // namespace

AutoRecorder::AutoRecorder(QObject *parent) : QObject(parent)
{
	obs_data_array_t *hotkeyData = nullptr;
	cfg = Config::load(&hotkeyData);

	toggleHotkey = obs_hotkey_register_frontend(kHotkeyName, obs_module_text("Hotkey.ToggleAutomation"),
						    &AutoRecorder::toggleHotkeyPressed, this);
	if (hotkeyData) {
		obs_hotkey_load(toggleHotkey, hotkeyData);
		obs_data_array_release(hotkeyData);
	}

	timer.setTimerType(Qt::CoarseTimer);
	connect(&timer, &QTimer::timeout, this, &AutoRecorder::tick);
}

AutoRecorder::~AutoRecorder()
{
	timer.stop();
	pathOverride.restore();
	if (toggleHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(toggleHotkey);
}

void AutoRecorder::start()
{
	if (started)
		return;
	started = true;

	obs_log(LOG_INFO, "watching %lld game(s), checking every %d s, automation %s",
		static_cast<long long>(cfg.games.size()), cfg.checkInterval,
		cfg.automationEnabled ? "enabled" : "paused");

	restartTimer();
	tick();
}

void AutoRecorder::shutdown()
{
	timer.stop();
	started = false;
	pathOverride.restore();
	saveConfig();
}

void AutoRecorder::restartTimer()
{
	timer.start(cfg.checkInterval * 1000);
}

void AutoRecorder::saveConfig()
{
	obs_data_array_t *hotkeyData = nullptr;
	if (toggleHotkey != OBS_INVALID_HOTKEY_ID)
		hotkeyData = obs_hotkey_save(toggleHotkey);
	Config::save(cfg, hotkeyData);
	obs_data_array_release(hotkeyData);
}

void AutoRecorder::setConfig(const PluginConfig &config)
{
	const bool wasEnabled = cfg.automationEnabled;
	cfg = config;
	cfg.automationEnabled = wasEnabled;
	saveConfig();

	/* A pending start may belong to a game that was just removed or re-timed. */
	if (phase == Phase::Pending)
		resetToIdle();

	if (started) {
		restartTimer();
		tick();
	}
	emit configChanged();
	emit statusChanged();
}

void AutoRecorder::setAutomationEnabled(bool enabled)
{
	if (cfg.automationEnabled == enabled)
		return;

	cfg.automationEnabled = enabled;
	obs_log(LOG_INFO, "automation %s", enabled ? "resumed" : "paused");
	saveConfig();

	if (started)
		tick();
	emit statusChanged();
}

void AutoRecorder::toggleHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;

	/* Hotkeys fire on OBS's hotkey thread; hop over to the UI thread. */
	auto *self = static_cast<AutoRecorder *>(data);
	QMetaObject::invokeMethod(
		self, [self]() { self->setAutomationEnabled(!self->automationEnabled()); }, Qt::QueuedConnection);
}

void AutoRecorder::resetToIdle()
{
	phase = Phase::Idle;
	activeGame.clear();
	outputDir.clear();
	countdownDelay = 0;
	recordingClock.invalidate();
}

const GameEntry *AutoRecorder::firstRunningGame(const QSet<QString> &running) const
{
	for (const GameEntry &game : cfg.games) {
		const QString exe = game.executable.toLower();
		if (exe == suppressedGame)
			continue;
		if (running.contains(exe))
			return &game;
	}
	return nullptr;
}

int AutoRecorder::remainingSeconds(int delaySeconds) const
{
	const qint64 remainingMs = delaySeconds * 1000LL - phaseClock.elapsed();
	if (remainingMs <= 0)
		return 0;
	return static_cast<int>((remainingMs + 999) / 1000);
}

void AutoRecorder::tick()
{
	/* Safety nets in case OBS never reports back. */
	if (phase == Phase::Starting && phaseClock.elapsed() > kStartTimeoutMs) {
		obs_log(LOG_WARNING, "OBS did not start recording for %s; not retrying until it is restarted",
			utf8(activeGame).constData());
		pathOverride.restore();
		suppressedGame = activeGame.toLower();
		resetToIdle();
	} else if (phase == Phase::StopRequested && phaseClock.elapsed() > kStopTimeoutMs) {
		obs_log(LOG_WARNING, "OBS did not confirm the recording stopped");
		resetToIdle();
	}
	if (phase != Phase::Starting)
		pathOverride.restore();

	lastRunning = ProcessScanner::runningExecutables();
	if (!suppressedGame.isEmpty() && !lastRunning.contains(suppressedGame)) {
		obs_log(LOG_INFO, "%s closed; it will be recorded again next launch", utf8(suppressedGame).constData());
		suppressedGame.clear();
	}

	/* A game whose recording was stopped by hand keeps its scene until it closes. */
	if (!sceneOwner.isEmpty() && phase == Phase::Idle && !lastRunning.contains(sceneOwner))
		returnToPreviousScene();

	if (!cfg.automationEnabled) {
		/* Pausing never stops a recording; it just freezes the countdowns. */
		if (phase == Phase::Pending)
			resetToIdle();
		else if (phase == Phase::Stopping)
			phase = Phase::Recording;
		emit statusChanged();
		return;
	}

	switch (phase) {
	case Phase::Idle:
	case Phase::Pending: {
		if (obs_frontend_recording_active()) {
			/* The user is recording on their own; don't interfere. */
			resetToIdle();
			break;
		}

		const GameEntry *game = firstRunningGame(lastRunning);
		if (!game) {
			resetToIdle();
			break;
		}

		if (phase != Phase::Pending || activeGame.compare(game->executable, Qt::CaseInsensitive) != 0) {
			phase = Phase::Pending;
			activeGame = game->executable;
			countdownDelay = cfg.startDelay;
			phaseClock.restart();
			obs_log(LOG_INFO, "%s detected, recording in %d s", utf8(activeGame).constData(),
				countdownDelay);
		}

		if (phaseClock.elapsed() + kTickSlackMs >= countdownDelay * 1000LL)
			beginRecording(*game);
		break;
	}

	case Phase::Recording:
	case Phase::Stopping: {
		if (lastRunning.contains(activeGame.toLower())) {
			phase = Phase::Recording;
			break;
		}

		if (phase != Phase::Stopping) {
			phase = Phase::Stopping;
			countdownDelay = cfg.stopDelay;
			phaseClock.restart();
			obs_log(LOG_INFO, "%s closed, stopping in %d s", utf8(activeGame).constData(), countdownDelay);
		}

		if (phaseClock.elapsed() + kTickSlackMs >= countdownDelay * 1000LL)
			endRecording();
		break;
	}

	case Phase::Starting:
	case Phase::StopRequested:
		break;
	}

	emit statusChanged();
}

void AutoRecorder::switchToGameScene(const GameEntry &game)
{
	if (game.scene.isEmpty())
		return;

	OBSSourceAutoRelease source = obs_get_source_by_name(utf8(game.scene).constData());
	if (!source || !obs_scene_from_source(source)) {
		obs_log(LOG_WARNING, "scene '%s' for %s was not found; keeping the current scene",
			utf8(game.scene).constData(), utf8(game.executable).constData());
		return;
	}

	const QString current = currentSceneName();
	if (current == game.scene)
		return;

	/* If an earlier session's scene is still up (game switched to another game),
	   keep the original scene as the one to return to. */
	if (sceneOwner.isEmpty() || current != gameScene)
		previousScene = current;
	gameScene = game.scene;
	sceneOwner = game.executable.toLower();

	obs_frontend_set_current_scene(source);
}

void AutoRecorder::returnToPreviousScene()
{
	const QString previous = previousScene;
	const QString expected = gameScene;
	previousScene.clear();
	gameScene.clear();
	sceneOwner.clear();

	if (previous.isEmpty())
		return;

	/* The user picked another scene during the session; leave their choice alone. */
	if (currentSceneName() != expected)
		return;

	OBSSourceAutoRelease source = obs_get_source_by_name(utf8(previous).constData());
	if (!source || !obs_scene_from_source(source)) {
		obs_log(LOG_INFO, "previous scene '%s' no longer exists; staying on '%s'", utf8(previous).constData(),
			utf8(expected).constData());
		return;
	}

	obs_log(LOG_INFO, "switching back to scene '%s'", utf8(previous).constData());
	obs_frontend_set_current_scene(source);
}

void AutoRecorder::pointCaptureAtGame(const GameEntry &game)
{
	const QString sourceName = cfg.captureSourceFor(game);
	if (sourceName.isEmpty())
		return;

	QString title;
	QString error;
	if (!CaptureTarget::retarget(sourceName, game.executable, title, error)) {
		obs_log(LOG_WARNING, "could not point capture source at %s: %s", utf8(game.executable).constData(),
			utf8(error).constData());
		return;
	}

	if (title.isEmpty())
		obs_log(LOG_INFO, "'%s' now captures %s (window not open yet; it will be picked up when it appears)",
			utf8(sourceName).constData(), utf8(game.executable).constData());
	else
		obs_log(LOG_INFO, "'%s' now captures %s window '%s'", utf8(sourceName).constData(),
			utf8(game.executable).constData(), utf8(title).constData());
}

void AutoRecorder::beginRecording(const GameEntry &game)
{
	switchToGameScene(game);
	pointCaptureAtGame(game);

	outputDir.clear();
	if (!game.subfolder.trimmed().isEmpty()) {
		QString error;
		if (!pathOverride.apply(game.subfolder, outputDir, error)) {
			obs_log(LOG_WARNING, "not using subfolder '%s' for %s: %s", utf8(game.subfolder).constData(),
				utf8(game.executable).constData(), utf8(error).constData());
			outputDir.clear();
		}
	}
	if (outputDir.isEmpty())
		outputDir = RecordingPathOverride::baseDirectory();

	phase = Phase::Starting;
	activeGame = game.executable;
	phaseClock.restart();

	obs_log(LOG_INFO, "starting recording for %s into '%s'", utf8(activeGame).constData(),
		utf8(QDir::toNativeSeparators(outputDir)).constData());
	obs_frontend_recording_start();
}

void AutoRecorder::endRecording()
{
	phase = Phase::StopRequested;
	phaseClock.restart();
	obs_log(LOG_INFO, "stopping recording for %s", utf8(activeGame).constData());
	obs_frontend_recording_stop();
}

void AutoRecorder::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		/* OBS has read the path by now, so the user's setting can go back. */
		pathOverride.restore();
		if (phase == Phase::Starting) {
			phase = Phase::Recording;
			recordingClock.start();
		}
		emit statusChanged();
		break;

	case OBS_FRONTEND_EVENT_RECORDING_STOPPED: {
		pathOverride.restore();
		const bool closedGame = phase == Phase::StopRequested;
		if (phase == Phase::Starting || phase == Phase::Recording || phase == Phase::Stopping) {
			/* Stopped by the user (or by an error) while the game is still open:
			   don't start again until the game is closed and relaunched. */
			suppressedGame = activeGame.toLower();
			obs_log(LOG_INFO,
				"recording for %s was stopped outside Autopilot; waiting for the game to close",
				utf8(activeGame).constData());
		}
		if (phase != Phase::Idle && phase != Phase::Pending)
			resetToIdle();
		/* The game is gone, so its session is over. If it was stopped by hand, the
		   scene stays until the game closes (handled in tick()). */
		if (closedGame)
			returnToPreviousScene();
		emit statusChanged();
		break;
	}

	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		/* Never let the override leak into another profile. */
		pathOverride.restore();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		/* Scene names from one collection mean nothing in the next. */
		previousScene.clear();
		gameScene.clear();
		sceneOwner.clear();
		break;

	default:
		break;
	}
}

AutoRecorder::Status AutoRecorder::status() const
{
	Status s;
	s.phase = phase;
	s.automationEnabled = cfg.automationEnabled;
	s.game = activeGame;
	s.outputDir = outputDir;
	s.manualRecording = phase == Phase::Idle && obs_frontend_recording_active();
	if (phase == Phase::Pending || phase == Phase::Stopping)
		s.secondsRemaining = remainingSeconds(countdownDelay);
	if (recordingClock.isValid())
		s.recordingSeconds = recordingClock.elapsed() / 1000;

	s.games.reserve(cfg.games.size());
	for (const GameEntry &game : cfg.games)
		s.games.append({game.executable, lastRunning.contains(game.executable.toLower())});
	return s;
}
