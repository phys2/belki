#ifndef SCATTERTAB_H
#define SCATTERTAB_H

#include "ui_scattertab.h"
#include "viewer.h"

class Chart;

class ScatterTab : public Viewer, private Ui::ScatterTab
{
	Q_OBJECT

public:
	explicit ScatterTab(QWidget *parent = nullptr);
	void init(Dataset *data) override;

protected:
	Chart *scene; // owned by view
};

#endif
