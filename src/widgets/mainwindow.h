#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "utils.h"
#include "dataset.h"

#include <QMainWindow>
#include <QSortFilterProxyModel>

#include <map>
#include <unordered_map>
#include <unordered_set>

class DataHub;
class WindowState;
class QLabel;
class QTreeView;
class QStandardItemModel;
class JobStatus;
class FAMSControl;
class GuiState;

class MainWindow : public QMainWindow, private Ui::MainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(GuiState &owner);

	void setDatasetControlModel(QStandardItemModel *m);
	void setMarkerControlModel(QStandardItemModel *m);
	void setStructureControlModel(QStandardItemModel *m);

public slots:
	void showHelp();
	void setName(const QString &name, const QString &path);
	void setDataset(Dataset::Ptr data);
	void removeDataset(unsigned id);
	void selectStructure(int id);

signals:
	void message(const GuiMessage &message);
	void newWindowRequested();
	void closeWindowRequested();
	void closeProjectRequested();
	void openProjectRequested(QString filename);
	void quitApplicationRequested();
	void datasetSelected(unsigned id);
	void datasetDeselected();
	void markerFlipped(QModelIndex i);

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

	/* our proxy to individually:
	 * (1) filter proteins, if desired, to only show markers
	 * (2) enable/disable protein entries based on dataset
	 */
	struct CustomShowAndEnableProxyModel : QSortFilterProxyModel {
		using QSortFilterProxyModel::QSortFilterProxyModel;
		void invalidateFilter() { QSortFilterProxyModel::invalidateFilter(); } // publicify

		bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
		Qt::ItemFlags flags(const QModelIndex &index) const override; // for 'enabled' flag

		// TODO: use more clever/efficient cache which items should be enabled (in itemdata?)
		// esp. we can do it per-dataset instead of per-window (fill-in once per new-dataset)
		std::unordered_set<ProteinId> available;
		bool onlyMarkers = false;
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
	void selectHierarchy(unsigned id, unsigned granularity, bool pruned);
	void switchHierarchyPartition(unsigned granularity, bool pruned);
	std::optional<Annotations> currentAnnotations();

	Dataset::Ptr data;
	std::shared_ptr<WindowState> state;

	CustomShowAndEnableProxyModel markerModel;
	QTreeView *datasetTree;
	JobStatus *jobWidget;
	FAMSControl *famsControl;

	struct {
		QAction *datasets;
		QAction *structure;
		QActionGroup *hierarchy;
		QAction *fams;
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
