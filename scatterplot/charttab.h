#ifndef CHARTTAB_H
#define CHARTTAB_H

#include "ui_charttab.h"
#include "viewer.h"

class Chart;

class ChartTab : public Viewer, private Ui::ChartTab
{
	Q_OBJECT

public:
	explicit ChartTab(QWidget *parent = nullptr);
	void init(Dataset *data) override;

signals:
	void computeDisplay(const QString &name); // to data&storage thread

protected:
	Chart *scene; // owned by view
};

#endif // CHARTTAB_H
