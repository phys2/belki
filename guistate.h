#ifndef GUISTATE_H
#define GUISTATE_H

#include "model.h"
#include "utils.h"

#include <QObject>
#include <QStandardItemModel>
#include <memory>

class MainWindow;
class DataHub;
class ProteinDB;
class Storage;
class Dataset;
class QStandardItem;
class QStandardItemModel;
class QMenu;

class GuiState : public QObject
{
	Q_OBJECT

public:
	explicit GuiState(DataHub &hub);
	~GuiState();

	std::unique_ptr<QMenu> proteinMenu(ProteinId id);

	bool eventFilter(QObject *watched, QEvent *event) override;

signals:
	void instanceRequested(const QString &filename);
	void closed();

public slots:
	void addWindow();
	void removeWindow(unsigned id);
	void openProject(const QString &filename);

	void addDataset(std::shared_ptr<Dataset> dataset);
	void addProtein(ProteinId id, const Protein &protein);
	void flipMarker(QModelIndex i);
	void toggleMarker(ProteinId id, bool present);

	void handleMarkerChange(QStandardItem *item);

	void displayMessage(const GuiMessage &message);

public:
	DataHub &hub;
	ProteinDB &proteins;

protected:
	void shutdown();
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
};

#endif
