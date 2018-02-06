#ifndef CHART_H
#define CHART_H

#include <QtCharts/QChart>

QT_CHARTS_BEGIN_NAMESPACE
class QScatterSeries;
QT_CHARTS_END_NAMESPACE

struct DataMark {
	QString label;
	QPointF pos;
};

struct DataEllipse {
	QPointF center;
	qreal width, height;
	qreal rotation;
};

class Chart;

class ForeignObject
{
public:
	ForeignObject(Chart *c) : holder(c) {}
	virtual ~ForeignObject() {}

	virtual void updateGeometry() = 0;

protected:
	Chart *holder;
};

class Marker: public ForeignObject
{
public:
	Marker(Chart *c, const DataMark &source);
	~Marker() { delete pointer; delete texter; }

	void setChart(Chart *c);
	void updateGeometry();

protected:
	QGraphicsEllipseItem *pointer;
	QGraphicsSimpleTextItem *texter;
	const DataMark &source;
};

class Ellipse: public ForeignObject
{
public:
	Ellipse(Chart *c, const DataEllipse &source);
	~Ellipse() { delete elli; }

	void setChart(Chart *c);
	void updateGeometry();

protected:
	QGraphicsEllipseItem *elli;
	const DataEllipse &source;
};

class Chart: public QtCharts::QChart
{
    Q_OBJECT

public:
	Chart(QGraphicsItem *parent = nullptr, Qt::WindowFlags wFlags = nullptr);
    virtual ~Chart();

	void setLabels(QMap<QString, int> *li, QVector<QString> *il)
	{ labelIndex = li; indexLabel = il; }

	void display(QString name, const QVector<QPointF> &points);
	void trackCursor(const QPointF &pos);
	void addMarker(QString label);

	static QColor tableau20(bool reset = false);

signals:
	void cursorChanged(QStringList labels);

protected:
	QtCharts::QScatterSeries *master;
	std::vector<QtCharts::QScatterSeries*> partitions;
	QMap<QString, QtCharts::QScatterSeries*> markers;

	std::vector<ForeignObject*> items;
	QGraphicsEllipseItem *tracker;

	QMap<QString, int> *labelIndex = nullptr;
	QVector<QString> *indexLabel = nullptr;
};

#endif /* CHART_H */
