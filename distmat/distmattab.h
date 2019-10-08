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

	void selectDataset(unsigned id) override;
	void addDataset(Dataset::Ptr data) override;

protected:
	struct DataState : public Viewer::DataState {
		std::unique_ptr<DistmatScene> scene;
	};

	void setupOrderUI();
	void updateEnabled();

	struct {
		Dataset::Direction direction = Dataset::Direction::PER_DIMENSION;
		bool showPartitions; // initialized by MainWindow
		QVector<QColor> colorset; // initialized by MainWindow
	} guiState;

	ContentMap<DataState> content;
	Current<DataState> current;
};

#endif // distmatTAB_H
