#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dataset.h"

#include <QMainWindow>

#include <map>
#include <unordered_map>

class CentralHub;
class FileIO;
class QLabel;
class QStandardItem;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(CentralHub &hub);

	const QString& getTitle() const { return title; }
	FileIO *getIo() { return io; }

public slots:
	void showHelp();
	void displayError(const QString &message);

	void addProtein(ProteinId id);
	void toggleMarker(ProteinId id, bool present);

	void newDataset(Dataset::Ptr data);

signals:
	void datasetSelected(unsigned id);
	void partitionsToggled(bool show);
	void orderChanged(Dataset::OrderBy reference, bool synchronize);

protected:
	enum class Input {
		DATASET, DATASET_RAW,
		STRUCTURE,
		MARKERS,
		DESCRIPTIONS
	};

	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

	void setDataset(Dataset::Ptr data);
	void updateState(Dataset::Touched affected);
	void openFile(Input type, QString filename = {});

	void setupToolbar();
	void setupSignals();
	void setupActions();
	void setupMarkerControls();
	void resetMarkerControls();
	void finalizeMarkerItems();
	void setFilename(QString name);
	void setSelectedDataset(unsigned index);

	void selectStructure(int id);

	CentralHub &hub;
	Dataset::Ptr data;

	QString title;

	QTreeWidget *datasetTree;
	std::map<unsigned, QTreeWidgetItem*> datasetItems;
	std::unordered_map<ProteinId, QStandardItem*> markerItems;

	std::vector<Viewer*> views;
	FileIO *io;

	struct {
		QAction *datasets;
		QAction *structure;
		QAction *granularity;
		QAction *famsK;
	} toolbarActions;
};

#endif // MAINWINDOW_H
