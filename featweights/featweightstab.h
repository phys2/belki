#ifndef FEATWEIGHTSTAB_H
#define FEATWEIGHTSTAB_H

#include "ui_featweightstab.h"
#include "viewer.h"

class FeatweightsScene;

class FeatweightsTab : public Viewer, private Ui::FeatweightsTab
{
	Q_OBJECT

public:
	explicit FeatweightsTab(QWidget *parent = nullptr);
	void init(Dataset *data) override;

protected:
	void setupWeightingUI();

	FeatweightsScene *scene;
};

#endif
