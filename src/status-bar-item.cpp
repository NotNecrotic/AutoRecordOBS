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

#include "status-bar-item.hpp"

#include <obs-module.h>

#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>

#include "auto-recorder.hpp"
#include "settings-dialog.hpp"
#include "theme.hpp"

namespace {

constexpr int kIconSize = 14;

const QColor kFallbackWarning(0xE5, 0xAF, 0x24);
const QColor kFallbackMuted(0x99, 0x99, 0x99);

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* "VRChat.exe" -> "VRChat": the status bar has little room. */
QString shortName(const QString &executable)
{
	return QFileInfo(executable).completeBaseName();
}

} // namespace

StatusBarItem::StatusBarItem(AutoRecorder *recorder_, const char *dockId_, QWidget *parent)
	: QWidget(parent),
	  recorder(recorder_),
	  dockId(QString::fromUtf8(dockId_))
{
	icon = new QLabel(this);
	icon->setFixedSize(kIconSize, kIconSize);
	text = new QLabel(this);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(6, 0, 6, 0);
	layout->setSpacing(4);
	layout->addWidget(icon);
	layout->addWidget(text);

	setCursor(Qt::PointingHandCursor);

	connect(recorder, &AutoRecorder::statusChanged, this, &StatusBarItem::refresh);
	connect(&clock, &QTimer::timeout, this, &StatusBarItem::refresh);
	clock.start(1000);

	loadThemeColors();
	refresh();
}

void StatusBarItem::loadThemeColors()
{
	updatingTheme = true;
	warning = Theme::classColor(this, "text-warning", kFallbackWarning);
	muted = Theme::classColor(this, "text-muted", kFallbackMuted);
	updatingTheme = false;
}

void StatusBarItem::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (!updatingTheme && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)) {
		loadThemeColors();
		refresh();
	}
}

void StatusBarItem::refresh()
{
	if (!recorder) {
		clock.stop();
		hide();
		return;
	}

	using Phase = AutoRecorder::Phase;
	const AutoRecorder::Status s = recorder->status();
	const QString game = shortName(s.game);

	QString glyph = QStringLiteral("idle");
	QColor color = muted;
	QString state;

	switch (s.phase) {
	case Phase::Idle:
		state = tr_("Bar.Idle");
		break;
	case Phase::Pending:
		glyph = QStringLiteral("waiting");
		color = warning;
		state = tr_("Bar.StartsIn").arg(game).arg(s.secondsRemaining);
		break;
	case Phase::Starting:
		glyph = QStringLiteral("waiting");
		color = warning;
		state = tr_("Bar.Starting").arg(game);
		break;
	case Phase::Recording:
		glyph = QStringLiteral("record");
		state = game;
		break;
	case Phase::Stopping:
		glyph = QStringLiteral("waiting");
		color = warning;
		state = tr_("Bar.StopsIn").arg(game).arg(s.secondsRemaining);
		break;
	case Phase::StopRequested:
		glyph = QStringLiteral("waiting");
		color = warning;
		state = tr_("Bar.Stopping").arg(game);
		break;
	}

	const bool ownsRecording = s.phase != Phase::Idle && s.phase != Phase::Pending;
	if (!s.automationEnabled && !ownsRecording) {
		glyph = QStringLiteral("paused");
		color = muted;
		state = tr_("Bar.Paused");
	}

	const QIcon stateIcon = glyph == QStringLiteral("record") ? Theme::recordingIcon()
								  : Theme::glyph(glyph, color, kIconSize);
	icon->setPixmap(stateIcon.pixmap(kIconSize, kIconSize));
	text->setText(tr_("Bar.Label").arg(state));

	QStringList tip;
	if (ownsRecording && !s.outputDir.isEmpty())
		tip << tr_("Bar.SavingTo").arg(QDir::toNativeSeparators(s.outputDir));
	if (!s.automationEnabled && ownsRecording)
		tip << tr_("Bar.PausedStillRecording");
	tip << tr_("Bar.ClickHint");
	setToolTip(tip.join('\n'));
}

void StatusBarItem::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
		showMenu();
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void StatusBarItem::showMenu()
{
	if (!recorder)
		return;

	QMenu menu(this);
	QAction *pause = menu.addAction(tr_("Menu.PauseAutomation"));
	pause->setCheckable(true);
	pause->setChecked(!recorder->automationEnabled());
	menu.addSeparator();
	QAction *settings = menu.addAction(tr_("Menu.OpenSettings"));
	QAction *dock = menu.addAction(tr_("Menu.ShowDock"));

	/* Open above the status bar so it doesn't fall off the bottom of the screen. */
	const QSize size = menu.sizeHint();
	QAction *chosen = menu.exec(mapToGlobal(QPoint(0, -size.height())));

	if (!recorder || !chosen)
		return;
	if (chosen == pause) {
		recorder->setAutomationEnabled(!pause->isChecked());
	} else if (chosen == settings) {
		SettingsDialog::showFor(recorder, window());
	} else if (chosen == dock) {
		if (auto *widget = window()->findChild<QDockWidget *>(dockId)) {
			widget->show();
			widget->raise();
		}
	}
}
