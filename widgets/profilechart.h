#ifndef PROFILECHART_H
#define PROFILECHART_H

#include "dataset.h"

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
	ProfileChart(Dataset &data);
	ProfileChart(ProfileChart *source);

	void setCategories(QStringList categories);

	void clear(); // need to be called before addSample calls
	void addSample(unsigned index, bool marker = false);
	void finalize(bool fresh = true); // need to be called after addSample calls

	/* statistics representing the data */
	struct {
		std::vector<qreal> mean;
		std::vector<qreal> stddev;
	} stats;

	/* indices of proteins shown in the graph, as markers or not */
	std::vector<std::pair<unsigned, bool>> content;

signals:
	void toggleLabels(bool on);
	void toggleIndividual(bool on);
	void toggleAverage(bool on);

protected:
	void computeStats(); // helper to finalize()

	// data source
	Dataset &data;

	QtCharts::QAbstractAxis *ax, *ay;
};

#endif // PROFILECHART_H
