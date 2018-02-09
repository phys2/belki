#ifndef CHART_H
#define CHART_H

#include "dataset.h"

#include <QtCharts/QChart>

QT_CHARTS_BEGIN_NAMESPACE
class QScatterSeries;
QT_CHARTS_END_NAMESPACE

class Chart: public QtCharts::QChart
{
    Q_OBJECT

public:
	Chart(QGraphicsItem *parent = nullptr, Qt::WindowFlags wFlags = nullptr);
    virtual ~Chart();

	void setMeta(QVector<Dataset::Protein> *p, QMap<QString, int> *i)
	{ proteins = p; protIndex = i; }

	void display(const QVector<QPointF> &points, bool reset = false);
	void trackCursor(const QPointF &pos);
	void addMarker(const QString &label);
	void removeMarker(const QString &label);

	static QColor tableau20(bool reset = false);

signals:
	void cursorChanged(QStringList labels);
	void markerAdded(const QString &label);
	void markerRemoved(const QString &label);

protected:
	QtCharts::QScatterSeries *master;
	std::vector<QtCharts::QScatterSeries*> partitions;
	QMap<QString, QtCharts::QScatterSeries*> markers;

	QGraphicsEllipseItem *tracker;

	QVector<Dataset::Protein> *proteins = nullptr;
	QMap<QString, int> *protIndex = nullptr;
};

#endif /* CHART_H */
