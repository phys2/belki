#include "profilewidget.h"
#include "profilechart.h"
#include "profilewindow.h"
#include "dataset.h"
#include "windowstate.h"

#include <random>

ProfileWidget::ProfileWidget(QWidget *parent) :
    QWidget(parent)
{
	setupUi(this);

	plot->setRenderHint(QPainter::Antialiasing);
	// common background for plot and its container
	auto p = inlet->palette();
	p.setColor(QPalette::Window, p.color(QPalette::Base));
	inlet->setPalette(p);

	// setup full view action
	profileViewButton->setDefaultAction(actionProfileView);
	connect(actionProfileView, &QAction::triggered, [this] {
		if (chart)
			new ProfileWindow(chart, this->window());
	});

	// move button into chart (evil :D)
	profileViewButton->setParent(plot);
	profileViewButton->move(4, 4);
	topBar->deleteLater();

	setDisabled(true);
}

void ProfileWidget::setData(std::shared_ptr<Dataset> dataset)
{
	if (dataset == data)
		return;

	if (data)
		data->disconnect(this);

	data = dataset;
	proteinList->clear();
	plot->setVisible(false);

	if (data) {
		// TODO: rework the ownership/lifetime stuff (or wait for our own chartview class)
		auto old = chart;
		chart = new ProfileChart(data);

		if (data->peek<Dataset::Base>()->logSpace)
			chart->toggleLogSpace(true);

		plot->setChart(chart);
		delete old;
		plot->setVisible(true);
	}
}

void ProfileWidget::updateDisplay(QVector<unsigned> newSamples, const QString &title)
{
	samples = newSamples;
	if (chart)
		chart->setTitle(title);

	update();
}

void ProfileWidget::update()
{
	/* clear plot */
	if (chart)
		chart->clear();

	if (samples.empty() || !data || !chart) {
		proteinList->clear();
		setDisabled(true);
		return;
	}

	/* determine marker proteins contained in samples */
	auto d = data->peek<Dataset::Base>();
	auto p = data->peek<Dataset::Proteins>();
	std::set<unsigned> markers;
	for (auto i : qAsConst(samples)) {
		if (p->markers.count(d->protIds[i]))
			markers.insert(i);
	}

	/* set up plot */
	bool reduced = samples.size() >= 25;
	chart->toggleAverage(reduced);
	chart->toggleIndividual(!reduced);
	for (auto i : qAsConst(samples))
		chart->addSampleByIndex(i, markers.count(i));
	chart->finalize();

	/* set up list */

	// determine how many lines we can fit
	auto total = samples.size();
	auto testFont = proteinList->currentFont(); // replicate link font
	testFont.setBold(true);
	testFont.setUnderline(true);
	auto showMax = proteinList->contentsRect().height() /
	        QFontMetrics(testFont).lineSpacing() - 1;

	// create format string and reduce set
	auto text = QString("%1");
	if (total > showMax) {
		text.append("… ");
		// shuffle before cutting off
		std::shuffle(samples.begin(), samples.end(), std::mt19937(0));
		samples.resize(showMax);
	}
	text.append(QString("(%1 total)").arg(total));

	// sort by name -- _after_ set reduction to get a broad representation
	std::sort(samples.begin(), samples.end(), [&d,&p] (unsigned a, unsigned b) {
		return d->lookup(p, a).name < d->lookup(p, b).name;
	});

	// obtain annotations, if available
	auto s = data->peek<Dataset::Structure>(); // keep while we operate with Annot*!
	auto annotations = s->fetch(state->annotations);

	// compose list
	QString content;
	// TODO: custom URL with URL handler that leads to protein menu
	QString tpl("<b><a href='https://uniprot.org/uniprot/%1_%2'>%1</a></b> <small>%3 <i>%4</i></small><br>");
	for (auto i : qAsConst(samples)) {
		 // highlight marker proteins
		if (markers.count(i))
			content.append("<small>★</small>");
		auto &prot = d->lookup(p, i);
		QStringList clusters;
		if (annotations) {
			auto &m = annotations->memberships[i];
			clusters = std::accumulate(m.begin(), m.end(), QStringList(),
			                           [&annotations] (QStringList a, unsigned b) {
			                            return a << annotations->groups.at(b).name; });
		}
		content.append(tpl.arg(prot.name, prot.species, clusters.join(", "), prot.description));
	}
	proteinList->setText(text.arg(content));

	setEnabled(true);
}
