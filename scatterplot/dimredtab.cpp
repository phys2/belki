#include "dimredtab.h"
#include "chart.h"

#include "jobregistry.h"

#include <QMenu>
#include <QToolButton>

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

	// initialize compute menu and let button display menu without holding mouse
	actionComputeDisplay->setMenu(new QMenu(widget));
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

	updateIsEnabled();
}

DimredTab::~DimredTab()
{
	deselectDataset(); // avoid double delete
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
	bool enabled = selectData(id);

	updateMenus();
	if (!enabled)
		return;

	/* hook into dataset updates (specify receiver so signal is cleaned up!) */
	connect(selected().data.get(), &Dataset::update, this, [this] (Dataset::Touched touched) {
		if (!(touched & Dataset::Touch::DISPLAY))
			return;
		updateMenus();
	});

	view->switchChart(selected().scene.get());
}

void DimredTab::deselectDataset()
{
	view->releaseChart();
	Viewer::deselectDataset();
}

void DimredTab::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	state.scene = std::make_unique<Chart>(data, view->getConfig());

	auto scene = state.scene.get();
	scene->setState(windowState);
	scene->setTitles("dim 1", "dim 2");

	/* connect outgoing signals */
	connect(scene, &Chart::cursorChanged, this, &Viewer::proteinsHighlighted);
}

void DimredTab::selectDisplay(const QString &name)
{
	if (!haveData() || name.isEmpty())
		return;
	transformSelect->setCurrentText(name);

	auto &current = selected();
	if (current.displayName == name)
		return;

	auto d = current.data->peek<Dataset::Representations>();
	current.scene->display(d->displays.at(name));
	current.displayName = name;
}

QString DimredTab::currentMethod() const
{
	return transformSelect->currentText();
}

void DimredTab::computeDisplay(const dimred::Method &method) {
	tabState.preferredDisplay = method.id;
	if (!haveData())
		return;
	auto d = selected().data;
	Task task{[d,name=method.name] { d->computeDisplay(name); },
	          Task::Type::COMPUTE, {method.description, d->config().name}};
	// note: when we have a local progress indicator thingy, we can add it to monitors
	JobRegistry::run(task, windowState->jobMonitors);
}

void DimredTab::updateMenus() {
	/* set transform select entries from available displays */
	transformSelect->clear();
	for (auto a : {actionCycleForward, actionCycleBackward})
		a->setEnabled(false);

	if (!haveData())
		return;

	auto d = selected().data->peek<Dataset::Representations>();
	for (auto &[name, _] : d->displays)
		transformSelect->addItem(name);

	if (transformSelect->count() > 1) {
		for (auto a : {actionCycleForward, actionCycleBackward})
			a->setEnabled(true);
	}

	/* add all methods that are not here yet to compute offers */
	const auto& methods = dimred::availableMethods();
	auto menu = actionComputeDisplay->menu();
	menu->clear();
	for (const auto& m : methods) {
		if (transformSelect->findText(m.id) >= 0)
			continue;

		menu->addAction(m.description, [this, m] { computeDisplay(m); });
	}

	/* select a display */
	if (d->displays.empty())
		return; // nothing available

	auto previous = selected().displayName;
	auto least = d->displays.rbegin()->first;
	for (auto &i : {tabState.preferredDisplay, previous, least}) {
		if (d->displays.count(i)) {
			selectDisplay(i);
			break;
		}
	}
}

bool DimredTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	on = on && selected().data->peek<Dataset::Base>()->dimensions.count() > 2;

	setEnabled(on);
	view->setVisible(on);

	return on;
}
