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

#include "update-checker.hpp"

#include <obs-module.h>

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QUrl>
#include <QVersionNumber>

#include <memory>
#include <string>

#include <windows.h>
#include <winhttp.h>

#include "auto-recorder.hpp"
#include "plugin-support.h"

namespace {

/* Where releases are published. GitHub redirects the API if the repository is renamed. */
constexpr const wchar_t *kApiHost = L"api.github.com";
constexpr const wchar_t *kLatestReleasePath = L"/repos/NotNecrotic/obs-autopilot/releases/latest";
constexpr const char *kReleasesPage = "https://github.com/NotNecrotic/obs-autopilot/releases/latest";

constexpr int kStartupDelayMs = 10 * 1000;
constexpr int kDailyMs = 24 * 60 * 60 * 1000;
/* Short timeouts keep OBS from waiting on a slow network when it closes mid-check. */
constexpr int kTimeoutMs = 5000;
constexpr qsizetype kMaxResponseBytes = 1024 * 1024;

UpdateChecker *activeInstance = nullptr;

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

struct InternetHandle {
	HINTERNET handle = nullptr;
	explicit InternetHandle(HINTERNET h) : handle(h) {}
	~InternetHandle()
	{
		if (handle)
			WinHttpCloseHandle(handle);
	}
	InternetHandle(const InternetHandle &) = delete;
	InternetHandle &operator=(const InternetHandle &) = delete;
	explicit operator bool() const { return handle != nullptr; }
};

QString lastErrorText(const char *step)
{
	return QStringLiteral("%1 failed (error %2)").arg(QString::fromLatin1(step)).arg(GetLastError());
}

/* "v3.1.0" / "3.1.0" -> 3.1.0 */
QVersionNumber parseVersion(QString text)
{
	text = text.trimmed();
	if (text.startsWith('v', Qt::CaseInsensitive))
		text.remove(0, 1);
	return QVersionNumber::fromString(text);
}

} // namespace

UpdateChecker::UpdateChecker(AutoRecorder *recorder_, QObject *parent) : QObject(parent), recorder(recorder_)
{
	activeInstance = this;

	dailyTimer.setInterval(kDailyMs);
	connect(&dailyTimer, &QTimer::timeout, this, &UpdateChecker::checkNow);
	startupDelay.setSingleShot(true);
	startupDelay.setInterval(kStartupDelayMs);
	connect(&startupDelay, &QTimer::timeout, this, &UpdateChecker::checkNow);

	if (recorder)
		connect(recorder, &AutoRecorder::configChanged, this, &UpdateChecker::applyConfig);
}

UpdateChecker::~UpdateChecker()
{
	if (activeInstance == this)
		activeInstance = nullptr;
	/* A check in flight finishes within the WinHTTP timeouts. */
	if (worker) {
		worker->wait();
		delete worker;
	}
}

UpdateChecker *UpdateChecker::instance()
{
	return activeInstance;
}

QString UpdateChecker::currentVersion()
{
	return QString::fromUtf8(PLUGIN_VERSION);
}

void UpdateChecker::start()
{
	started = true;
	applyConfig();
}

void UpdateChecker::applyConfig()
{
	if (!started)
		return;

	const bool enabled = recorder && recorder->config().checkForUpdates;
	if (enabled && !dailyTimer.isActive()) {
		dailyTimer.start();
		startupDelay.start();
	} else if (!enabled) {
		dailyTimer.stop();
		startupDelay.stop();
		/* Turning checks off also hides an update that was already found. */
		if (currentState != State::Checking)
			setState(State::Idle);
	}
}

void UpdateChecker::checkNow()
{
	if (worker)
		return; /* Already checking. */

	setState(State::Checking);

	auto response = std::make_shared<Response>();
	worker = QThread::create([response]() { *response = fetchLatestRelease(); });
	connect(worker, &QThread::finished, this, [this, response]() {
		worker->deleteLater();
		worker = nullptr;
		handleResponse(*response);
	});
	worker->start(QThread::LowPriority);
}

UpdateChecker::Response UpdateChecker::fetchLatestRelease()
{
	Response result;
	const std::wstring userAgent = L"Autopilot-OBS-Plugin/" + currentVersion().toStdWString();

	InternetHandle session(WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
					   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session) {
		result.error = lastErrorText("WinHttpOpen");
		return result;
	}
	WinHttpSetTimeouts(session.handle, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

	InternetHandle connection(WinHttpConnect(session.handle, kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
	if (!connection) {
		result.error = lastErrorText("WinHttpConnect");
		return result;
	}

	InternetHandle request(WinHttpOpenRequest(connection.handle, L"GET", kLatestReleasePath, nullptr,
						  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
						  WINHTTP_FLAG_SECURE));
	if (!request) {
		result.error = lastErrorText("WinHttpOpenRequest");
		return result;
	}

	const wchar_t *headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
	if (!WinHttpSendRequest(request.handle, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(request.handle, nullptr)) {
		result.error = lastErrorText("Request");
		return result;
	}

	DWORD status = 0;
	DWORD size = sizeof(status);
	if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
		result.error = lastErrorText("WinHttpQueryHeaders");
		return result;
	}
	result.status = static_cast<int>(status);

	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request.handle, &available)) {
			result.error = lastErrorText("WinHttpQueryDataAvailable");
			return result;
		}
		if (available == 0)
			break;
		if (result.body.size() + available > kMaxResponseBytes) {
			result.error = QStringLiteral("response too large");
			return result;
		}
		const qsizetype offset = result.body.size();
		result.body.resize(offset + available);
		DWORD read = 0;
		if (!WinHttpReadData(request.handle, result.body.data() + offset, available, &read)) {
			result.error = lastErrorText("WinHttpReadData");
			return result;
		}
		result.body.resize(offset + read);
	}
	return result;
}

void UpdateChecker::handleResponse(const Response &response)
{
	if (!response.error.isEmpty()) {
		error = response.error;
		obs_log(LOG_INFO, "update check failed: %s", error.toUtf8().constData());
		setState(State::Failed);
		return;
	}

	if (response.status == 404) {
		/* No releases published yet. */
		latest.clear();
		setState(State::UpToDate);
		return;
	}
	if (response.status != 200) {
		error = QStringLiteral("HTTP %1").arg(response.status);
		obs_log(LOG_INFO, "update check failed: %s", error.toUtf8().constData());
		setState(State::Failed);
		return;
	}

	const QJsonObject release = QJsonDocument::fromJson(response.body).object();
	const QString tag = release.value(QStringLiteral("tag_name")).toString();
	const QVersionNumber latestVersion = parseVersion(tag);
	if (latestVersion.isNull()) {
		error = tr_("Updates.BadResponse");
		obs_log(LOG_INFO, "update check failed: unexpected response");
		setState(State::Failed);
		return;
	}

	latest = latestVersion.toString();
	url = release.value(QStringLiteral("html_url")).toString();
	const bool newer = latestVersion > parseVersion(currentVersion());
	if (newer)
		obs_log(LOG_INFO, "update available: %s (running %s)", latest.toUtf8().constData(),
			currentVersion().toUtf8().constData());
	setState(newer ? State::UpdateAvailable : State::UpToDate);
}

void UpdateChecker::setState(State state)
{
	currentState = state;
	emit stateChanged();
}

void UpdateChecker::openReleasePage() const
{
	/* Only ever open a GitHub page, whatever the response said. */
	QUrl target(url);
	if (!target.isValid() || target.scheme() != QStringLiteral("https") ||
	    target.host() != QStringLiteral("github.com"))
		target = QUrl(QString::fromLatin1(kReleasesPage));
	QDesktopServices::openUrl(target);
}
