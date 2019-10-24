#ifndef FEATWEIGHTSTAB_H
#define FEATWEIGHTSTAB_H

#include "ui_featweightstab.h"
#include "viewer.h"
#include "featweightsscene.h"

#include <vector>
#include <memory>
#include <cmath>

class QAction;

class FeatweightsTab : public Viewer, private Ui::FeatweightsTab
{
	Q_OBJECT

public:
	explicit FeatweightsTab(QWidget *parent = nullptr);

	void setWindowState(std::shared_ptr<WindowState> s) override;

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState : public Viewer::DataState {
		double scoreThreshold;
		std::unique_ptr<FeatweightsScene> scene;
	};

	void setupWeightingUI();
	void updateScoreSlider();
	void updateEnabled();

	struct {
		bool useAlternate = false;
		FeatweightsScene::Weighting weighting = FeatweightsScene::Weighting::OFFSET;
	} tabState;

	std::vector<QAction*> scoreActions;
	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
