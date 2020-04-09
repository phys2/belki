#ifndef DIMREDTAB_H
#define DIMREDTAB_H

#include "ui_dimredtab.h"
#include "viewer.h"
#include "compute/dimred.h"

class Chart;

class DimredTab : public Viewer, private Ui::DimredTab
{
	Q_OBJECT
public:
	explicit DimredTab(QWidget *parent = nullptr);
	~DimredTab() override;

	void setWindowState(std::shared_ptr<WindowState> s) override;

	void selectDataset(unsigned id) override;
	void deselectDataset() override;
	void addDataset(Dataset::Ptr data) override;

	QString currentMethod() const;

protected:
	struct DataState : public Viewer::DataState {
		using Viewer::DataState::DataState;
		QString displayName;
		std::unique_ptr<Chart> scene;
	};

	bool updateIsEnabled() override;

	DataState &selected() { return selectedAs<DataState>(); }
	void selectDisplay(const QString& name);
	void computeDisplay(const dimred::Method &method);
	void updateMenus();

	struct {
		// TODO this is crap. Have a list of preferences instead,
		// so if the user triggers computation of several, they actually get
		// respected regardless of finishing order
		QString preferredDisplay; // init to none
	} tabState;
};

#endif
