#ifndef GUISTATE_H
#define GUISTATE_H

#include "datahub.h"

#include <QObject>
#include <QStandardItemModel>

class QStandardItem;
class QStandardItemModel;
class MainWindow;

class GuiState : public QObject
{
	Q_OBJECT

public:
	explicit GuiState(DataHub &hub);

	bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
	unsigned addWindow();
	void removeWindow(unsigned id);

	void addDataset(Dataset::Ptr dataset);
	void addProtein(ProteinId id, const Protein &protein);
	void flipMarker(QModelIndex i);
	void toggleMarker(ProteinId id, bool present);

	void handleMarkerChange(QStandardItem *item);

	void displayMessage(const QString &message, MessageType type = MessageType::CRITICAL);

protected:
	void sortMarkerModel();

	MainWindow *focused();

	std::map<unsigned, MainWindow*> windows;
	MainWindow *lastFocused = nullptr;
	unsigned nextId = 1;

	struct {
		QStandardItemModel model;
		std::map<unsigned, QStandardItem*> items;
	} datasetControl;

	struct {
		QStandardItemModel model;
		std::unordered_map<ProteinId, QStandardItem*> items;
		bool dirty = false;
	} markerControl;

	QStandardItemModel structureModel;

	DataHub &hub;
};

#endif
