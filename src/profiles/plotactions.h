#ifndef PLOTACTIONS_H
#define PLOTACTIONS_H

#include <QObject>

class QAction;
class QToolBar;
class QToolButton;
class QMainWindow;
class ProfileChart;

class PlotActions : public QObject
{
	Q_OBJECT
public:
	struct CapturePlotActions {
		QAction *head = nullptr, *toFile = nullptr, *toClipboard = nullptr;
	};

	struct Actions {
		QAction *logarithmic = nullptr;
		QAction *showLabels = nullptr;
		QAction *showAverage = nullptr, *showQuantiles = nullptr, *showIndividual = nullptr;
		QAction *zoomToGlobal = nullptr, *zoomToVisible = nullptr;
		CapturePlotActions capturePlot;
	};

	static QAction *createAction(QObject *parent, QIcon icon, QString title, QString tooltip,
	                         bool isToggle, QString shortcut = {});
	static CapturePlotActions createCapturePlotActions(QWidget *parent);
	static void addCaptureButton(const CapturePlotActions &actions, QToolBar *target);

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
	void capturePlot(bool toFile);

public slots:
	void updateZoom(QAction *origin = nullptr);

protected:
	QToolBar *toolbar;

	Actions actions;
	ProfileChart *chart = nullptr;
};

#endif // PLOTACTIONS_H
