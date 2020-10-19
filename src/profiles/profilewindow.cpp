#include "profilewindow.h"
#include "plotactions.h"
#include "profilechart.h"
#include "windowstate.h"
#include "dataset.h"
#include "fileio.h"

ProfileWindow::ProfileWindow(std::shared_ptr<WindowState> state, ProfileChart *source, QWidget *parent) :
    QMainWindow(parent),
    plotbar(new PlotActions(this)),
    chart(new ProfileChart(source))
{
	setupUi(this);
	plotbar->setupActions(true, true, false, true);
	auto nProfiles = chart->numProfiles();
	plotbar->setLogarithmic(chart->isLogSpace());
	plotbar->setAverageIndividual(nProfiles >= 2, nProfiles >= 10, nProfiles < 50);
	plotbar->attachTo(this);
	plotbar->attachTo(chart); // also applies avg./indv. settings to chart

	/* note: we do not use OpenGL as it has drawing bugs / does not support our
	 * customizations for score points */
	chartView->setChart(chart);
	chartView->setRenderHint(QPainter::Antialiasing);

	/* actions */
	connect(plotbar, &PlotActions::savePlot, [this,state] {
		auto title = chart->dataset()->config().name;
		auto desc = chart->title();
		if (desc.isEmpty())
			desc = "Selected Profiles";
		state->io().renderToFile(chartView, {title, desc});
	});
	connect(plotbar, &PlotActions::toggleLogarithmic, chart, &ProfileChart::toggleLogSpace);

	chart->finalize();

	/* we are a single popup thingy: self-show and self-delete on close */
	//setAttribute(Qt::WA_DeleteOnClose); CAUSES CRASH IN QT :-/ FIXME Who's gonna delete? (eventually parent will)
	//setAttribute(Qt::WA_ShowWithoutActivating); opens window in background (hidden)
	show();
}
