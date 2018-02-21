#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dataset.h"

#include <QtWidgets/QMainWindow>
#include <QtCore/QThread>

#include <memory>

class QLabel;
class Chart;
class QStandardItem;
namespace QtCharts {
class QChart;
}

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();

signals:
	void loadDataset(const QString &filename);
	void loadAnnotations(const QString &filename);

public slots:
	void showHelp();
	void displayError(const QString &message);
	void updateData(const QString &filename);
	void updateCursorList(QVector<unsigned> samples);

protected:
	void setupMarkerControls();
	void updateMarkerControls();

	QMap<unsigned, QStandardItem*> markerItems;
	Dataset data;
	QThread dataThread;

	Chart *chart; // initialize after data
	QtCharts::QChart *cursorChart;
	QLabel *fileLabel;
};

#endif // MAINWINDOW_H
