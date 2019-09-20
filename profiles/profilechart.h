#ifndef PROFILECHART_H
#define PROFILECHART_H

#include "model.h"

#include <QStringList>
#include <QChart>
#include <QTimer>

#include <memory>

class ProfileWindow;
class ProteinDB;
class Dataset;

namespace QtCharts {
class QAreaSeries;
class QLineSeries;
class QCategoryAxis;
class QValueAxis;
class QLogValueAxis;
}

class ProfileChart : public QtCharts::QChart
{
	Q_OBJECT

public:
	ProfileChart(std::shared_ptr<Dataset const> data);
	ProfileChart(ProfileChart *source);

	unsigned numProfiles() { return content.size(); }
	bool isLogSpace() { return logSpace; }

	void clear(); // need to be called before addSample calls
	void addSample(ProteinId id, bool marker = false);
	void addSampleByIndex(unsigned index, bool marker = false);
	void finalize(); // need to be called after addSample calls
	void toggleLabels(bool on);
	void toggleLogSpace(bool on);

signals:
	void toggleIndividual(bool on);
	void toggleAverage(bool on);

protected:
	void setupSeries();
	void toggleHighlight(unsigned index);
	// helpers to constructors
	void setupSignals();
	void setupAxes(const Features::Range &range);
	// helper to finalize()
	void computeStats();

	/* indices of proteins shown in the graph, as markers or not */
	std::vector<std::pair<unsigned, bool>> content;
	std::unordered_map<unsigned, QtCharts::QLineSeries*> series;
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
	QtCharts::QLogValueAxis *ayL;

	// data source
	std::shared_ptr<Dataset const> data;
	QStringList labels; // cached, so we don't need to bother dataset

	// state
	bool small = false;
	bool showAverage = false;
	bool showIndividual = true;
	bool logSpace = false;
	QTimer highlightAnim;
};

#endif // PROFILECHART_H
