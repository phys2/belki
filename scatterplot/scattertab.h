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

	void setWindowState(std::shared_ptr<WindowState> s) override;

	void selectDataset(unsigned id) override;
	void deselectDataset() override;
	void addDataset(Dataset::Ptr data) override;
	void removeDataset(unsigned id) override;

protected:
	struct DataState : public Viewer::DataState {
		int dimension = -1; // -1 means none
		int secondaryDimension = -1; // -1 means scores
		bool hasScores;
		std::unique_ptr<Chart> scene;
	};

	void refillDimensionSelects(bool onlySecondary = false);
	void selectDimension(int index);
	void selectSecondaryDimension(int index);
	bool updateEnabled();

	struct {} tabState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif
