#ifndef BNMSTAB_H
#define BNMSTAB_H

#include "ui_bnmstab.h"
#include "viewer.h"

#include <QIdentityProxyModel>

#include <memory>
#include <set>
#include <unordered_map>

class BnmsChart;

class BnmsTab : public Viewer, private Ui::BnmsTab
{
	Q_OBJECT

public:
	explicit BnmsTab(QWidget *parent = nullptr);

	void setProteinModel(QAbstractItemModel *) override;

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState : public Viewer::DataState {
		std::unique_ptr<BnmsChart> scene;
		bool logSpace = false;
	};

	void updateEnabled();

	struct {
		ProteinId reference = 0; // first protein
		bool showLabels = true;
		bool showAverage = false;
		bool showQuantiles = false;
	} tabState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
