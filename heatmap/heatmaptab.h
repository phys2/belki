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

	void setWindowState(std::shared_ptr<WindowState> s) override;

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState : public Viewer::DataState {
		using Viewer::DataState::DataState;
		std::unique_ptr<HeatmapScene> scene;
	};

	bool updateIsEnabled() override;

	DataState &selected() { return selectedAs<DataState>(); }
	void setupOrderUI();

	struct {
		bool singleColumn = false;
	} tabState;
};

#endif // HEATMAPTAB_H
