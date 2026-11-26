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

#include "theme.hpp"

#include <obs-frontend-api.h>

#include <QFile>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QSvgRenderer>

namespace Theme {

void setClasses(QWidget *widget, const QString &classes)
{
	if (widget->property("class").toString() == classes)
		return;
	widget->setProperty("class", classes);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

QColor classColor(QWidget *context, const QString &cls, const QColor &fallback)
{
	/* Probe labels inside the context so dock-specific theme rules apply too. */
	QLabel plain(context);
	plain.hide();
	plain.ensurePolished();

	QLabel styled(context);
	styled.hide();
	styled.setProperty("class", cls);
	styled.ensurePolished();

	const QColor base = plain.palette().color(QPalette::WindowText);
	const QColor themed = styled.palette().color(QPalette::WindowText);
	return themed != base ? themed : fallback;
}

QIcon classIcon(QWidget *context, const QString &cls, const QString &fallbackResource)
{
	QPushButton probe(context);
	probe.hide();
	probe.setProperty("class", cls);
	probe.ensurePolished();

	const QIcon icon = probe.icon();
	return icon.isNull() ? QIcon(fallbackResource) : icon;
}

QIcon glyph(const QString &name, const QColor &color, int size)
{
	/* The dock refreshes every second; don't re-render the same SVG each time.
	   Intentionally never freed: pixmaps must not be destroyed after Qt shuts down. */
	static auto *cache = new QHash<QString, QIcon>();
	const QString key = QStringLiteral("%1|%2|%3").arg(name, color.name(QColor::HexArgb)).arg(size);
	if (auto it = cache->constFind(key); it != cache->constEnd())
		return *it;

	QFile file(QStringLiteral(":/autopilot/%1.svg").arg(name));
	if (!file.open(QIODevice::ReadOnly))
		return {};

	QByteArray svg = file.readAll();
	svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
	QSvgRenderer renderer(svg);

	QIcon icon;
	for (qreal ratio : {1.0, 1.5, 2.0}) {
		QPixmap pixmap(QSize(size, size) * ratio);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		renderer.render(&painter, QRectF(0, 0, pixmap.width(), pixmap.height()));
		painter.end();
		pixmap.setDevicePixelRatio(ratio);
		icon.addPixmap(pixmap);
	}
	cache->insert(key, icon);
	return icon;
}

QIcon recordingIcon()
{
	const QString obsIcon = QStringLiteral(":/res/images/recording-active.svg");
	if (QFile::exists(obsIcon))
		return QIcon(obsIcon);
	/* Same colour as OBS's icon, in case a future OBS moves the resource. */
	return glyph(QStringLiteral("record"), QColor(0xC3, 0x12, 0x12));
}

QIcon helpIcon()
{
	return QIcon(obs_frontend_is_theme_dark() ? QStringLiteral(":/res/images/help_light.svg")
						  : QStringLiteral(":/res/images/help.svg"));
}

} // namespace Theme
