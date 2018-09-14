#ifndef CHART_H
#define CHART_H

#include "dataset.h"

#include <QtCharts/QChart>
#include <QtCharts/QScatterSeries>
#include <QtCore/QStack>

#include <unordered_map>

class QColor;
class QTimer;

namespace QtCharts {
class QValueAxis;
}

class Chart: public QtCharts::QChart
{
    Q_OBJECT

public:

	struct Proteins : QtCharts::QScatterSeries {
		Proteins(const QString &label, QColor color, Chart* chart);
		// no copies/moves! registers itself to the chart in above constructor
		Proteins(const Proteins&) = delete;
		Proteins& operator=(const Proteins&) = delete;
		void add(unsigned index, const QPointF &point);
		void apply();

		QVector<unsigned> samples;
		QVector<QPointF> replacement;
	};
	struct Marker : QtCharts::QScatterSeries {
		Marker(unsigned sampleIndex, Chart* chart);
		// no copies/moves! registers itself to the chart in above constructor
		Marker(const Marker&) = delete;
		Marker& operator=(const Marker&) = delete;
		unsigned sampleIndex;
	};

	Chart(Dataset &data);
    virtual ~Chart();

	void clear();
	void clearPartitions();
	void display(const QString& set);
	void updatePartitions();
	void addMarker(unsigned sampleIndex);
	void removeMarker(unsigned sampleIndex);
	void clearMarkers();

	void zoomAt(const QPointF &pos, qreal factor);
	void undoZoom();

	void scaleProteins(qreal factor);
	void switchProteinBorders();
	void adjustProteinAlpha(qreal adjustment);

	static QColor tableau20(unsigned index);

	bool cursorLocked = false;

public slots:
	void resetCursor();
	void updateCursor(const QPointF &pos = {});
	void togglePartitions(bool showPartitions);

signals:
	void areaChanged();
	void cursorChanged(QVector<unsigned> samples, QString title = {});
	void markerToggled(unsigned sampleIndex, bool present);
	void markersCleared();
	void proteinStyleUpdated();

protected:
	void animate(int msec);

	Dataset &data;

	Proteins *master;
	std::unordered_map<int, Proteins*> partitions;
	QMap<unsigned, Marker*> markers;

	QGraphicsEllipseItem *tracker;
	QtCharts::QValueAxis *ax, *ay;

	struct {
		QRectF current;
		QStack<QRectF> history;
	} zoom;

	struct {
		qreal size = 15.;
		qreal alpha = .65;
		Qt::PenStyle border = Qt::PenStyle::DotLine;
	} proteinStyle;

	QTimer *animReset;
};

#endif /* CHART_H */
