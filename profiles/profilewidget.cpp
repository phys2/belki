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
	/* have QTextBrowser use same background as whole widget, avoid graying out when disabled */
	auto p = inlet->palette();
	auto color = p.color(QPalette::Base);
	p.setColor(QPalette::Window, color);
	p.setColor(QPalette::Base, color);
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
	connect(actionAvoidScrolling, &QAction::toggled, [this] { updateDisplay(); });

	setDisabled(true);
}

void ProfileWidget::init(std::shared_ptr<WindowState> s)
{
	if (state)
		throw std::runtime_error("ProfileWidget::init() called twice");
	state = s;

	/* enable actions that need state */
	connect(actionAddToMarkers, &QAction::triggered, [this] {
		state->proteins().toggleMarkers(proteins, true);
	});
	connect(actionRemoveFromMarkers, &QAction::triggered, [this] {
		state->proteins().toggleMarkers(proteins, false);
	});
	connect(actionCopyToClipboard, &QAction::triggered, [this] {
		auto p = state->proteins().peek();
		QStringList list;
		for (auto id : proteins)
			list.append(p->proteins.at(id).name);
		QGuiApplication::clipboard()->setText(list.join("\r\n"));
	});
	connect(actionProfileView, &QAction::triggered, [this] {
		if (chart)
			new ProfileWindow(state, chart, this->window());
	});

	/* setup protein menu */
	connect(proteinList, &QTextBrowser::anchorClicked, [this] (const QUrl& link) {
		if (link.scheme() == "protein")
			state->proteinMenu(link.path().toUInt())->exec(QCursor::pos());
	});

	/* setup updates on protein changes */
	connect(&state->proteins(), &ProteinDB::markersToggled, this, &ProfileWidget::updateMarkers);
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

void ProfileWidget::updateMarkers(const std::vector<ProteinId> &ids, bool)
{
	if (proteins.empty())
		return;

	if (proteins.size() == 1) { // easy, but not seldom case: we currently show only one protein
		for (auto id : ids) {
			if (id == proteins.front()) {
				updateDisplay();
				break;
			}
		}
	} else {
		std::set<ProteinId> affected(ids.begin(), ids.end()); // convert the vector that is typically small
		for (auto id : proteins) {
			if (affected.find(id) != affected.end()) {
				updateDisplay();
				break;
			}
		}
	}
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
	auto annotations = (state ? s->fetch(state->annotations) : nullptr);

	// compose list
	QString content;
	// 'protein' url scheme is internally handled (shows protein menu)
	QString tpl("<span %5><a style='color: %6;' href='protein:%1'>%2</a></span>"
	            " <small>%3 <i>%4</i></small><br>");
	auto textColor = inlet->palette().color(QPalette::ColorRole::Text).name();
	auto markupCluster = [annotations] (QStringList a, unsigned b) {
		auto &g = annotations->groups.at(b);
		auto bgColor = g.color;
		bgColor.setAlphaF(.33);
		QString markup{"<span style='background-color:%2;'>&nbsp;%1&nbsp;</span>"};
		return std::move(a) << markup.arg(g.name).arg(bgColor.name(QColor::NameFormat::HexArgb));
	};
	for (auto [id, index] : samples) {
		auto &prot = p->proteins[id];
		QStringList clusters;
		if (annotations) {
			auto &m = annotations->memberships[index]; // we checked protIndex before
			clusters = std::accumulate(m.begin(), m.end(), QStringList(), markupCluster);
		}
		QString styleAttr;
		if (p->markers.count(id)) {	// highlight marker proteins
			auto bgColor = prot.color;
			bgColor.setAlphaF(.33);
			styleAttr = QString{"style='font-weight:bold;background-color:%1;'"}
			            .arg(bgColor.name(QColor::NameFormat::HexArgb));
		}
		content.append(tpl.arg(id).arg(prot.name, clusters.join(""), prot.description)
		               .arg(styleAttr).arg(textColor));
	}
	proteinList->setText(text.arg(content));

	setEnabled(true);
}
