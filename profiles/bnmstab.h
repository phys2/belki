#ifndef BNMSTAB_H
#define BNMSTAB_H

#include "ui_bnmstab.h"
#include "viewer.h"

#include <QIdentityProxyModel>

#include <memory>
#include <set>
#include <unordered_map>

class ProfileChart;

class BnmsTab : public Viewer, private Ui::BnmsTab
{
	Q_OBJECT

public:
	explicit BnmsTab(QWidget *parent = nullptr);

	void setProteinModel(QAbstractItemModel *) override;

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

	bool eventFilter(QObject *watched, QEvent *event) override;

protected:
	struct DataState : public Viewer::DataState {
		std::unique_ptr<ProfileChart> scene;
		bool logSpace = false;
	};

	/* our proxy that reflects extra proteins in plot (transforms check state) */
	struct RemoveCheckstateProxyModel : QIdentityProxyModel {
		Qt::ItemFlags flags(const QModelIndex &index) const override;
	};

	void rebuildPlot(); // TODO temporary hack
	void updateEnabled();
	void setupProteinBox();

	RemoveCheckstateProxyModel proteinModel;

	struct {
		std::set<ProteinId> extras;
		bool showLabels = false;
		bool showAverage = false;
		bool showQuantiles = false;
	} tabState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
