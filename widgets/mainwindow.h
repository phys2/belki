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
class QTreeWidget;
class QStandardItemModel;
class QTreeWidgetItem; // todo remove

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(DataHub &hub);

	const QString& getTitle() const { return title; }
	FileIO *getIo() { return io; }

	void setMarkerControlModel(QStandardItemModel *m);

public slots:
	void showHelp();
	void displayMessage(const QString &message, MessageType type = MessageType::CRITICAL);

	void newDataset(Dataset::Ptr data);

signals:
	void newWindowRequested();
	void closeWindowRequested();
	void datasetSelected(unsigned id);
	void orderChanged(Dataset::OrderBy reference, bool synchronize);
	void markerFlipped(QModelIndex i);
	void markerToggled(ProteinId id, bool present);

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
	void closeEvent(QCloseEvent* event) override;

	void setDataset(Dataset::Ptr data);
	void updateState(Dataset::Touched affected);
	void openFile(Input type, QString filename = {});

	void setupToolbar();
	void setupTabs();
	void setupSignals();
	void setupActions();
	void resetMarkerControls();
	void finalizeMarkerItems();

	void addTab(Tab type);

	void setFilename(QString name);
	void setSelectedDataset(unsigned index);
	void selectStructure(int id);

	void applyAnnotations(unsigned id);
	void applyHierarchy(unsigned id, unsigned granularity);
	void createPartition(unsigned granularity);

	void runInBackground(const std::function<void()> &work);
	void runOnData(const std::function<void(Dataset::Ptr)> &work);

	DataHub &hub;
	Dataset::Ptr data;

	QString title;

	QTreeWidget *datasetTree;
	std::map<unsigned, QTreeWidgetItem*> datasetItems;

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
