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
	struct DataState : public Viewer::DataState {
		QString displayName;
		std::unique_ptr<Chart> scene;
	};

	void selectDisplay(const QString& name);
	void computeDisplay(const QString &name, const QString &id);
	void updateMenus();
	bool updateEnabled();

	struct {
		QString preferredDisplay; // init to none
		bool showPartitions; // initialized by MainWindow
		QVector<QColor> colorset; // initialized by MainWindow
	} guiState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
