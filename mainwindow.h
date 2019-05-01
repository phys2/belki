#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dataset.h"
#include "storage.h"
#include "fileio.h"

#include <QMainWindow>
#include <QThread>

#include <memory>

class Chart; // TODO remove
class QLabel;
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
	// to Dataset/Storage thread
	void selectDataset(unsigned index);
	void openDataset(const QString &filename);
	void readAnnotations(const QString &name);
	void readHierarchy(const QString &name);
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename);
	void clearClusters();
	void calculatePartition(unsigned granularity);
	void runFAMS();

	// to views
	void reset(bool haveData);
	void repartition(bool withOrder);
	void reorder();
	void toggleMarker(unsigned sampleIndex, bool present);
	void togglePartitions(bool show);

	// other signals
	void updateColorset(QVector<QColor> colors);

public slots:
	void showHelp();
	void displayError(const QString &message);

	void resetData();
	void newData(unsigned index);
	void updateCursorList(QVector<unsigned> samples, QString title);

protected:
	void clearData();

	void setupToolbar();
	void setupSignals();
	void setupActions();
	void setupMarkerControls();
	void resetMarkerControls();
	void setFilename(QString name);

	QMap<unsigned, QStandardItem*> markerItems;
	Dataset data;
	Storage store;
	QThread dataThread;
	QString title;

	std::vector<Viewer*> views;

	ProfileChart *cursorChart;
	FileIO *io;

	struct {
		QAction *datasets;
		QAction *partitions;
		QAction *granularity;
		QAction *famsK;
	} toolbarActions;
};

#endif // MAINWINDOW_H
