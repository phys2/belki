#ifndef PROFILETAB_H
#define PROFILETAB_H

#include "ui_profiletab.h"
#include "viewer.h"

#include <QIdentityProxyModel>

#include <memory>
#include <set>
#include <unordered_map>

class ProfileChart;
class QMenu;

class ProfileTab : public Viewer, private Ui::ProfileTab
{
	Q_OBJECT

public:
	explicit ProfileTab(QWidget *parent = nullptr);
	~ProfileTab();

	void setWindowState(std::shared_ptr<WindowState> s) override;
	void setProteinModel(QAbstractItemModel *) override;

	void selectDataset(unsigned id) override;
	void deselectDataset() override;
	void addDataset(Dataset::Ptr data) override;

	bool eventFilter(QObject *watched, QEvent *event) override;

protected:
	struct DataState : public Viewer::DataState {
		using Viewer::DataState::DataState;
		std::unique_ptr<ProfileChart> scene;
		bool logSpace = false;
	};

	/* our proxy that reflects extra proteins in plot (transforms check state) */
	struct CustomCheckedProxyModel : QIdentityProxyModel {
		CustomCheckedProxyModel(std::set<ProteinId> &marked) : marked(marked) {}

		QVariant data(const QModelIndex &index, int role) const override;

		std::set<ProteinId> &marked;
	};

	bool updateIsEnabled() override;

	DataState &selected() { return selectedAs<DataState>(); }
	std::unique_ptr<QMenu> proteinMenu(ProteinId id);
	void rebuildPlot(); // TODO temporary hack
	void toggleExtra(ProteinId id);
	void setupProteinBox();

	CustomCheckedProxyModel proteinModel;

	struct {
		std::set<ProteinId> extras;
		bool showLabels = false;
		bool showAverage = false;
		bool showQuantiles = false;
	} tabState;
};

#endif
