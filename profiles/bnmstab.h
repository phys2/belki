#ifndef BNMSTAB_H
#define BNMSTAB_H

#include "ui_bnmstab.h"
#include "viewer.h"
#include "bnmsmodel.h"

#include <QIdentityProxyModel>
#include <QMenu>

#include <memory>

class BnmsChart;
class ReferenceChart;
class RangeSelectItem;

class BnmsTab : public Viewer, private Ui::BnmsTab
{
	Q_OBJECT

public:
	explicit BnmsTab(QWidget *parent = nullptr);
	~BnmsTab();

	void setWindowState(std::shared_ptr<WindowState> s) override;
	void setProteinModel(QAbstractItemModel *) override;

	void selectDataset(unsigned id) override;
	void deselectDataset() override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState : public Viewer::DataState {
		using Viewer::DataState::DataState;
		~DataState(); // for unique_ptr
		std::unique_ptr<BnmsChart> scene;
		std::unique_ptr<ReferenceChart> refScene;
		std::unique_ptr<RangeSelectItem> rangeSelect;
		bool logSpace = false;
		// components per-protein in dataset index (note: this should not be here)
		std::vector<Components> components;
	};

	/* our proxy that removes checkable and check state */
	struct NoCheckstateProxyModel : QIdentityProxyModel {
		using QIdentityProxyModel::QIdentityProxyModel;
		QVariant data(const QModelIndex &index, int role) const override;
		Qt::ItemFlags flags(const QModelIndex &index) const override;
	};

	bool updateIsEnabled() override;

	DataState &selected() { return selectedAs<DataState>(); }
	std::unique_ptr<QMenu> proteinMenu(ProteinId id);
	void toggleComponentMode(bool on); // call through actionComponentToggle
	void setReference(ProteinId id);
	void addToHistory(ProteinId id);
	void setupMarkerMenu();
	void loadComponents();

	struct {
		ProteinId reference = 0; // first protein
		bool zoomToRange = false;
		bool componentMode = false;
		bool showLabels = true;
		bool showAverage = false;
		bool showQuantiles = false;
	} tabState;

	NoCheckstateProxyModel proteinModel;
	QMenu historyMenu, markerMenu;
};

#endif
