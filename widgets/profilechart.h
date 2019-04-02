#ifndef PROFILECHART_H
#define PROFILECHART_H

#include <QStringList>
#include <QChart>

class ProfileWindow;

namespace QtCharts {
class QAreaSeries;
class QLineSeries;
class QAbstractAxis;
}

class ProfileChart : public QtCharts::QChart
{
	Q_OBJECT

public:
	ProfileChart();
	ProfileChart(ProfileChart *source);

	void setCategories(QStringList categories);

	void clear(); // need to be called before addSample calls
	void addSample(QString name, QVector<QPointF> points);
	void finalize(bool fresh = true); // need to be called after addSample calls

	/* statistics representing the data */
	struct {
		std::vector<qreal> mean;
		std::vector<qreal> stddev;
	} stats;

	/* only the individual series in the graph */
	std::vector<QtCharts::QLineSeries*> content;

signals:
	void toggleLabels(bool on);
	void toggleIndividual(bool on);
	void toggleAverage(bool on);

protected:
	void computeStats(); // helper to finalize()

	QtCharts::QAbstractAxis *ax, *ay;
};

#endif // PROFILECHART_H
