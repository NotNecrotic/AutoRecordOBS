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

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;

/*
 * Helpers for blending in with OBS themes. OBS 30+ styles widgets through a
 * "class" property (e.g. "text-muted", "text-danger", "btn-tool icon-plus"),
 * so using the same classes lets the active theme decide colours and icons.
 */
namespace Theme {

/* Sets the OBS theme classes on a widget and re-applies the stylesheet. */
void setClasses(QWidget *widget, const QString &classes);

/* The text colour the current theme gives a class (e.g. "text-danger"), or fallback if the theme has none. */
QColor classColor(QWidget *context, const QString &cls, const QColor &fallback);

/* The icon the current theme gives a class (e.g. "icon-gear"), or the fallback resource if none. */
QIcon classIcon(QWidget *context, const QString &cls, const QString &fallbackResource);

/* One of the plugin's own single-colour SVG glyphs (":/autopilot/<name>.svg"), painted in the given colour. */
QIcon glyph(const QString &name, const QColor &color, int size = 16);

/* OBS's own red recording dot (the one beside REC in the status bar). */
QIcon recordingIcon();

/* OBS's "?" help icon for the current theme, as used beside settings that have a long description. */
QIcon helpIcon();

} // namespace Theme
