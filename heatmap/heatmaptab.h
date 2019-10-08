#ifndef HEATMAPTAB_H
#define HEATMAPTAB_H

#include "ui_heatmaptab.h"
#include "viewer.h"

#include <memory>

class HeatmapScene;

class HeatmapTab : public Viewer, private Ui::HeatmapTab
{
	Q_OBJECT

public:
	explicit HeatmapTab(QWidget *parent = nullptr);

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState : public Viewer::DataState {
		std::unique_ptr<HeatmapScene> scene;
	};

	void setupOrderUI();
	void updateEnabled();

	struct {
		bool singleColumn = false;
		bool showPartitions; // initialized by MainWindow
	} guiState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif // HEATMAPTAB_H
