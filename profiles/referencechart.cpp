#include "referencechart.h"
#include "dataset.h"
#include "compute/features.h"
#include "compute/colors.h"

#include <QLegendMarker>
#include <QLineSeries>
#include <QAreaSeries>
#include <QLogValueAxis>

/* small, inset plot constructor */
ReferenceChart::ReferenceChart(Dataset::ConstPtr dataset, const std::vector<Components> &comps)
    : ProfileChart(dataset, false, true), allComponents(comps)
{
	/* fooling around
	legend()->detachFromChart();
	legend()->setAlignment(Qt::AlignTop);
	connect(this, &ReferenceChart::plotAreaChanged, [this] () {
		legend()->setGeometry(plotArea());
	});
	auto m = margins();
	m.setLeft(150);
	setMargins(m);
	*/
}

void ReferenceChart::clear()
{
	components.clear();
	ProfileChart::clear();
}

void ReferenceChart::finalize()
{
	ProfileChart::finalize();

	// hide redundant reference marker
	legend()->markers(series.at(reference)).first()->setVisible(false);

	/* add component series */
	auto colors = Palette::tableau20; // TODO use windowState's standard colors
	auto ndim = size_t(series.at(reference)->pointsVector().size());

	auto createComponent = [&] (unsigned index, const Component &source) {
		auto &p = source.parameters;
		auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
		auto gauss = features::generate_gauss(ndim, p.mean, p.sigma, p.weight);
		auto minVal = (logSpace ? ayL->min() : 0.);
		for (size_t i = 0; i < ndim; ++i) {
			upper->append(i, gauss[i]);
			lower->append(i, minVal);
		}
		auto s = new QtCharts::QAreaSeries(upper, lower);
		s->setName(QString("Comp. %1").arg(index + 1));
		addSeries(s, SeriesCategory::CUSTOM);

		auto c = colors.at((int)index % colors.size());
		auto border = s->pen();
		border.setWidthF(border.widthF() * 0.5);
		s->setPen(border);
		s->setBorderColor(c);
		c.setAlphaF(.65);
		auto fill = s->brush();
		if (!source.active)
			fill.setStyle(Qt::BrushStyle::BDiagPattern);
		fill.setColor(c);
		s->setBrush(fill);

		auto toggleActive = [this,index] { toggleComponent(index); };
		connect(s, &QtCharts::QAreaSeries::clicked, toggleActive);
		connect(legend()->markers(s).first(), &QtCharts::QLegendMarker::clicked, toggleActive);
		return s;
	};

	for (size_t i = 0; i < components.size(); ++i) {
		auto &c = components[i];
		c.series = createComponent(i, c);
	}
}

void ReferenceChart::setReference(ProteinId ref)
{
	auto b = data->peek<Dataset::Base>();
	auto r = b->protIndex.find(ref);
	if (r == b->protIndex.end()) {
		clear(); // invalid reference for our dataset
		return;
	}

	if (reference == r->second)
		return;

	reference = r->second;
	repopulate();
}

void ReferenceChart::toggleComponent(unsigned index)
{
	if (index >= components.size())
		return;
	auto &c = components[index];
	c.active = !c.active;
	auto fill = c.series->brush();
	fill.setStyle(c.active ? Qt::BrushStyle::SolidPattern : Qt::BrushStyle::BDiagPattern);
	c.series->setBrush(fill);
}

void ReferenceChart::repopulate()
{
	clear(); // clears components

	for (auto &c : allComponents[reference])
		components.push_back({c});
	addSampleByIndex(reference, true); // claim "marker" state for bold drawing

	finalize(); // applies components
}

QString ReferenceChart::titleOf(unsigned int index, const QString &name, bool isMarker) const
{
	if (index == reference) // do not designate, we use "marker" state for bold drawing
		return QString("<b>%1</b>").arg(name);
	return ProfileChart::titleOf(index, name, isMarker);
}

QColor ReferenceChart::colorOf(unsigned int index, const QColor &color, bool isMarker) const
{
	if (index == reference)
		return Qt::black;
	return ProfileChart::colorOf(index, color, isMarker);
}
