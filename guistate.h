#ifndef GUISTATE_H
#define GUISTATE_H

#include "datahub.h"

#include <QObject>

class QStandardItem;
class QStandardItemModel;
class MainWindow;

class GuiState : public QObject
{
	Q_OBJECT

public:
	explicit GuiState(DataHub &hub);

public slots:
	unsigned addWindow();
	void removeWindow(unsigned id);

	void addDataset(Dataset::Ptr dataset);
	void addProtein(ProteinId id, const Protein &protein);
	void flipMarker(QModelIndex i);
	void toggleMarker(ProteinId id, bool present);

protected:
	void setupMarkerControl();
	void sortMarkerModel();

	std::map<unsigned, MainWindow*> windows;
	unsigned nextId = 1;

	struct {
		QStandardItemModel *model;
		std::map<unsigned, QStandardItem*> items;
	} datasetControl;

	struct {
		QStandardItemModel *model;
		std::unordered_map<ProteinId, QStandardItem*> items;
		bool dirty = false;
	} markerControl;

	DataHub &hub;
};

#endif
