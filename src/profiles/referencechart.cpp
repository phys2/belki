#include "referencechart.h"
#include "dataset.h"
#include "../compute/components.h"
#include "../compute/colors.h"

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

	auto createComponent = [&] (size_t index) {
		auto &p = allComponents[reference][index];
		auto upper = new QtCharts::QLineSeries, lower = new QtCharts::QLineSeries;
		auto gauss = components::generate_gauss(ndim, p.mean, p.sigma, p.weight);
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
		if (!components[index].active)
			fill.setStyle(Qt::BrushStyle::BDiagPattern);
		fill.setColor(c);
		s->setBrush(fill);

		auto toggleActive = [this,index] { toggleComponent(index, true); };
		connect(s, &QtCharts::QAreaSeries::clicked, toggleActive);
		connect(legend()->markers(s).first(), &QtCharts::QLegendMarker::clicked, toggleActive);
		return s;
	};

	for (size_t i = 0; i < components.size(); ++i)
		components[i].series = createComponent(i);
	/* TODO: debug code */
	if (!components.empty()) {
		std::vector<double> sum(ndim, 0.);
		double sumWeights = 0.;
		for (size_t i = 0; i < components.size(); ++i) {
			auto &p = allComponents[reference][i];
			components::add_gauss(sum, p.mean, p.sigma, p.weight);
			sumWeights += p.weight;
		}
		auto s = new QtCharts::QLineSeries;
		s->setName(QString::number(sumWeights));
		for (size_t i = 0; i < ndim; ++i)
			s->append(i, sum[i]);
		addSeries(s, SeriesCategory::CUSTOM);
		s->setPen({Qt::red});
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

void ReferenceChart::applyBorder(Qt::Edge border, double value)
{
	auto &target = (border == Qt::Edge::LeftEdge ? range.first : range.second);
	if (target == value)
		return;

	target = value;

	/* use range to re-set active components, but only once.
	 * the user might use the range to quickly filter components, but then
	 * refine by hand */
	bool changed = false;
	for (size_t i = 0; i < components.size(); ++i) {
		auto &p = allComponents[reference][i];
		auto active = components[i].active;
		bool inside = (p.mean >= range.first && p.mean <= range.second);
		// only change inside or outside our edge; don't touch outside opposite
		bool change = inside && !active;
		if (!inside && border == Qt::Edge::LeftEdge)
			change = (p.mean < range.first) && active;
		if (!inside && border == Qt::Edge::RightEdge)
			change = (p.mean > range.second) && active;
		if (change) {
			toggleComponent(i);
			changed = true;
		}
	}
	if (changed)
		emitSelection();
}

void ReferenceChart::toggleComponent(size_t index, bool signal)
{
	if (index >= components.size())
		return;

	auto &c = components[index];
	c.active = !c.active;
	auto fill = c.series->brush();
	fill.setStyle(c.active ? Qt::BrushStyle::SolidPattern : Qt::BrushStyle::BDiagPattern);
	c.series->setBrush(fill);
	if (signal)
		emitSelection();
}

void ReferenceChart::repopulate()
{
	clear(); // clears components

	for (size_t i = 0; i < allComponents[reference].size(); ++i) {
		auto &p = allComponents[reference][i];
		bool active = p.mean >= range.first && p.mean <= range.second;
		components.push_back({active});
	}
	addSampleByIndex(reference, true); // claim "marker" state for bold drawing

	finalize(); // applies components
}

QString ReferenceChart::titleOf(unsigned int index, const QString &name, bool isMarker) const
{
	if (index == reference)
		return QString("<b>%1</b>").arg(name); // do not further designate
	return ProfileChart::titleOf(index, name, isMarker);
}

QColor ReferenceChart::colorOf(unsigned int index, const QColor &color, bool isMarker) const
{
	if (index == reference)
		return Qt::black;
	return ProfileChart::colorOf(index, color, isMarker);
}

void ReferenceChart::emitSelection()
{
	std::vector<size_t> selection;
	for (size_t i = 0; i < components.size(); ++i) {
		if (components[i].active)
			selection.push_back(i);
	}
	emit componentsSelected(selection);
}
