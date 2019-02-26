#ifndef DISTMATTAB_H
#define DISTMATTAB_H

#include "ui_distmattab.h"
#include "viewer.h"

class DistmatScene;

class DistmatTab : public Viewer, private Ui::DistmatTab
{
	Q_OBJECT

public:
	explicit DistmatTab(QWidget *parent = nullptr);
	void init(Dataset *data) override;

protected:
	void setupOrderUI();

	DistmatScene *scene;
};

#endif // distmatTAB_H
