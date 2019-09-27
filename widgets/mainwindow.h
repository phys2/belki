#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dataset.h"
#include "utils.h"

#include <QMainWindow>

#include <map>
#include <unordered_map>

class DataHub;
class FileIO;
class QLabel;
class QStandardItem;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(DataHub &hub);

	const QString& getTitle() const { return title; }
	FileIO *getIo() { return io; }

public slots:
	void showHelp();
	void displayMessage(const QString &message, MessageType type = MessageType::CRITICAL);

	void addProtein(ProteinId id, const Protein &protein);
	void toggleMarker(ProteinId id, bool present);

	void newDataset(Dataset::Ptr data);

signals:
	void datasetSelected(unsigned id);
	void orderChanged(Dataset::OrderBy reference, bool synchronize);

protected:
	enum class Input {
		DATASET, DATASET_RAW,
		STRUCTURE,
		MARKERS,
		DESCRIPTIONS
	};

	enum class Tab {
		DIMRED, SCATTER, HEATMAP, DISTMAT, PROFILES, FEATWEIGHTS
	};

	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

	void setDataset(Dataset::Ptr data);
	void updateState(Dataset::Touched affected);
	void openFile(Input type, QString filename = {});

	void setupToolbar();
	void setupTabs();
	void setupSignals();
	void setupActions();
	void setupMarkerControls();
	void resetMarkerControls();
	void finalizeMarkerItems();

	void addTab(Tab type);

	void setFilename(QString name);
	void setSelectedDataset(unsigned index);
	void selectStructure(int id);

	DataHub &hub;
	Dataset::Ptr data;

	QString title;

	QTreeWidget *datasetTree;
	std::map<unsigned, QTreeWidgetItem*> datasetItems;
	std::unordered_map<ProteinId, QStandardItem*> markerItems;

	FileIO *io;

	struct {
		QAction *datasets;
		QAction *structure;
		QAction *granularity;
		QAction *famsK;
	} toolbarActions;
	std::unique_ptr<QMenu> tabMenu;

	inline static const std::map<Tab, QString> tabTitles = {
	    {Tab::DIMRED, "Visualization"},
	    {Tab::SCATTER, "Scatter Plot"},
	    {Tab::HEATMAP, "Heatmap"},
	    {Tab::DISTMAT, "Distance Map"},
	    {Tab::PROFILES, "Profiles"},
	    {Tab::FEATWEIGHTS, "Feature Weighting"},
	};
	std::multiset<Tab> tabHistory; // used as per-type incrementing counter
};

#endif // MAINWINDOW_H
