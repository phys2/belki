#ifndef PROFILECHART_H
#define PROFILECHART_H

#include "model.h"

#include <QStringList>
#include <QChart>

#include <memory>

class ProfileWindow;
class ProteinDB;
class Dataset;

namespace QtCharts {
class QAreaSeries;
class QLineSeries;
class QCategoryAxis;
class QValueAxis;
}

class ProfileChart : public QtCharts::QChart
{
	Q_OBJECT

public:
	ProfileChart(std::shared_ptr<Dataset const> data);
	ProfileChart(ProfileChart *source);

	unsigned numProfiles() { return content.size(); }
	bool haveStats() { return !stats.mean.empty(); }

	void clear(); // need to be called before addSample calls
	void addSample(unsigned index, bool marker = false);
	void finalize(bool fresh = true); // need to be called after addSample calls
	void toggleLabels(bool on);

signals:
	void toggleIndividual(bool on);
	void toggleAverage(bool on);

protected:
	// helper to constructors
	void setupAxes(const Features::Range &range, bool small);
	// helper to finalize()
	void computeStats();

	/* indices of proteins shown in the graph, as markers or not */
	std::vector<std::pair<unsigned, bool>> content;
	/* statistics representing the data */
	struct {
		std::vector<qreal> mean;
		std::vector<qreal> stddev;
		std::vector<qreal> min, max;
	} stats;

	// axes
	QtCharts::QCategoryAxis *ax;
	QtCharts::QCategoryAxis *axC;
	QtCharts::QValueAxis *ay;

	// data source
	std::shared_ptr<Dataset const> data;
	QStringList labels; // cached, so we don't need to bother dataset
};

#endif // PROFILECHART_H
