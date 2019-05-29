#ifndef CHART_H
#define CHART_H

#include "dataset.h"
#include "utils.h"

#include <QChart>
#include <QScatterSeries>
#include <QStack>

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

	struct Proteins : QtCharts::QScatterSeries, NonCopyable // registers itself to the chart
	{
		Proteins(const QString &label, QColor color, Chart* chart);

		void clear() { QtCharts::QScatterSeries::clear(); samples.clear(); }
		void add(unsigned index, const QPointF &point);
		void apply();
		void redecorate(bool full = true, bool highlight = false);

		QVector<unsigned> samples;
		QVector<QPointF> replacement;
		bool highlighted = false;
	};

	struct Marker
	{
		Marker(unsigned sampleIndex, ProteinId id, Chart* chart);
		// remove+add graphicitems (used for z-ordering of chart elements)
		void reAdd();

		unsigned sampleIndex;
		ProteinId sampleId;

		// items are added to the scene, so we make them non-copyable
		// although scene also owns them, we delete first and they de-register
		std::unique_ptr<QtCharts::QScatterSeries> series;

	protected:
		void setup(Chart *chart);
	};

	Chart(Dataset::ConstPtr data);

	bool cursorLocked = false;

public slots:
	void setTitles(const QString &x, const QString &y);
	void display(const QVector<QPointF> &coords);
	void updatePartitions();
	void togglePartitions(bool showPartitions);
	void updateColorset(QVector<QColor> colors);
	void updateMarkers(bool newDisplay = false);
	void toggleMarker(ProteinId id, bool present);

	void zoomAt(const QPointF &pos, qreal factor);
	void undoZoom(bool full = false);

	void toggleSingleMode();
	void scaleProteins(qreal factor);
	void switchProteinBorders();
	void adjustProteinAlpha(qreal adjustment);

	void resetCursor();
	void updateCursor(const QPointF &pos = {});

signals:
	void cursorChanged(QVector<unsigned> samples, QString title = {});
	void markerToggled(ProteinId id, bool present);
	void areaChanged();
	void proteinStyleUpdated();

protected:
	void animate(int msec);

	/* state variables */
	struct {
		QRectF current;
		QStack<QRectF> history;
	} zoom;

	struct {
		bool singleMode = false; // mode to highlight single clusters
		qreal size = 15.;
		struct {
			qreal reg = .65;
			qreal hi = .9;
			qreal lo = .1;
		} alpha;
		Qt::PenStyle border = Qt::PenStyle::DotLine;
	} proteinStyle;

	/* items in the scene */
	Proteins *master; // owned by chart
	// note partitions are also owned by chart, but we delete first and they de-register
	std::unordered_map<int, std::unique_ptr<Proteins>> partitions;
	std::map<ProteinId, Marker> markers;

	QGraphicsEllipseItem *tracker;
	QtCharts::QValueAxis *ax, *ay;
	// deferred animation reset
	QTimer *animReset;

	QVector<QColor> colorset;
	// data source
	Dataset::ConstPtr data;
};

#endif /* CHART_H */
