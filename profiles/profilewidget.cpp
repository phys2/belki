#include "profilewidget.h"
#include "profilechart.h"
#include "profilewindow.h"
#include "dataset.h"
#include "windowstate.h"

#include <QMenu>
#include <QCursor>
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

	// setup protein menu
	connect(proteinList, &QTextBrowser::anchorClicked, [this] (const QUrl& link) {
		if (state && link.scheme() == "protein")
			state->proteinMenu(link.path().toUInt())->exec(QCursor::pos());
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

void ProfileWidget::updateDisplay(std::vector<ProteinId> newProteins, const QString &title)
{
	proteins = newProteins;
	if (chart)
		chart->setTitle(title);

	update();
}

void ProfileWidget::update()
{
	/* clear plot */
	if (chart)
		chart->clear();

	if (proteins.empty() || !data || !chart) {
		proteinList->clear();
		setDisabled(true);
		return;
	}

	auto d = data->peek<Dataset::Base>();
	auto p = data->peek<Dataset::Proteins>();
	/* sender dataset & ours might be out-of-sync. play it save and compose samples */
	std::vector<std::pair<ProteinId, unsigned>> samples;
	for (auto i : proteins) {
		try {
			samples.push_back({i, d->protIndex.at(i)});
		} catch (std::out_of_range&) {} // not in index, just leave it
	}
	unsigned total = samples.size();
	bool reduced = total >= 25;

	/* set up plot */
	for (auto [id, index] : samples)
		chart->addSampleByIndex(index, p->markers.count(id));
	chart->toggleAverage(reduced);
	chart->toggleIndividual(!reduced);
	chart->finalize();

	/* set up list */

	// determine how many lines we can fit
	auto testFont = proteinList->currentFont(); // replicate link font
	testFont.setBold(true);
	testFont.setUnderline(true);
	auto showMax = size_t(proteinList->contentsRect().height() /
	        QFontMetrics(testFont).lineSpacing() - 1);

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
	std::sort(samples.begin(), samples.end(), [&p] (auto a, auto b) {
		return p->proteins[a.first].name < p->proteins[b.first].name;
	});

	// obtain annotations, if available
	auto s = data->peek<Dataset::Structure>(); // keep while we operate with Annot*!
	auto annotations = s->fetch(state->annotations);

	// compose list
	QString content;
	// TODO: custom URL with URL handler that leads to protein menu
	QString tpl("<b><a href='protein:%1'>%2</a></b> <small>%3 <i>%4</i></small><br>");
	for (auto [id, index] : samples) {
		 // highlight marker proteins
		if (p->markers.count(id))
			content.append("<small>★</small>");
		auto &prot = p->proteins[id];
		QStringList clusters;
		if (annotations) {
			auto &m = annotations->memberships[index]; // we checked protIndex before
			clusters = std::accumulate(m.begin(), m.end(), QStringList(),
			                           [&annotations] (QStringList a, unsigned b) {
			                            return a << annotations->groups.at(b).name; });
		}
		content.append(tpl.arg(id).arg(prot.name, clusters.join(", "), prot.description));
	}
	proteinList->setText(text.arg(content));

	setEnabled(true);
}
