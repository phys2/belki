#ifndef DIMREDTAB_H
#define DIMREDTAB_H

#include "ui_dimredtab.h"
#include "viewer.h"

#include <QString>
#include <QVector>
#include <QColor>
#include <memory>

class Dataset;
class Chart;

class DimredTab : public Viewer, private Ui::DimredTab
{
	Q_OBJECT

public:
	explicit DimredTab(QWidget *parent = nullptr);
	~DimredTab() override;

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

	QString currentMethod() const;

protected:
	struct DataState {
		QString displayName;
		Dataset::Ptr data;
		std::unique_ptr<Chart> scene;
	};

	struct {
		bool showPartitions;
		QString preferredDisplay;
		QVector<QColor> colorset;
	} guiState;

	void selectDisplay(const QString& name);
	void computeDisplay(const QString &method);
	void updateMenus();
	void updateEnabled();

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
