#include "profilewidget.h"
#include "profilechart.h"
#include "profilewindow.h"
#include "dataset.h"
#include "windowstate.h"

#include <QMenu>
#include <QCursor>
#include <QStringList>
#include <QGuiApplication>
#include <QClipboard>
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

	/* setup actions */
	std::map<QToolButton*, QAction*> mapping = {
	    {profileViewButton, actionProfileView},
	    {avoidScrollingButton, actionAvoidScrolling},
	    {copyToClipboardButton, actionCopyToClipboard},
	    {addToMarkersButton, actionAddToMarkers},
	    {removeFromMarkersButton, actionRemoveFromMarkers},
	};
	for (auto &[btn, action] : mapping)
		btn->setDefaultAction(action);

	// by default reduce long protein sets
	actionAvoidScrolling->setChecked(true);

	connect(actionAddToMarkers, &QAction::triggered, [this] {
		state->proteins().toggleMarkers(proteins, true);
		updateDisplay();
	});
	connect(actionRemoveFromMarkers, &QAction::triggered, [this] {
		state->proteins().toggleMarkers(proteins, false);
		updateDisplay();
	});
	connect(actionCopyToClipboard, &QAction::triggered, [this] {
		auto p = state->proteins().peek();
		QStringList list;
		for (auto id : proteins)
			list.append(p->proteins[id].name);
		QGuiApplication::clipboard()->setText(list.join("\t"));
	});
	connect(actionProfileView, &QAction::triggered, [this] {
		if (chart)
			new ProfileWindow(state, chart, this->window());
	});
	connect(actionAvoidScrolling, &QAction::toggled, [this] {
		updateDisplay();
	});

	/* setup protein menu */
	connect(proteinList, &QTextBrowser::anchorClicked, [this] (const QUrl& link) {
		if (state && link.scheme() == "protein")
			state->proteinMenu(link.path().toUInt())->exec(QCursor::pos());
	});

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

	// avoid accidential misuse, which is also a performance concern
	actionAddToMarkers->setEnabled(proteins.size() <= 25);

	updateDisplay();
}

void ProfileWidget::updateDisplay()
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
	actionAvoidScrolling->setEnabled(total > showMax); // indicate if we are reducing or not

	// create format string and reduce set
	auto text = QString("%1");
	if (actionAvoidScrolling->isChecked() && total > showMax) {
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
	// 'protein' url scheme is internally handled (shows protein menu)
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
