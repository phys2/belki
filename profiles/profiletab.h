#ifndef PROFILETAB_H
#define PROFILETAB_H

#include "ui_profiletab.h"
#include "viewer.h"

#include <memory>
#include <unordered_map>

class ProfileChart;
class QStandardItem;

class ProfileTab : public Viewer, private Ui::ProfileTab
{
	Q_OBJECT

public:
	explicit ProfileTab(QWidget *parent = nullptr);

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

	bool eventFilter(QObject *watched, QEvent *event) override;

protected:
	struct DataState : public Viewer::DataState {
		std::unique_ptr<ProfileChart> scene;
		bool logSpace = false;
	};

	void rebuildPlot(); // TODO temporary hack
	void addProtein(ProteinId id, const Protein &protein);
	void updateEnabled();
	void setupProteinBox();
	void finalizeProteinBox();
	void updateProteinItems();

	std::unordered_map<ProteinId, QStandardItem*> proteinItems;
	bool proteinModelDirty = false;

	struct {
		std::set<ProteinId> extras;
		bool showLabels = false;
	} guiState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
