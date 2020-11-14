#include "plotactions.h"
#include "profilechart.h"
#include <QActionGroup>
#include <QToolBar>
#include <QToolButton>
#include <QMenu>
#include <QMainWindow>

QAction* PlotActions::createAction(QObject *parent, QIcon icon, QString title, QString tooltip, bool isToggle, QString shortcut)
{
	auto action = new QAction(icon, title, parent);
    action->setCheckable(isToggle);
	action->setIcon(icon);
	action->setToolTip(tooltip);
	if (!shortcut.isEmpty())
		action->setShortcut({shortcut});
	return action;
}

PlotActions::CapturePlotActions PlotActions::createCapturePlotActions(QWidget *parent)
{
	CapturePlotActions ret;
	/* capture button w/ menu */
	ret.head = createAction(parent, QIcon::fromTheme("camera-photo"), "Capture",
	                        "Save the plot to SVG or PNG file", false);
	ret.toClipboard = createAction(parent, QIcon::fromTheme("edit-copy"), "Copy to clipboard",
	                               "Copy the plot to clipboard", false, "Ctrl+Shift+C");
	ret.toFile = createAction(parent, QIcon::fromTheme("document-save"), "Save to file",
	                          "Save the plot to SVG or PNG file", false, "Ctrl+p");
	/* use print key as primary shortcut where available */
#if !defined(Q_OS_MAC)
	ret.toClipboard->setShortcuts({{"Print"}, {"Ctrl+Shift+C"}});
#endif

	auto snapshotMenu = new QMenu(parent);
	snapshotMenu->addAction(ret.toClipboard);
	snapshotMenu->addAction(ret.toFile);
	ret.head->setMenu(snapshotMenu);
	return ret;
}

void PlotActions::addCaptureButton(const PlotActions::CapturePlotActions &actions, QToolBar *target)
{
	/* add a button to plot capture actions in a consistent way throughout views */

	// right-align capture button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	target->addWidget(spacer);

	// the button itself
	auto button = new QToolButton(target);
	button->setDefaultAction(actions.head);
	button->setPopupMode(QToolButton::ToolButtonPopupMode::MenuButtonPopup);
	target->addWidget(button);
}

PlotActions::PlotActions(QObject *parent) : QObject(parent)
{
	toolbar = new QToolBar("Plot Display");
	toolbar->setFloatable(false);
}

void PlotActions::attachTo(QMainWindow *target)
{
	target->addToolBar(toolbar);
}

void PlotActions::attachTo(ProfileChart *newChart)
{
	// first disconnect old, if any
	detachFromChart();
	chart = newChart;
	if (!chart)
		return;

	// toggle what is shown
	if (actions.showLabels) {
		chart->toggleLabels(actions.showLabels->isChecked());
		connect(actions.showLabels, &QAction::toggled, chart, &ProfileChart::toggleLabels);
	}
	if (actions.showAverage) {
		chart->toggleAverage(actions.showAverage->isChecked());
		connect(actions.showAverage, &QAction::toggled, chart, &ProfileChart::toggleAverage);
	}
	if (actions.showQuantiles) {
		chart->toggleQuantiles(actions.showQuantiles->isChecked());
		connect(actions.showQuantiles, &QAction::toggled, chart, &ProfileChart::toggleQuantiles);
	}
	if (actions.showIndividual) {
		chart->toggleIndividual(actions.showIndividual->isChecked());
		connect(actions.showIndividual, &QAction::toggled, chart, &ProfileChart::toggleIndividual);
	}

	// toggle y-axis zoom; dynamics here are different: we handle the actions' signals ourselves
	updateZoom();

	// Note: log. scale is synchronized by owner.
}

void PlotActions::detachFromChart()
{
	if (!chart)
		return;

	for (auto i : {actions.showLabels, actions.showAverage,
	               actions.showQuantiles, actions.showIndividual}) {
		if (i)
			i->disconnect(chart);
	}
}

void PlotActions::setupActions(bool labels, bool average, bool quantiles, bool individual)
{
	// create action and also insert it into toolbar
	auto add_action = [&] (QIcon icon, QString title, QString tooltip,
	        bool isToggle, QString shortcut = {}) {
		auto action = createAction(this, icon, title, tooltip, isToggle, shortcut);
		toolbar->addAction(action);
		return action;
	};

	/* selection of what to display */
	if (labels) {
		actions.showLabels = add_action(QIcon{":/icons/show-labels.svg"}, "Labels",
		                                 "Show sample labels", true, "L");
		toolbar->addSeparator();
	}
	if (average)
		actions.showAverage = add_action(QIcon{":/icons/show-average.svg"}, "Average Profile",
		                                 "Show average profile", true, "A");
	if (quantiles)
		actions.showQuantiles = add_action(QIcon{":/icons/show-quantiles.svg"}, "Quantiles",
		                                 "Show per-dimension quantiles", true, "Q");
	if (individual)
		actions.showIndividual = add_action(QIcon{":/icons/show-individual.svg"}, "Individual Profiles",
		                                 "Show individual profiles", true, "I");

	// we assume that one of the three above are always enabled
	toolbar->addSeparator();

	/* Log-scale and zoom */
	{
		QIcon icon;
		icon.addFile(":/icons/logspace-off.svg", {}, QIcon::Normal, QIcon::Off);
		icon.addFile(":/icons/logspace-on.svg", {}, QIcon::Normal, QIcon::On);
		actions.logarithmic = add_action(icon, "Logarithmic", "Plot data on a log10 axis",
		                                  true, "Shift+L");
		connect(actions.logarithmic, &QAction::toggled, this, &PlotActions::toggleLogarithmic);
	}
	auto zoomGroup = new QActionGroup(this);
	zoomGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
	actions.zoomToGlobal = createAction(zoomGroup, QIcon{":/icons/auto-scale-global.svg"},
	                                    "Scale to dataset",
	                                    "Set zoom to fit data range of whole dataset",
	                                    true, "Shift+Z");
	actions.zoomToVisible = createAction(zoomGroup, QIcon{":/icons/auto-scale-individual.svg"},
	                                     "Scale to selection",
	                                     "Set zoom to fit data range of shown profiles",
	                                     true, "Z");
	toolbar->addActions(zoomGroup->actions());
	connect(zoomGroup, &QActionGroup::triggered, this, &PlotActions::updateZoom);

	/* capture button w/ menu */
	actions.capturePlot = createCapturePlotActions(toolbar);
	for (auto act : {actions.capturePlot.head, actions.capturePlot.toFile})
		connect(act, &QAction::triggered, this, [this] { emit capturePlot(true); });
	connect(actions.capturePlot.toClipboard, &QAction::triggered,
	        this, [this] { emit capturePlot(false); });
	addCaptureButton(actions.capturePlot, toolbar);
}

void PlotActions::addAction(QAction *action)
{
	toolbar->addAction(action);
}

void PlotActions::setLogarithmic(bool on) {
	actions.logarithmic->setChecked(on);
}

void PlotActions::setAverageIndividual(bool averageEnabled, bool averageOn, bool individualOn)
{
	if (!actions.showAverage || !actions.showIndividual)
		std::runtime_error("Attempt to manipulate average/individual which are not setup");
	actions.showAverage->setEnabled(averageEnabled);
	actions.showAverage->setChecked(averageOn);
	actions.showIndividual->setChecked(individualOn);
}

void PlotActions::updateZoom(QAction *)
{
	if (!chart)
		return;

	ProfileChart::YRange rangeMode = ProfileChart::YRange::KEEP;
	if (actions.zoomToGlobal->isChecked()) {
		rangeMode = ProfileChart::YRange::GLOBAL;
	} else if (actions.zoomToVisible->isChecked()) {
		rangeMode = ProfileChart::YRange::LOCAL;
	}
	chart->setYRange(rangeMode);
}
