#include "plotactions.h"
#include "profilechart.h"
#include <QActionGroup>
#include <QToolBar>
#include <QMainWindow>

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
	// TODO log.scale
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
	// create action, wire it to signal and also insert it into toolbar
	auto add_action = [this] (QIcon icon, QString title, QString tooltip,
	        bool isToggle, QString shortcut = {}) {
		auto action = new QAction(icon, title, this);
	    action->setCheckable(isToggle);
		action->setIcon(icon);
		action->setToolTip(tooltip);
		if (!shortcut.isEmpty())
			action->setShortcut({shortcut});
		toolbar->addAction(action);
		return action;
	};

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

	auto zoomGroup = new QActionGroup(this);
	zoomGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
	// TODO
	/*actions.zoomToGlobal = new QAction("global", zoomGroup);
	/actions.zoomToVisible = new QAction("visible", zoomGroup);
	for (auto i : {actions.zoomToGlobal, actions.zoomToVisible}) {
		i->setCheckable(true);
	}*/

	{
		QIcon icon;
		icon.addFile(":/icons/logspace-off.svg", {}, QIcon::Normal, QIcon::Off);
		icon.addFile(":/icons/logspace-on.svg", {}, QIcon::Normal, QIcon::On);
		actions.logarithmic = add_action(icon, "Logarithmic", "Plot data on a log10 axis",
		                                  true, "Shift+L");
		connect(actions.logarithmic, &QAction::toggled, this, &PlotActions::toggleLogarithmic);
	}

	actions.savePlot = add_action(QIcon::fromTheme("camera-photo"), "Capture",
	                               "Save the plot to SVG or PNG file", false, "Print");
	connect(actions.savePlot, &QAction::triggered, this, &PlotActions::savePlot);

	toolbar->addActions(zoomGroup->actions());

	// right-align screenshot button to be consistent with non-profile tabs' ui
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolbar->insertWidget(actions.savePlot, spacer);
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
