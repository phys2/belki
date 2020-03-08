#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "dataset.h"
#include "utils.h"

#include <QMainWindow>
#include <QIdentityProxyModel>

#include <map>
#include <unordered_map>
#include <unordered_set>

class DataHub;
class WindowState;
class QLabel;
class QTreeView;
class QStandardItemModel;
class QTreeWidgetItem; // todo remove

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(std::shared_ptr<WindowState> state);

	void setDatasetControlModel(QStandardItemModel *m);
	void setMarkerControlModel(QStandardItemModel *m);
	void setStructureControlModel(QStandardItemModel *m);

public slots:
	void showHelp();
	void setName(const QString &name, const QString &path);
	void setDataset(Dataset::Ptr data);
	void selectStructure(int id);

signals:
	void message(const GuiMessage &message);
	void newWindowRequested();
	void closeWindowRequested();
	void closeProjectRequested();
	void openProjectRequested(QString filename);
	void datasetSelected(unsigned id);
	void markerFlipped(QModelIndex i);
	void markerToggled(ProteinId id, bool present);

protected:
	enum class Input {
		DATASET, DATASET_RAW,
		STRUCTURE,
		MARKERS,
		DESCRIPTIONS,
		PROJECT
	};

	enum class Tab { // see also tabTitles!
		DIMRED, SCATTER, HEATMAP, DISTMAT, PROFILES, FEATWEIGHTS, BNMS
	};

	/* our proxy to individually enable/disable protein entries based on dataset */
	struct CustomEnableProxyModel : QIdentityProxyModel {
		using QIdentityProxyModel::QIdentityProxyModel;

		Qt::ItemFlags flags(const QModelIndex &index) const override;

		// TODO: use more clever/efficient cache which items should be enabled (in itemdata?)
		// esp. we can do it per-dataset instead of per-window
		std::unordered_set<ProteinId> available;
	};

	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void closeEvent(QCloseEvent* event) override;

	void updateState(Dataset::Touched affected);
	void openFile(Input type, QString filename = {});

	void setupModelViews();
	void setupToolbar();
	void setupTabs();
	void setupSignals();
	void setupActions();

	void addTab(Tab type);

	void setSelectedDataset(unsigned id);

	void selectAnnotations(const Annotations::Meta &desc);
	void selectFAMS(float k);
	void selectHierarchy(unsigned id, unsigned granularity);
	void switchHierarchyPartition(unsigned granularity);

	std::unique_ptr<Annotations> currentAnnotations();

	void runInBackground(const std::function<void()> &work);
	void runOnData(const std::function<void(Dataset::Ptr)> &work);

	Dataset::Ptr data;
	std::shared_ptr<WindowState> state;

	CustomEnableProxyModel markerModel;
	QTreeView *datasetTree;

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
	    {Tab::BNMS, "Matching"},
	};
	std::multiset<Tab> tabHistory; // used as per-type incrementing counter
};

#endif // MAINWINDOW_H
