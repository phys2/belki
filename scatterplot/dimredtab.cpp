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
	view->setRubberBand(QtCharts::QChartView::RectangleRubberBand); // TODO: issue #5

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
		guiState.preferredDisplay = name;
	});

	/* connect incoming signals */
	connect(this, &Viewer::inUpdateColorset, [this] (auto colors) {
		guiState.colorset = colors;
		if (current)
			current().scene->updateColorset(colors);
	});
	connect(this, &Viewer::inTogglePartitions, [this] (bool show) {
		guiState.showPartitions = show;
		if (current)
			current().scene->togglePartitions(show);
	});
	connect(this, &Viewer::inToggleMarker, [this] (ProteinId id, bool present) {
		// we do not keep track of markers for inactive scenes
		if (current)
			current().scene->toggleMarker(id, present);
	});

	updateEnabled();
}

DimredTab::~DimredTab()
{
	view->setChart(new QtCharts::QChart()); // release ownership
}

void DimredTab::selectDataset(unsigned id)
{
	if (current)
		disconnect(current().data.get());

	current = {id, &content[id]};

	updateMenus();
	updateEnabled();

	if (current) {
		// pass guiState onto chart
		auto scene = current().scene.get();
		scene->updateColorset(guiState.colorset);
		scene->togglePartitions(guiState.showPartitions);
		scene->updateMarkers();
		view->setChart(scene);

		/* hook into dataset updates */
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
	state.scene = std::make_unique<Chart>(data);

	auto scene = state.scene.get();
	scene->setTitles("dim 1", "dim 2");

	/* connect outgoing signals */
	connect(scene, &Chart::markerToggled, this, &Viewer::markerToggled);
	connect(scene, &Chart::cursorChanged, this, &Viewer::cursorChanged);
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
	guiState.preferredDisplay = id;
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
	for (auto &i : {guiState.preferredDisplay, previous, least}) {
		if (d->display.count(i)) {
			selectDisplay(i);
			break;
		}
	}
}

void DimredTab::updateEnabled()
{
	bool on = current;
	setEnabled(on);
	view->setVisible(on);
}
