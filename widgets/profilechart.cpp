#include "profilechart.h"
#include "profilewindow.h"

#include <QLineSeries>
#include <QAreaSeries>
#include <QValueAxis>
#include <QCategoryAxis>
#include <QBarCategoryAxis>

#include <opencv2/core/core.hpp>


ProfileChart::ProfileChart(Dataset &data)
    : data(data)
{
	/* small plot constructor */
	setMargins({0, 10, 0, 0});

	ax = new QtCharts::QBarCategoryAxis;
	ay = new QtCharts::QValueAxis;
	ay->setRange(0, 1);
	addAxis(ax, Qt::AlignBottom);
	addAxis(ay, Qt::AlignLeft);
	for (auto a : {ax, ay})
		a->hide();
	legend()->hide();
}

ProfileChart::ProfileChart(ProfileChart *source)
    : data(source->data)
{
	/* big, labelled plot constructor */
	auto ax = new QtCharts::QCategoryAxis;
	this->ax = ax; // keep QCategoryAxis* in this method
	ay = new QtCharts::QValueAxis;
	addAxis(ax, Qt::AlignBottom);
	addAxis(ay, Qt::AlignLeft);

	setTitle(source->title());
	legend()->setAlignment(Qt::AlignLeft);

	ax->setLabelsAngle(-90);
	ax->setLabelsPosition(QtCharts::QCategoryAxis::AxisLabelsPositionOnValue);
	auto labels = qobject_cast<QtCharts::QBarCategoryAxis*>(source->ax)->categories();
	ax->setRange(0, labels.size() - 1);
	ay->setRange(0, 1);

	auto toggleLabels = [ax, labels] (bool on) {
		/* QCategoryAxis does not adapt geometry when simply hiding labels. And
		 * it makes it really complicated for us to replace them :/ */
		if (on) {
			auto i = 0;
			for (auto &l : qAsConst(labels)) {
				ax->append(l, i++);
			}
		} else {
			auto categories = ax->categoriesLabels();
			for (auto &l : qAsConst(categories))
				ax->remove(l);
		}
	};
	toggleLabels(false);
	connect(this, &ProfileChart::toggleLabels, toggleLabels);

	/* copy over content
	 * Series are non-copyable, so we just recreate them */
	stats = source->stats;
	for (auto i : source->content)
		addSample(i.first, i.second);
	finalize(false);
}

void ProfileChart::setCategories(QStringList categories)
{
	auto a = qobject_cast<QtCharts::QBarCategoryAxis*>(ax);
	a->setCategories(categories);
}

void ProfileChart::clear()
{
	stats = {{}, {}};
	content.clear();
	removeAllSeries();
}

void ProfileChart::addSample(unsigned index, bool marker)
{
	content.push_back({index, marker});
}

void ProfileChart::finalize(bool fresh)
{
	auto d = data.peek();

	if (fresh) {
		computeStats();
		// small view, sort by name but marked last (for z-index)
		std::sort(content.begin(), content.end(), [&d] (auto a, auto b) {
			if (a.second != b.second)
				return b.second;
			return d->proteins[a.first].name < d->proteins[b.first].name;
		});
	} else {
		// big view, sort by name only
		std::sort(content.begin(), content.end(), [&d] (auto a, auto b) {
			return d->proteins[a.first].name < d->proteins[b.first].name;
		});
	}

	bool reduced = fresh && content.size() >= 25;
	bool outer = (!fresh || reduced) && !stats.mean.empty();

	// add & wire a series
	auto add = [this] (QtCharts::QAbstractSeries *s, bool isIndiv, bool isMarker = false) {
		addSeries(s);
		for (auto a : {ax, ay})
			s->attachAxis(a);
		auto signal = isIndiv ? &ProfileChart::toggleIndividual : &ProfileChart::toggleAverage;
		if (!isMarker) // marker always shows
			connect(this, signal, s, &QtCharts::QAbstractSeries::setVisible);
	};

	// setup and add QLineSeries for mean
	auto addMean = [&] {
		auto s = new QtCharts::QLineSeries;
		for (unsigned i = 0; i < stats.mean.size(); ++i) {
			s->append(i, stats.mean[i]);
		}
		add(s, false);
		s->setName("Avg.");
		auto pen = s->pen();
		pen.setColor(Qt::black);
		pen.setWidthF(pen.widthF()*1.5);
		s->setPen(pen);
	};

	// setup and add QAreaSeries for stddev
	auto addStddev = [&] {
		auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
		for (unsigned i = 0; i < stats.mean.size(); ++i) {
			upper->append(i, stats.mean[i] + stats.stddev[i]);
			lower->append(i, stats.mean[i] - stats.stddev[i]);
		}
		auto s = new QtCharts::QAreaSeries(upper, lower);
		add(s, false);
		s->setName("σ (SD)");
		s->setColor(Qt::gray);
		s->setBorderColor(Qt::gray);
	};

	// add individual series (in order, after area series)
	auto addIndividuals = [&] (bool onlyMarkers) {
		for (auto [index, isMarker] : content) {
			if (onlyMarkers && !isMarker)
				continue;

			auto s = new QtCharts::QLineSeries;
			add(s, true, isMarker);
			QString title = (isMarker ? "<small>★</small>" : "") + d->proteins[index].name;
			// color only markers in small view
			QColor color = (isMarker || !fresh ? d->proteins[index].color : Qt::black);
			if (isMarker && !fresh) { // acentuate markers in big view
				auto p = s->pen();
				p.setWidthF(3. * p.widthF());
				s->setPen(p);
			}
			s->setColor(color);
			s->setName(title);
			s->replace(d->featurePoints[index]);
		}
	};

	/* add everything in stacking order, based on conditions */
	if (outer)
		addStddev();
	if (!reduced)
		addIndividuals(false);
	if (outer)
		addMean();
	if (reduced)
		addIndividuals(true);
}

void ProfileChart::computeStats()
{
	if (content.size() < 2)
		return;

	auto d = data.peek();
	auto len = (size_t)d->dimensions.size();

	stats.mean.resize(len);
	stats.stddev.resize(len);
	/* compute mean+stddev per-dimension */
	for (size_t i = 0; i < len; ++i) {
		std::vector<double> f(content.size());
		for (size_t j = 0; j < content.size(); ++j)
			f[j] = d->features[(int)content[j].first][i];
		cv::Scalar m, s;
		cv::meanStdDev(f, m, s);
		stats.mean[i] = m[0];
		stats.stddev[i] = s[0];
	}
}
