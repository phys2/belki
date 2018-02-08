#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dataset.h"

#include <QtWidgets/QMainWindow>

#include <memory>

class QLabel;
class Chart;
namespace QtCharts {
class QChart;
}

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);

	void loadDataset(QString filename);

public slots:
	void updateCursorList(QStringList labels);

protected:
	void updateMarkerControls();

	Chart *chart;
	QtCharts::QChart *cursorChart;
	QLabel *fileLabel;

	std::unique_ptr<Dataset> data;
};

#endif // MAINWINDOW_H
