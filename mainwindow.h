#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "fileio.h"
#include "dataset.h"

#include <QMainWindow>
#include <QThread>

#include <memory>
#include <map>
#include <unordered_map>

class CentralHub;
class ProfileChart;
class QLabel;
class QStandardItem;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(CentralHub &hub);

	const QString& getTitle() { return title; }
	FileIO *getIo() { return io; }

public slots:
	void showHelp();
	void displayError(const QString &message);

	void addProtein(ProteinId id);
	void toggleMarker(ProteinId id, bool present);

	void newDataset(Dataset::Ptr data);
	void updateCursorList(QVector<unsigned> samples, QString title);

signals:
	void datasetSelected(unsigned id);
	void partitionsToggled(bool show);

protected:
	void setDataset(Dataset::Ptr data);
	void updateState(Dataset::Touched affected);

	void setupToolbar();
	void setupSignals();
	void setupActions();
	void setupMarkerControls();
	void resetMarkerControls();
	void ensureSortedMarkerItems();
	void setFilename(QString name);
	void setSelectedDataset(unsigned index);

	CentralHub &hub;
	Dataset::Ptr data;

	QString title;

	QTreeWidget *datasetTree;
	std::map<unsigned, QTreeWidgetItem*> datasetItems;
	std::unordered_map<ProteinId, QStandardItem*> markerItems;

	std::vector<Viewer*> views;
	ProfileChart *cursorChart = nullptr;
	FileIO *io;

	struct {
		QAction *datasets;
		QAction *partitions;
		QAction *granularity;
		QAction *famsK;
	} toolbarActions;
};

#endif // MAINWINDOW_H
