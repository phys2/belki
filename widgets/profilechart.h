#ifndef PROFILECHART_H
#define PROFILECHART_H

#include <QStringList>
#include <QChart>

class ProfileWindow;
class CentralHub;
class ProteinDB;
class Dataset;

namespace QtCharts {
class QAreaSeries;
class QLineSeries;
class QAbstractAxis;
}

class ProfileChart : public QtCharts::QChart
{
	Q_OBJECT

public:
	ProfileChart(CentralHub &hub);
	ProfileChart(ProfileChart *source);

	unsigned numProfiles() { return content.size(); }
	bool haveStats() { return !stats.mean.empty(); }

	void setCategories(QStringList categories);

	void clear(); // need to be called before addSample calls
	void addSample(unsigned index, bool marker = false);
	void finalize(bool fresh = true); // need to be called after addSample calls

signals:
	void toggleLabels(bool on);
	void toggleIndividual(bool on);
	void toggleAverage(bool on);

protected:
	void computeStats(); // helper to finalize()

	/* indices of proteins shown in the graph, as markers or not */
	std::vector<std::pair<unsigned, bool>> content;
	/* statistics representing the data */
	struct {
		std::vector<qreal> mean;
		std::vector<qreal> stddev;
		std::vector<qreal> min, max;
	} stats;

	// axes
	QtCharts::QAbstractAxis *ax, *ay;

	// protein database
	ProteinDB &proteins;

	// data source
	Dataset &data;
};

#endif // PROFILECHART_H
