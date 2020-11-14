#include "dimredtab.h"
#include "chart.h"

#include "jobregistry.h"
#include "../compute/features.h"
#include "../profiles/plotactions.h"

#include <QMainWindow>
#include <QMenu>
#include <QToolButton>

DimredTab::DimredTab(QWidget *parent) :
    Viewer(new QMainWindow, parent)
{
	setupUi(qobject_cast<QMainWindow*>(widget));

	// setup toolbar
	auto anchor = actionCycleBackward;
	toolBar->insertWidget(anchor, transformLabel);
	toolBar->insertWidget(anchor, transformSelect);

	auto capturePlot = PlotActions::createCapturePlotActions(toolBar);
	PlotActions::addCaptureButton(capturePlot, toolBar);

	// initialize compute menu and let button display menu without holding mouse
	actionComputeDisplay->setMenu(new QMenu(widget));
	auto btn = qobject_cast<QToolButton*>(toolBar->widgetForAction(actionComputeDisplay));
	btn->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);

	// remove container we picked from
	stockpile->deleteLater();

	/* connect toolbar actions */
	connect(actionCycleForward, &QAction::triggered, [this] {
		auto s = transformSelect;
		selectDisplay(s->itemText((s->currentIndex() + 1) % s->count()));
	});
	connect(actionCycleBackward, &QAction::triggered, [this] {
		auto s = transformSelect;
		selectDisplay(s->itemText((s->count() + s->currentIndex() - 1) % s->count()));
	});
	auto requestExport = [this] (bool toFile) {
		emit exportRequested(view, transformSelect->currentText(), toFile);
	};
	for (auto act : {capturePlot.head, capturePlot.toFile})
		connect(act, &QAction::triggered, this, [requestExport] { requestExport(true); });
	connect(capturePlot.toClipboard, &QAction::triggered,
	        this, [requestExport] { requestExport(false); });
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
	connect(transformSelect, qOverload<const QString&>(&QComboBox::activated),
#else
	connect(transformSelect, &QComboBox::textActivated,
#endif
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

	auto &data = selected().data;

	/* hook into dataset updates (specify receiver so signal is cleaned up!) */
	connect(data.get(), &Dataset::update, this, [this] (Dataset::Touched touched) {
		if (!(touched & Dataset::Touch::DISPLAY))
			return;
		updateMenus();
	});

	/* special handling for datasets w/ only two dimensions: fall back to scatter plot.
	 * code taken from ScatterTab:selectSecondaryDimension() */
	if (data->peek<Dataset::Base>()->dimensions.size() == 2) {
		auto d = data->peek<Dataset::Base>();
		auto points = features::scatter(d->features, 0, d->features, 1);
		auto labelX = d->dimensions.at(0), labelY = d->dimensions.at(1);
		d.unlock();
		auto &scene = selected().scene;
		scene->display(points);
		scene->setTitles(labelX, labelY);
	}

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
	if (!on)
		return false; // return early to avoid access on selected()

	auto dims = selected().data->peek<Dataset::Base>()->dimensions.size();
	on = dims > 1;

	widget->setEnabled(on);
	view->setVisible(on);

	bool enoughDims = dims > 2;
	// if not enough dimensions, we fall back to a scatter plot; disable anything transform-related
	for (auto action : {actionComputeDisplay, actionCycleBackward, actionCycleForward})
		action->setEnabled(enoughDims);
	transformLabel->setEnabled(enoughDims);
	transformSelect->setEnabled(enoughDims);

	return on;
}
