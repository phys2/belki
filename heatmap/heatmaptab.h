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
	void init(Dataset *data) override;

protected:
	void setupOrderUI();

	std::unique_ptr<HeatmapScene> scene;
};

#endif // HEATMAPTAB_H
