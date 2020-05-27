#ifndef DISTMATTAB_H
#define DISTMATTAB_H

#include "ui_distmattab.h"
#include "viewer.h"

#include <memory>

class DistmatScene;

class DistmatTab : public Viewer, private Ui::DistmatTab
{
	Q_OBJECT

public:
	explicit DistmatTab(QWidget *parent = nullptr);

	void setWindowState(std::shared_ptr<WindowState> s) override;

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState : public Viewer::DataState {
		using Viewer::DataState::DataState;
		std::unique_ptr<DistmatScene> scene;
	};

	bool updateIsEnabled() override;

	DataState &selected() { return selectedAs<DataState>(); }
	void setupOrderUI();

	struct {
		Dataset::Direction direction = Dataset::Direction::PER_DIMENSION;
	} tabState;
};

#endif // distmatTAB_H
