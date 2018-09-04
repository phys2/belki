#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dimred.h"
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
	void openDataset(const QString &filename);
	void readAnnotations(const QString &name);
	void readHierarchy(const QString &name);
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename);
	void computeDisplay(const QString &name);
	void calculatePartition(unsigned granularity);

public slots:
	void showHelp();
	void displayError(const QString &message);

	void resetData();
	void updateData(const QString &display);
	void updateCursorList(QVector<unsigned> samples, QString title);

protected:
	void setupToolbar();
	void setupSignals();
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

	struct {
		QAction *partitions = nullptr;
		QAction *granularity = nullptr;
	} toolbarActions;
};

#endif // MAINWINDOW_H
