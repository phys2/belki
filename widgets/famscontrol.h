#ifndef FAMSCONTROL_H
#define FAMSCONTROL_H

#include "ui_famscontrol.h"
#include "viewer.h"

class FAMSControl : public Viewer, private Ui::FAMSControl
{
	Q_OBJECT

public:
	explicit FAMSControl(QWidget *parent = nullptr);
	~FAMSControl();

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

	void configure();
	void run();

protected:
	struct DataState : public Viewer::DataState {
		using Viewer::DataState::DataState;

		enum Step {
			IDLE,
			COMPUTING,
			ABORTING
		} step = IDLE;
		unsigned job = 0;
	};

	bool updateIsEnabled() override;
	bool isAvailable();

	DataState &selected() { return selectedAs<DataState>(); }
};

#endif // FAMSCONTROL_H
