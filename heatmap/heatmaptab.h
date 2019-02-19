#ifndef HEATMAPTAB_H
#define HEATMAPTAB_H

#include "ui_heatmaptab.h"
#include "viewer.h"

class HeatmapScene;

class HeatmapTab : public Viewer, private Ui::HeatmapTab
{
	Q_OBJECT

public:
	explicit HeatmapTab(QWidget *parent = nullptr);
	void init(Dataset *data) override;

protected:
	HeatmapScene *scene;
};

#endif // HEATMAPTAB_H
