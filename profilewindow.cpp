#include "profilewindow.h"
#include "profilechart.h"
#include "mainwindow.h"

ProfileWindow::ProfileWindow(ProfileChart *source, MainWindow *parent) :
    QMainWindow(parent), chart(new ProfileChart(source))
{
	setupUi(this);

	/* toolbar */
	// right-align screenshot button
	auto* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	toolBar->insertWidget(actionSavePlot, spacer);

	/* chart */
	chartView->setChart(chart);
	chartView->setRenderHint(QPainter::Antialiasing);

	/* actions */
	connect(actionSavePlot, &QAction::triggered, [this] {
		auto parent = parentWidget();
		auto title = parent->getTitle();
		auto desc = chart->title();
		if (desc.isEmpty())
			desc = "Selected Profiles";
		parent->getIo()->renderToFile(chartView, title, desc);
	});
	connect(actionShowLabels, &QAction::toggled, chart, &ProfileChart::toggleLabels);
	connect(actionShowIndividual, &QAction::toggled, chart, &ProfileChart::toggleIndividual);
	connect(actionShowAverage, &QAction::toggled, chart, &ProfileChart::toggleAverage);

	actionShowIndividual->setChecked(true); // cheap trick: let next one trigger
	actionShowIndividual->setChecked(chart->content.size() < 50);
	if (!chart->stats.mean.empty()) {
		actionShowAverage->setChecked(true);
	} else {
		actionShowAverage->setDisabled(true);
	}

	/* we are a single popup thingy: self-show and self-delete on close */
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_ShowWithoutActivating);
	show();
}

MainWindow *ProfileWindow::parentWidget()
{
	return qobject_cast<MainWindow*>(QMainWindow::parentWidget());
}
