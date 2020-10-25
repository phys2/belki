#include "scattertab.h"
#include "chart.h"
#include "../compute/features.h"
#include "../profiles/plotactions.h"

#include <QMainWindow>

ScatterTab::ScatterTab(QWidget *parent) :
    Viewer(new QMainWindow, parent)
{
	setupUi(qobject_cast<QMainWindow*>(widget));

	// setup toolbar
	auto anchor = actionCycleBackward;
	toolBar->insertWidget(anchor, dimensionLabel);
	toolBar->insertWidget(anchor, dimXSelect);

	toolBar->addSeparator();
	toolBar->addWidget(dimensionLabel_2);
	toolBar->addWidget(dimYSelect);

	auto capturePlot = PlotActions::createCapturePlotActions(toolBar);
	PlotActions::addCaptureButton(capturePlot, toolBar);

	// remove container we picked from
	topBar->deleteLater();

	/* connect toolbar actions */
	connect(actionCycleForward, &QAction::triggered, [this] {
		auto s = dimXSelect;
		selectDimension((s->currentIndex() + 1) % s->count());
	});
	connect(actionCycleBackward, &QAction::triggered, [this] {
		auto s = dimXSelect;
		selectDimension((s->count() + s->currentIndex() - 1) % s->count());
	});
	auto requestExport = [this] (bool toFile) {
		auto descript = QString("%1 – %2").arg(dimXSelect->currentText(),
		                                       dimYSelect->currentText());
		emit exportRequested(view, descript, toFile);
	};
	for (auto act : {capturePlot.head, capturePlot.toFile})
		connect(act, &QAction::triggered, this, [requestExport] { requestExport(true); });
	connect(capturePlot.toClipboard, &QAction::triggered,
	        this, [requestExport] { requestExport(false); });
	connect(dimXSelect, qOverload<int>(&QComboBox::activated),
	        this, &ScatterTab::selectDimension);
	connect(dimYSelect, qOverload<int>(&QComboBox::activated),
	        this, &ScatterTab::selectSecondaryDimension);

	updateIsEnabled();
}

ScatterTab::~ScatterTab()
{
	deselectDataset(); // avoid double delete
}

void ScatterTab::setWindowState(std::shared_ptr<WindowState> s)
{
	Viewer::setWindowState(s);
	view->toggleOpenGL(s->useOpenGl);

	/* connect state change signals (specify receiver so signal is cleaned up!) */
	auto ws = s.get();
	connect(ws, &WindowState::openGlToggled, this, [this] () {
		view->toggleOpenGL(windowState->useOpenGl);
	});
}

void ScatterTab::selectDataset(unsigned id)
{
	bool enabled = selectData(id);
	if (!enabled)
		return;

	auto &current = selected();
	// update dimensionSelects
	refillDimensionSelects();
	if (current.dimension < 0) {
		selectDimension(0);  // scene is still empty
	} else {
		// dimX has a direct index mapping, but dimY uses userData (due to index shifts)
		dimXSelect->setCurrentIndex(current.dimension);
		dimYSelect->setCurrentIndex(dimYSelect->findData(current.secondaryDimension));
	}

	view->switchChart(current.scene.get());
}

void ScatterTab::deselectDataset()
{
	view->releaseChart();
	Viewer::deselectDataset();
}

void ScatterTab::addDataset(Dataset::Ptr data)
{
	auto &state = addData<DataState>(data);
	state.hasScores = data->peek<Dataset::Base>()->hasScores();
	if (!state.hasScores)
		state.secondaryDimension = 1;
	state.scene = std::make_unique<Chart>(data, view->getConfig());

	auto scene = state.scene.get();
	scene->setState(windowState);

	/* connect outgoing signals */
	connect(scene, &Chart::cursorChanged, this, &Viewer::proteinsHighlighted);
}

void ScatterTab::refillDimensionSelects(bool onlySecondary)
{
	auto d = selected().data->peek<Dataset::Base>();

	// re-fill dimYSelect
	dimYSelect->clear();
	if (selected().hasScores)
		dimYSelect->addItem("Score", -1);
	for (auto i = 0; i < d->dimensions.size(); ++i) {
		//if (i == current().dimension)	continue;
		dimYSelect->addItem(d->dimensions.at(i), i);
	}

	if (onlySecondary)
		return;

	// re-fill dimXSelect
	dimXSelect->clear();
	dimXSelect->addItems(d->dimensions);
	for (auto a : {actionCycleForward, actionCycleBackward})
		a->setEnabled(dimXSelect->count() > 1);
}

void ScatterTab::selectDimension(int index)
{
	selected().dimension = index;
	dimXSelect->setCurrentIndex(index);

	// update dimY state and plot
	refillDimensionSelects(true);
	selectSecondaryDimension(dimYSelect->findData(selected().secondaryDimension));
}

void ScatterTab::selectSecondaryDimension(int index)
{
	auto &current = selected();
	current.secondaryDimension = dimYSelect->itemData(index).toInt();
	dimYSelect->setCurrentIndex(index);

	bool displayScores = current.secondaryDimension < 0;

	auto d = current.data->peek<Dataset::Base>();
	auto dim = (size_t)current.dimension;
	auto dim2 = (size_t)current.secondaryDimension;
	auto points = (displayScores
	               ? features::scatter(d->features, dim, d->scores, dim)
	               : features::scatter(d->features, dim, d->features, dim2));
	d.unlock();
	current.scene->display(points);

	if (displayScores) {
		current.scene->setTitles("Value", "Score");
	} else {
		current.scene->setTitles(dimXSelect->currentText(), dimYSelect->currentText());
	}
}

bool ScatterTab::updateIsEnabled()
{
	bool on = Viewer::updateIsEnabled();
	widget->setEnabled(on);
	view->setVisible(on);

	return on;
}
