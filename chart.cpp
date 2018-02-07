#include "chart.h"

#include <QtCharts/QAbstractAxis>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPolygonF>
#include <cmath>

#include <QtCore/QDebug>

Chart::Chart(QGraphicsItem *parent, Qt::WindowFlags wFlags):
    QChart(QChart::ChartTypeCartesian, parent, wFlags)
{
	// set up general appearance
	setAnimationOptions(QChart::NoAnimation);
	legend()->setAlignment(Qt::AlignLeft);
	setAxisX(new QtCharts::QValueAxis);
	setAxisY(new QtCharts::QValueAxis);
	axisX()->setTitleText("dim 1");
	axisY()->setTitleText("dim 2");

	// set up master series
	master = new QtCharts::QScatterSeries;
	this->addSeries(master);
	master->attachAxis(axisX());
	master->attachAxis(axisY());

	auto c = master->color();
	master->setBorderColor(c);
	c.setAlphaF(0.5);
	master->setColor(c);

	legend()->markers(master)[0]->setShape(QtCharts::QLegend::MarkerShapeCircle);

	// set up tracker ellipse used in track()
	tracker = new QGraphicsEllipseItem(this);
	tracker->setPen({Qt::red});
	tracker->setZValue(50);

	// keep static objects in sync
	connect(this, &QChart::plotAreaChanged, [this] {
		for (auto& i : items)
			i->updateGeometry();
	});
}

Chart::~Chart()
{
}

void Chart::display(QString name, const QVector<QPointF> &points)
{
	qDeleteAll(items);
	items.clear();

	//setTitle(entry.name);

	master->setName(name);
	master->replace(points);

	for (auto s : markers) {
		s->replace(0, master->pointsVector()[(*labelIndex)[s->name()]]);
	}

	// update ranges cheap & dirty
	auto bbox = QPolygonF(points).boundingRect();
	auto offset = bbox.width() * 0.05; // give some breathing space
	bbox.adjust(-offset, -offset, offset, offset);
	axisX()->setRange(bbox.left(), bbox.right());
	axisY()->setRange(bbox.top(), bbox.bottom());
}

void Chart::trackCursor(const QPointF &pos)
{
	if (pos.isNull()) { // disable tracker
		tracker->hide();
		emit cursorChanged({});
		return;
	}

	const qreal radius = 50;

	// find cursor in feature space (center + range)
	auto center = mapToValue(pos);
	auto diff = center - mapToValue(pos + QPointF{radius, 0});
	auto range = QPointF::dotProduct(diff, diff);

	// shape the corresponding ellipse in viewport space
	auto o = std::sqrt(range);
	QPointF offset(o, o);
	auto topLeft = mapToPosition(center - offset);
	auto botRight = mapToPosition(center + offset);
	tracker->setRect({topLeft, botRight});
	tracker->setTransformOriginPoint(center);

	// show tracker
	tracker->show();

	// determine all proteins that fall into the cursor
	auto p = master->pointsVector();
	QStringList list;
	for (int i = 0; i < p.size(); ++i) {
		auto diff = p[i] - center;
		if (QPointF::dotProduct(diff, diff) < range) {
			list << indexLabel->at(i);
		}
	}
	emit cursorChanged(list);
}

void Chart::addMarker(const QString &label)
{
	if (markers.contains(label))
		return; // already there

	// TODO: use custom class that does the styling itself

	auto s = new QtCharts::QScatterSeries;
	s->setName(label);
	s->setPointLabelsFormat(label);
	s->append(master->pointsVector()[(*labelIndex)[label]]);
	this->addSeries(s);
	markers[label] = s;

	s->attachAxis(axisX());
	s->attachAxis(axisY());

	s->setBorderColor(Qt::black);
	s->setColor(tableau20());
	s->setMarkerShape(QtCharts::QScatterSeries::MarkerShapeRectangle);
	s->setMarkerSize(20);
	s->setPointLabelsVisible(true);
	auto f = s->pointLabelsFont();
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.3);
	s->setPointLabelsFont(f);

	connect(legend()->markers(s)[0], &QtCharts::QLegendMarker::clicked, [this, label] {
		removeMarker(label);
	});
	emit markerAdded(label);
}

void Chart::removeMarker(const QString &label)
{
	removeSeries(markers[label]);
	markers.remove(label);
	emit markerRemoved(label);
}

QColor Chart::tableau20(bool reset)
{
	const std::vector<QColor> tableau = {
	    {31, 119, 180}, {174, 199, 232}, {255, 127, 14}, {255, 187, 120},
	    {44, 160, 44}, {152, 223, 138}, {214, 39, 40}, {255, 152, 150},
	    {148, 103, 189}, {197, 176, 213}, {140, 86, 75}, {196, 156, 148},
	    {227, 119, 194}, {247, 182, 210}, {127, 127, 127}, {199, 199, 199},
	    {188, 189, 34}, {219, 219, 141}, {23, 190, 207}, {158, 218, 229}};

	static unsigned index = 2;
	if (reset)
		index = 1;
	return tableau[index++ % tableau.size()];
}


Marker::Marker(Chart *c, const DataMark &source)
    : ForeignObject(c),
      pointer(new QGraphicsEllipseItem(c)),
      texter(new QGraphicsSimpleTextItem(c)),
      source(source) {

	pointer->setRect(0, 0, 11, 11);
	pointer->setPen({Qt::red});
	pointer->setZValue(99);

	texter->setText(source.label);
	auto f = texter->font();
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.3);
	texter->setFont(f);
	texter->setBrush(Qt::red);
	texter->setPen({Qt::white});
	texter->setZValue(100);

	updateGeometry();
	pointer->show();
	texter->show();
}

void Marker::updateGeometry() {
	auto p = source.pos;
	pointer->setPos(holder->mapToPosition(p) - QPointF{pointer->rect().width()/2., pointer->rect().height()/2.});
	texter->setPos(holder->mapToPosition(p));
}

Ellipse::Ellipse(Chart *c, const DataEllipse &source)
    : ForeignObject(c),
      elli(new QGraphicsEllipseItem(c)),
      source(source) {

	elli->setPen({Qt::blue});
	elli->pen().setWidth(10);
	elli->setZValue(50);

	updateGeometry();
	elli->show();
}

void Ellipse::updateGeometry() {
	QPointF center(holder->mapToPosition(source.center));

	QPointF offset(source.width/2., source.height/2.);
	QPointF topLeft(holder->mapToPosition(source.center - offset));
	QPointF botRight(holder->mapToPosition(source.center + offset));

	elli->setRect({topLeft, botRight});
	elli->setTransformOriginPoint(center);
	elli->setRotation(-source.rotation);
}
