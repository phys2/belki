#include "dimredtab.h"
#include "chart.h"
#include "compute/dimred.h"

#include <QMenu>
#include <QToolButton>
#include <QtConcurrent>

DimredTab::DimredTab(QWidget *parent) :
    Viewer(parent)
{
	setupUi(this);

	// setup toolbar
	auto anchor = actionCycleBackward;
	toolBar->insertWidget(anchor, transformLabel);
	toolBar->insertWidget(anchor, transformSelect);

	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	// let compute button display menu without holding mouse
	auto btn = qobject_cast<QToolButton*>(toolBar->widgetForAction(actionComputeDisplay));
	btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);

	// remove container we picked from
	topBar->deleteLater();

	/* connect toolbar actions */
	connect(actionCycleForward, &QAction::triggered, [this] {
		auto s = transformSelect;
		selectDisplay(s->itemText((s->currentIndex() + 1) % s->count()));
	});
	connect(actionCycleBackward, &QAction::triggered, [this] {
		auto s = transformSelect;
		selectDisplay(s->itemText((s->count() + s->currentIndex() - 1) % s->count()));
	});
	connect(actionSavePlot, &QAction::triggered, [this] {
		emit exportRequested(view, transformSelect->currentText());
	});
	connect(transformSelect, qOverload<const QString&>(&QComboBox::activated),
	        [this] (auto name) {
		selectDisplay(name);
		tabState.preferredDisplay = name;
	});

	updateEnabled();
}

DimredTab::~DimredTab()
{
	view->releaseChart(); // avoid double delete
}

void DimredTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	view->toggleOpenGL(s->useOpenGl);

	/* connect state change signals (specify receiver so signal is cleaned up!) */
	auto ws = s.get();
	connect(ws, &WindowState::openGlToggled, this, [this] () {
		view->toggleOpenGL(windowState->useOpenGl);
	});
}

void DimredTab::selectDataset(unsigned id)
{
	if (current)
		disconnect(current().data.get());

	current = {id, &content[id]};

	updateMenus();
	bool enabled = updateEnabled();

	if (enabled) {
		view->switchChart(current().scene.get());

		/* hook into dataset updates (specify receiver so signal is cleaned up!) */
		connect(current().data.get(), &Dataset::update, this, [this] (Dataset::Touched touched) {
			if (!(touched & Dataset::Touch::DISPLAY))
				return;
			updateMenus();
		});
	}
}

void DimredTab::addDataset(Dataset::Ptr data)
{
	auto id = data->id();
	auto &state = content[id]; // emplace (note: ids are never recycled)
	state.data = data;
	state.scene = std::make_unique<Chart>(data, view->getConfig());

	auto scene = state.scene.get();
	scene->setState(windowState);
	scene->setTitles("dim 1", "dim 2");

	/* connect outgoing signals */
	connect(scene, &Chart::cursorChanged, this, &Viewer::proteinsHighlighted);
}

void DimredTab::removeDataset(unsigned id)
{
	if (current.id == id) {
		current = {};
		updateEnabled();
	}
	content.erase(id); // kills both dataset and scene
}

void DimredTab::selectDisplay(const QString &name)
{
	if (!current || name.isEmpty())
		return;
	transformSelect->setCurrentText(name);

	if (current().displayName == name)
		return;

	auto d = current().data->peek<Dataset::Representation>();
	current().scene->display(d->display.at(name));
	current().displayName = name;
}

QString DimredTab::currentMethod() const
{
	return transformSelect->currentText();
}

void DimredTab::computeDisplay(const QString &name, const QString &id) {
	if (!current)
		return;
	tabState.preferredDisplay = id;
	auto d = current().data;
	QtConcurrent::run([d,name] { d->computeDisplay(name); });
}

void DimredTab::updateMenus() {
	/* set transform select entries from available displays */
	transformSelect->clear();
	for (auto a : {actionCycleForward, actionCycleBackward})
		a->setEnabled(false);

	if (!current)
		return;

	auto d = current().data->peek<Dataset::Representation>();
	for (auto &[name, _] : d->display)
		transformSelect->addItem(name);

	if (transformSelect->count() > 1) {
		for (auto a : {actionCycleForward, actionCycleBackward})
			a->setEnabled(true);
	}

	/* add all methods that are not here yet to compute offers */
	auto methods = dimred::availableMethods();
	auto menu = new QMenu(this->window());
	for (const auto& m : methods) {
		if (transformSelect->findText(m.id) >= 0)
			continue;

		menu->addAction(m.description, [this, m] { computeDisplay(m.name, m.id); });
	}
	actionComputeDisplay->setMenu(menu);

	/* select a display */
	if (d->display.empty())
		return; // nothing available

	auto previous = current().displayName;
	auto least = d->display.rbegin()->first;
	for (auto &i : {tabState.preferredDisplay, previous, least}) {
		if (d->display.count(i)) {
			selectDisplay(i);
			break;
		}
	}
}

bool DimredTab::updateEnabled()
{
	bool on = current;
	on = on && current().data->peek<Dataset::Base>()->dimensions.count() > 2;

	setEnabled(on);
	view->setVisible(on);
	return on;
}
