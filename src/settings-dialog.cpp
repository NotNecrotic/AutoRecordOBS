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

#include "settings-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "auto-recorder.hpp"
#include "capture-target.hpp"
#include "game-picker.hpp"
#include "recording-path.hpp"
#include "theme.hpp"

namespace {

constexpr int kMaxDelay = 3600;
constexpr int kNavWidth = 150;

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QStringList currentSceneNames()
{
	QStringList result;
	char **names = obs_frontend_get_scene_names();
	if (!names)
		return result;
	for (char **name = names; *name; name++)
		result.append(QString::fromUtf8(*name));
	bfree(names);
	return result;
}

QSpinBox *makeSecondsSpin(QWidget *parent, int minimum, int maximum)
{
	auto *spin = new QSpinBox(parent);
	spin->setRange(minimum, maximum);
	spin->setSuffix(QStringLiteral(" s"));
	return spin;
}

/* Form layout configured the way OBS's Settings pages lay out their rows. */
QFormLayout *makeForm(QWidget *parent)
{
	auto *form = new QFormLayout(parent);
	form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	return form;
}

/* A form label followed by OBS's "?" help icon; hovering either shows the description, as in OBS's own settings. */
QWidget *labelWithHelp(QWidget *parent, const QString &text, const QString &help, QLabel **helpIconOut = nullptr)
{
	auto *box = new QWidget(parent);
	auto *label = new QLabel(text, box);
	auto *icon = new QLabel(box);
	icon->setPixmap(Theme::helpIcon().pixmap(16, 16));
	icon->setToolTip(help);
	label->setToolTip(help);

	auto *row = new QHBoxLayout(box);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(4);
	row->addStretch(1);
	row->addWidget(label);
	row->addWidget(icon);

	if (helpIconOut)
		*helpIconOut = icon;
	return box;
}

/* Small icon-only list button, as under OBS's Scenes/Sources lists and in the Filters window. */
QPushButton *makeToolButton(QWidget *parent, const QString &cls, const QString &fallbackIcon, const QString &tip)
{
	auto *button = new QPushButton(parent);
	button->setIcon(QIcon(fallbackIcon));
	button->setToolTip(tip);
	button->setAccessibleName(tip);
	button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	Theme::setClasses(button, QStringLiteral("btn-tool ") + cls);
	return button;
}

/* OBS's own game-capture icon from the active theme, so the Games page matches the Sources list. */
QIcon gameIcon(QWidget *context)
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (main) {
		const QIcon icon = main->property("gameCapIcon").value<QIcon>();
		if (!icon.isNull())
			return icon;
	}
	const QString fallback = QStringLiteral(":/res/images/sources/gamepad.svg");
	if (QFile::exists(fallback))
		return QIcon(fallback);
	return Theme::glyph(QStringLiteral("gamepad"), context->palette().color(QPalette::WindowText));
}

} // namespace

void SettingsDialog::showFor(AutoRecorder *recorder, QWidget *parent)
{
	static QPointer<SettingsDialog> instance;
	if (!instance) {
		instance = new SettingsDialog(recorder, parent);
		instance->setAttribute(Qt::WA_DeleteOnClose);
	}
	instance->nav->setCurrentRow(0);
	instance->show();
	instance->raise();
	instance->activateWindow();
}

SettingsDialog::SettingsDialog(AutoRecorder *recorder_, QWidget *parent)
	: QDialog(parent),
	  recorder(recorder_),
	  sceneNames(currentSceneNames()),
	  baseDirectory(RecordingPathOverride::baseDirectory()),
	  captureSources(CaptureTarget::captureSourceNames())
{
	setWindowTitle(tr_("Settings.Title"));
	resize(820, 520);

	/* Page list on the left, like OBS's Settings window. */
	nav = new QListWidget(this);
	nav->setFixedWidth(kNavWidth);
	nav->setIconSize(QSize(16, 16));
	nav->addItem(new QListWidgetItem(Theme::classIcon(this, QStringLiteral("icon-gear"),
							  QStringLiteral(":/res/images/settings/general.svg")),
					 tr_("Settings.General")));
	nav->addItem(new QListWidgetItem(gameIcon(this), tr_("Settings.Games")));

	pages = new QStackedWidget(this);
	pages->addWidget(buildGeneralPage());
	pages->addWidget(buildGamesPage());

	auto *body = new QHBoxLayout();
	body->addWidget(nav);
	body->addWidget(pages, 1);

	auto *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
	applyButton = buttons->button(QDialogButtonBox::Apply);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(body, 1);
	layout->addWidget(buttons);

	connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		if (apply())
			accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(applyButton, &QAbstractButton::clicked, this, &SettingsDialog::apply);

	if (recorder) {
		load(recorder->config());
		/* The dock can add, remove and reorder games; show that unless there are unsaved edits here. */
		connect(recorder, &AutoRecorder::configChanged, this, [this]() {
			if (!dirty && recorder)
				load(recorder->config());
		});
	}
}

QWidget *SettingsDialog::buildGeneralPage()
{
	auto *page = new QWidget(this);

	checkInterval = makeSecondsSpin(page, 1, 60);
	startDelay = makeSecondsSpin(page, 0, kMaxDelay);
	stopDelay = makeSecondsSpin(page, 0, kMaxDelay);

	auto *detection = new QGroupBox(tr_("Settings.Detection"), page);
	auto *form = makeForm(detection);
	form->addRow(tr_("Settings.CheckInterval"), checkInterval);
	form->addRow(tr_("Settings.StartDelay"), startDelay);
	form->addRow(tr_("Settings.StopDelay"), stopDelay);

	for (QSpinBox *spin : {checkInterval, startDelay, stopDelay})
		connect(spin, &QSpinBox::valueChanged, this, &SettingsDialog::markDirty);

	defaultCaptureCombo = new QComboBox(page);
	defaultCaptureCombo->addItem(tr_("Settings.CaptureNone"), QString());
	for (const QString &name : captureSources)
		defaultCaptureCombo->addItem(name, name);

	connect(defaultCaptureCombo, &QComboBox::currentIndexChanged, this, [this]() {
		updateDefaultCaptureLabel();
		markDirty();
	});

	auto *capture = new QGroupBox(tr_("Settings.Capture"), page);
	auto *captureForm = makeForm(capture);
	captureForm->addRow(labelWithHelp(capture, tr_("Settings.CaptureSource"), tr_("Settings.CaptureSource.Help")),
			    defaultCaptureCombo);

	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(detection);
	layout->addWidget(capture);
	layout->addStretch(1);
	return page;
}

QWidget *SettingsDialog::buildGamesPage()
{
	auto *page = new QWidget(this);

	/* Left: list + buttons. */
	gamesView = new QListWidget(page);
	gamesView->setIconSize(QSize(16, 16));

	auto *addButton = makeToolButton(page, "icon-plus", ":/res/images/plus.svg", tr_("Settings.AddGame"));
	removeButton = makeToolButton(page, "icon-trash", ":/res/images/trash.svg", tr_("Settings.Remove"));
	upButton = makeToolButton(page, "icon-up", ":/res/images/up.svg", tr_("Settings.MoveUp"));
	downButton = makeToolButton(page, "icon-down", ":/res/images/down.svg", tr_("Settings.MoveDown"));

	auto *addMenu = new QMenu(addButton);
	GamePicker::fillAddMenu(addMenu, this, [this](const QString &exe) { addGame(exe); });
	connect(addButton, &QPushButton::clicked, this,
		[addButton, addMenu]() { addMenu->exec(addButton->mapToGlobal(QPoint(0, addButton->height()))); });

	auto *toolbar = new QHBoxLayout();
	toolbar->setSpacing(4); /* Same gap as the buttons in OBS's Filters window. */
	toolbar->addWidget(addButton);
	toolbar->addWidget(removeButton);
	toolbar->addWidget(upButton);
	toolbar->addWidget(downButton);
	toolbar->addStretch(1);
	auto *orderHelp = new QLabel(page);
	orderHelp->setPixmap(Theme::helpIcon().pixmap(16, 16));
	orderHelp->setToolTip(tr_("Settings.OrderHelp"));
	toolbar->addWidget(orderHelp);

	auto *listColumn = new QVBoxLayout();
	listColumn->addWidget(gamesView, 1);
	listColumn->addLayout(toolbar);

	auto *listBox = new QWidget(page);
	listBox->setLayout(listColumn);
	listBox->setFixedWidth(240);

	/* Right: details of the selected game. */
	executableEdit = new QLineEdit(page);
	executableEdit->setPlaceholderText(tr_("Settings.ExecutablePlaceholder"));
	auto *browseButton = new QPushButton(tr_("Settings.Browse"), page);
	auto *exeRow = new QHBoxLayout();
	exeRow->setContentsMargins(0, 0, 0, 0);
	exeRow->addWidget(executableEdit, 1);
	exeRow->addWidget(browseButton);

	sceneCombo = new QComboBox(page);
	sceneCombo->setEditable(true);
	sceneCombo->addItem(QString());
	sceneCombo->addItems(sceneNames);
	sceneCombo->lineEdit()->setPlaceholderText(tr_("Settings.NoSceneSwitch"));

	gameCaptureCombo = new QComboBox(page);
	gameCaptureCombo->addItem(QString(), QString()); /* "Default (...)" label set in updateDefaultCaptureLabel() */
	for (const QString &name : captureSources)
		gameCaptureCombo->addItem(name, name);

	subfolderEdit = new QLineEdit(page);
	subfolderEdit->setPlaceholderText(tr_("Settings.SubfolderPlaceholder"));

	auto *detailsGroup = new QGroupBox(tr_("Settings.GameDetails"), page);
	auto *form = makeForm(detailsGroup);
	form->addRow(tr_("Settings.Game"), exeRow);
	form->addRow(labelWithHelp(detailsGroup, tr_("Settings.Scene"), tr_("Settings.Scene.Help")), sceneCombo);
	form->addRow(labelWithHelp(detailsGroup, tr_("Settings.Subfolder"), QString(), &subfolderHelp), subfolderEdit);
	form->addRow(tr_("Settings.CaptureSource"), gameCaptureCombo);

	auto *detailsPage = new QWidget(page);
	auto *detailsLayout = new QVBoxLayout(detailsPage);
	detailsLayout->setContentsMargins(0, 0, 0, 0);
	detailsLayout->addWidget(detailsGroup);
	detailsLayout->addStretch(1);

	auto *emptyLabel = new QLabel(tr_("Settings.NoGameSelected"), page);
	emptyLabel->setAlignment(Qt::AlignCenter);
	emptyLabel->setWordWrap(true);
	Theme::setClasses(emptyLabel, "text-muted");

	details = new QStackedWidget(page);
	details->addWidget(emptyLabel);
	details->addWidget(detailsPage);

	auto *layout = new QHBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(listBox);
	layout->addWidget(details, 1);

	connect(gamesView, &QListWidget::currentRowChanged, this, &SettingsDialog::selectGame);
	connect(removeButton, &QPushButton::clicked, this, &SettingsDialog::removeSelected);
	connect(upButton, &QPushButton::clicked, this, [this]() { moveSelected(-1); });
	connect(downButton, &QPushButton::clicked, this, [this]() { moveSelected(1); });
	connect(browseButton, &QPushButton::clicked, this, [this]() {
		const QString name = GamePicker::browseForExecutable(this);
		if (!name.isEmpty())
			executableEdit->setText(name);
	});

	connect(executableEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
		if (loading || current < 0)
			return;
		gameList[current].executable = text.trimmed();
		refreshList();
		markDirty();
	});
	connect(sceneCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
		if (loading || current < 0)
			return;
		gameList[current].scene = text.trimmed();
		markDirty();
	});
	connect(gameCaptureCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (loading || current < 0)
			return;
		gameList[current].captureSource = gameCaptureCombo->itemData(index).toString();
		markDirty();
	});
	connect(subfolderEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
		if (loading || current < 0)
			return;
		gameList[current].subfolder = text.trimmed();
		updatePathPreview();
		markDirty();
	});

	return page;
}

void SettingsDialog::load(const PluginConfig &config)
{
	loading = true;
	checkInterval->setValue(config.checkInterval);
	startDelay->setValue(config.startDelay);
	stopDelay->setValue(config.stopDelay);
	defaultCaptureCombo->setCurrentIndex(captureIndex(defaultCaptureCombo, config.captureSource));
	updateDefaultCaptureLabel();

	const int keep = current;
	gameList = config.games;
	current = -1;
	gamesView->clear();
	refreshList();
	if (!gameList.isEmpty())
		gamesView->setCurrentRow(std::clamp(keep, 0, static_cast<int>(gameList.size()) - 1));
	showSelected();
	loading = false;
	setDirty(false);
}

void SettingsDialog::markDirty()
{
	if (!loading)
		setDirty(true);
}

void SettingsDialog::setDirty(bool changed)
{
	/* Like OBS's own Settings: Apply only lights up once something has changed. */
	dirty = changed;
	applyButton->setEnabled(changed);
}

void SettingsDialog::refreshList()
{
	const QIcon icon = gameIcon(this);
	while (gamesView->count() > gameList.size())
		delete gamesView->takeItem(gamesView->count() - 1);
	while (gamesView->count() < gameList.size())
		gamesView->addItem(new QListWidgetItem(icon, QString()));

	for (int i = 0; i < gameList.size(); i++) {
		const QString name = gameList[i].executable;
		QListWidgetItem *item = gamesView->item(i);
		item->setText(name.isEmpty() ? tr_("Settings.UnnamedGame") : name);
	}
	updateButtons();
}

void SettingsDialog::selectGame(int index)
{
	current = (index >= 0 && index < gameList.size()) ? index : -1;
	showSelected();
}

void SettingsDialog::showSelected()
{
	updateButtons();
	if (current < 0) {
		details->setCurrentIndex(0);
		return;
	}

	const bool wasLoading = loading;
	loading = true;
	const GameEntry &game = gameList[current];
	executableEdit->setText(game.executable);
	sceneCombo->setCurrentText(game.scene);
	subfolderEdit->setText(game.subfolder);
	gameCaptureCombo->setCurrentIndex(captureIndex(gameCaptureCombo, game.captureSource));
	loading = wasLoading;

	updatePathPreview();
	details->setCurrentIndex(1);
}

void SettingsDialog::updatePathPreview()
{
	const QString base = baseDirectory.isEmpty() ? tr_("Settings.RecordingPath")
						     : QDir::toNativeSeparators(baseDirectory);
	const QString subfolder = current >= 0 ? RecordingPathOverride::sanitizeSubfolder(gameList[current].subfolder)
					       : QString();
	const QString target = subfolder.isEmpty() ? base
						   : base + QDir::separator() + QDir::toNativeSeparators(subfolder);
	/* The tooltip shows where this game's recordings will actually go. */
	const QString help = tr_("Settings.Subfolder.Help").arg(target);
	subfolderHelp->setToolTip(help);
	if (auto *label = subfolderHelp->parentWidget()->findChild<QLabel *>())
		label->setToolTip(help);
}

int SettingsDialog::captureIndex(QComboBox *combo, const QString &source)
{
	/* A source that no longer exists is kept as an extra entry so saving doesn't silently drop it. */
	int index = combo->findData(source);
	if (index < 0) {
		combo->addItem(tr_("Settings.CaptureMissing").arg(source), source);
		index = combo->count() - 1;
	}
	return index;
}

void SettingsDialog::updateDefaultCaptureLabel()
{
	const QString shared = defaultCaptureCombo->currentData().toString();
	gameCaptureCombo->setItemText(0, shared.isEmpty() ? tr_("Settings.CaptureDefaultNone")
							  : tr_("Settings.CaptureDefault").arg(shared));
}

void SettingsDialog::updateButtons()
{
	const bool selected = current >= 0;
	removeButton->setEnabled(selected);
	upButton->setEnabled(selected && current > 0);
	downButton->setEnabled(selected && current < gameList.size() - 1);
}

void SettingsDialog::addGame(const QString &executable)
{
	GameEntry game;
	game.executable = executable;
	gameList.append(game);
	refreshList();
	gamesView->setCurrentRow(static_cast<int>(gameList.size()) - 1);
	subfolderEdit->setFocus();
	markDirty();
}

void SettingsDialog::removeSelected()
{
	if (current < 0)
		return;
	const int index = current;
	gameList.removeAt(index);
	current = -1;
	refreshList();
	if (!gameList.isEmpty())
		gamesView->setCurrentRow(std::min(index, static_cast<int>(gameList.size()) - 1));
	else
		showSelected();
	markDirty();
}

void SettingsDialog::moveSelected(int delta)
{
	const int target = current + delta;
	if (current < 0 || target < 0 || target >= gameList.size())
		return;
	gameList.swapItemsAt(current, target);
	refreshList();
	gamesView->setCurrentRow(target);
	markDirty();
}

bool SettingsDialog::collect(PluginConfig &config)
{
	config.checkInterval = checkInterval->value();
	config.startDelay = startDelay->value();
	config.stopDelay = stopDelay->value();
	config.captureSource = defaultCaptureCombo->currentData().toString();
	config.games.clear();

	QSet<QString> seen;
	for (int i = 0; i < gameList.size(); i++) {
		GameEntry game = gameList[i];
		game.executable = QFileInfo(game.executable.trimmed()).fileName();
		if (game.executable.isEmpty())
			continue;
		/* Process names always carry their extension; "Game" would never match. */
		if (!game.executable.contains('.'))
			game.executable += QStringLiteral(".exe");

		const QString key = game.executable.toLower();
		if (seen.contains(key)) {
			nav->setCurrentRow(1);
			gamesView->setCurrentRow(i);
			QMessageBox::warning(this, windowTitle(), tr_("Settings.Duplicate").arg(game.executable));
			return false;
		}
		seen.insert(key);

		const QString rawFolder = game.subfolder.trimmed();
		game.subfolder = RecordingPathOverride::sanitizeSubfolder(rawFolder);
		if (!rawFolder.isEmpty() && game.subfolder.isEmpty()) {
			nav->setCurrentRow(1);
			gamesView->setCurrentRow(i);
			QMessageBox::warning(this, windowTitle(), tr_("Settings.BadSubfolder").arg(rawFolder));
			subfolderEdit->setFocus();
			return false;
		}

		game.scene = game.scene.trimmed();
		config.games.append(game);
	}
	return true;
}

bool SettingsDialog::apply()
{
	if (!recorder)
		return true;

	PluginConfig config = recorder->config();
	if (!collect(config))
		return false;

	recorder->setConfig(config);
	/* Reload so the page shows the cleaned-up values that were saved. */
	load(recorder->config());
	return true;
}
