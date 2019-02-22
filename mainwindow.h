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
	void openDataset(const QString &filename);
	void readAnnotations(const QString &name);
	void readHierarchy(const QString &name);
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename);
	void calculatePartition(unsigned granularity);
	void runFAMS();
	void orderProteins(Dataset::OrderBy order);

	// to views
	void reset(bool haveData);
	void repartition();
	void reorder();
	void toggleMarker(unsigned sampleIndex, bool present);

	// other signals
	void updateColorset(QVector<QColor> colors);

public slots:
	void showHelp();
	void displayError(const QString &message);

	void clearData();
	void resetData();
	void updateCursorList(QVector<unsigned> samples, QString title);

protected:
	void setupToolbar();
	void setupSignals();
	void setupActions();
	void setupMarkerControls();
	void resetMarkerControls();

	QMap<unsigned, QStandardItem*> markerItems;
	Dataset data;
	Storage store;
	QThread dataThread;
	QString title;

	std::vector<Viewer*> views;

	ProfileChart *cursorChart;
	QLabel *fileLabel;
	FileIO *io;

	struct {
		QAction *partitions;
		QAction *granularity;
		QAction *famsK;
	} toolbarActions;
};

#endif // MAINWINDOW_H
