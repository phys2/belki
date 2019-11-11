#ifndef PROFILECHART_H
#define PROFILECHART_H

#include "model.h"

#include <QStringList>
#include <QChart>
#include <QTimer>
#include <QDeadlineTimer>

#include <memory>
#include <set>

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
	ProfileChart(std::shared_ptr<Dataset const> data, bool small=true, bool global=false);
	ProfileChart(ProfileChart *source);

	std::shared_ptr<Dataset const> dataset() { return data; }
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
	void toggleQuantiles(bool on);
	void menuRequested(ProteinId id);

protected:
	enum class SeriesCategory { // see showCategories
		INDIVIDUAL,
		AVERAGE,
		QUANTILE
	};

	void setupSeries();
	void animHighlight(int index, qreal step);
	void toggleHighlight(int index = -1);
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
		std::vector<qreal> quant25, quant50, quant75;
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
	std::set<SeriesCategory> showCategories = {SeriesCategory::INDIVIDUAL};
	bool logSpace = false;
	bool globalStats = false;
	QTimer highlightAnim;
	QDeadlineTimer highlightAnimDeadline;
};

#endif // PROFILECHART_H
