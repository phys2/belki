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
	~ScatterTab() override;

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState {
		int dimension = -1;
		bool hasScores;
		Dataset::Ptr data;
		std::unique_ptr<Chart> scene;
	};

	void selectDimension(int index);
	bool updateEnabled();

	struct {
		bool showPartitions;
		QVector<QColor> colorset;
	} guiState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
