#ifndef PLOTACTIONS_H
#define PLOTACTIONS_H

#include <QObject>

class QAction;
class QToolBar;
class QMainWindow;
class ProfileChart;

class PlotActions : public QObject
{
	Q_OBJECT
public:
	struct Actions {
		QAction *logarithmic = nullptr;
		QAction *showLabels = nullptr;
		QAction *showAverage = nullptr, *showQuantiles = nullptr, *showIndividual = nullptr;
		QAction *zoomToGlobal = nullptr, *zoomToVisible = nullptr;
		QAction *savePlot = nullptr;
	};

	explicit PlotActions(QObject *parent = nullptr);
	void setupActions(bool labels, bool average, bool quantiles, bool individual);
	void attachTo(QMainWindow *target);
	void attachTo(ProfileChart *chart);
	void detachFromChart();
	void addAction(QAction *action);

	void setLogarithmic(bool on);
	void setAverageIndividual(bool averageEnabled, bool averageOn, bool individualOn);

signals:
	void toggleLogarithmic(bool on);
	void zoomToGlobal();
	void zoomToVisible();
	void savePlot();

protected:
	QToolBar *toolbar;

	Actions actions;
	ProfileChart *chart = nullptr;
};

#endif // PLOTACTIONS_H
