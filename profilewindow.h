#ifndef PROFILEWINDOW_H
#define PROFILEWINDOW_H

#include <QMainWindow>

#include "ui_profilewindow.h"

class MainWindow;

namespace QtCharts {
class QChart;
}

class ProfileWindow : public QMainWindow, private Ui::ProfileWindow
{
	Q_OBJECT

	// hack override to cast to right type.
	// Note: only works with object refence (e.g. internally) because non-virtual
	MainWindow *parentWidget();

public:
	explicit ProfileWindow(QtCharts::QChart *source, MainWindow *parent = nullptr);

protected:
	void addSeries(QtCharts::QAbstractSeries* s, bool individual);
	std::pair<std::vector<qreal>, std::vector<qreal>>
	computeMeanStddev(const QList<QtCharts::QAbstractSeries*> &input);

	QtCharts::QChart *chart;
};

#endif // PROFILEWINDOW_H
