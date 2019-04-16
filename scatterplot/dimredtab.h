#ifndef DIMREDTAB_H
#define DIMREDTAB_H

#include "ui_dimredtab.h"
#include "viewer.h"

class Chart;

class DimredTab : public Viewer, private Ui::DimredTab
{
	Q_OBJECT

public:
	explicit DimredTab(QWidget *parent = nullptr);
	void init(Dataset *data) override;

signals:
	void computeDisplay(const QString &name); // to data&storage thread

protected:
	Chart *scene; // owned by view
};

#endif
