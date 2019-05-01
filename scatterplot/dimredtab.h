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

	std::pair<QString, QVariant> currentMethod() const;

signals:
	void computeDisplay(const QString &name); // to data&storage thread

protected:
	void updateComputeMenu();

	Chart *scene; // owned by view
};

#endif
