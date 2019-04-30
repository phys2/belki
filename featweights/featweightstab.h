#ifndef FEATWEIGHTSTAB_H
#define FEATWEIGHTSTAB_H

#include "ui_featweightstab.h"
#include "viewer.h"

#include <vector>
#include <memory>

class FeatweightsScene;
class QAction;

class FeatweightsTab : public Viewer, private Ui::FeatweightsTab
{
	Q_OBJECT

public:
	explicit FeatweightsTab(QWidget *parent = nullptr);
	void init(Dataset *data) override;

protected:
	std::vector<QAction*> scoreActions;

	void setupWeightingUI();

	std::unique_ptr<FeatweightsScene> scene;
};

#endif
