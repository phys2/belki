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
	virtual ~ProfileChart() {}

	std::shared_ptr<Dataset const> dataset() { return data; }
	unsigned numProfiles() { return content.size(); }
	bool isLogSpace() { return logSpace; }

	virtual void clear(); // need to be called before addSample calls
	void addSample(ProteinId id, bool marker = false);
	void addSampleByIndex(unsigned index, bool marker = false);
	virtual void finalize(); // need to be called after addSample calls
	void toggleLabels(bool on);
	void toggleLogSpace(bool on);

signals:
	// signals that pass through calls/signals from outside
	void toggleIndividual(bool on);
	void toggleAverage(bool on);
	void toggleQuantiles(bool on);
	// a regular signal to connect to
	void menuRequested(ProteinId id);

protected:
	enum class Sorting {
		NONE,
		NAME,
		MARKEDTHENNAME
	} sort = Sorting::NAME;

	enum class SeriesCategory { // see showCategories
		INDIVIDUAL,
		AVERAGE,
		QUANTILE,
		CUSTOM
	};

	void setupSeries();
	virtual QString titleOf(unsigned index, const QString &name, bool isMarker) const;
	virtual QColor colorOf(unsigned index, const QColor &color, bool isMarker) const;
	virtual void animHighlight(int index, qreal step);
	void toggleHighlight(int index = -1);
	// helpers to constructors
	void setupSignals();
	void setupAxes(const Features::Range &range);
	// helper to finalize()
	void computeStats(bool global);
	// helper to setupSeries()
	void addSeries(QtCharts::QAbstractSeries *s, SeriesCategory cat, bool sticky = false);

	/* indices of proteins shown in the graph, as markers or not */
	std::vector<std::pair<unsigned, bool>> content;
	std::unordered_map<unsigned, QtCharts::QLineSeries*> series;
	// stats on shown data and/or whole dataset
	Features::Stats statsLocal, statsGlobal;

	// axes
	QtCharts::QValueAxis *ax;
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
	bool useGlobalStats = false;
	QTimer highlightAnim;
	QDeadlineTimer highlightAnimDeadline;
};

#endif // PROFILECHART_H
