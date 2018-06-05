#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dataset.h"
#include "storage.h"
#include "fileio.h"

#include <QtWidgets/QMainWindow>
#include <QtCore/QThread>

#include <memory>

class QLabel;
class Chart;
class ProfileChart;
class QStandardItem;

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();

	const QString& getTitle() { return title; }
	FileIO *getIo() { return io; }

signals:
	void loadDataset(const QString &filename);
	void loadAnnotations(const QString &filename);
	void loadHierarchy(const QString &filename);
	void calculatePartition(unsigned granularity);

public slots:
	void showHelp();
	void displayError(const QString &message);
	void updateData();
	void updateCursorList(QVector<unsigned> samples, QString title);

protected:
	void setupToolbar();
	void setupActions();
	void setupMarkerControls();
	void updateMarkerControls();

	QMap<unsigned, QStandardItem*> markerItems;
	Dataset data;
	Storage store;
	QThread dataThread;
	QString title;

	Chart *chart; // initialize after data
	ProfileChart *cursorChart;
	QLabel *fileLabel;
	FileIO *io;
};

#endif // MAINWINDOW_H
