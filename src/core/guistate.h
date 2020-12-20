#ifndef GUISTATE_H
#define GUISTATE_H

#include "model.h"
#include "utils.h"

#include <QObject>
#include <QStandardItemModel>
#include <memory>

class FileIO;
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
	bool shutdown(bool withPrompt = true);

	bool eventFilter(QObject *watched, QEvent *event) override;

signals:
	void instanceRequested(const QString &filename = {});
	void quitRequested();
	void closed();

public slots:
	void addWindow();
	void removeWindow(unsigned id, bool withPrompt = true);
	void openProject(const QString &filename);

	void addDataset(std::shared_ptr<Dataset> dataset);
	void removeDataset(unsigned id);
	void addProtein(ProteinId id, const Protein &protein);
	void flipMarker(QModelIndex i);

	void handleMarkerChange(QStandardItem *item);

	void displayMessage(const GuiMessage &message);
	void displayMessageAt(const GuiMessage &message, QWidget *parent = nullptr);

	// job monitor interface
	void addJob(unsigned jobId) { runningJobs.insert(jobId); }
	void updateJob(unsigned) {}
	void removeJob(unsigned jobId) { runningJobs.erase(jobId); }

public:
	DataHub &hub;
	ProteinDB &proteins;
	std::unique_ptr<FileIO> io;

protected:
	bool promptOnClose(QWidget *parent = nullptr);
	void sortMarkerModel();

	MainWindow *focused();

	std::map<unsigned, MainWindow*> windows;
	MainWindow *lastFocused = nullptr;
	unsigned nextId = 1;

	struct {
		QStandardItemModel model;
		std::map<unsigned, QStandardItem*> items;
	} datasets;

	struct {
		QStandardItemModel model;
		std::unordered_map<ProteinId, QStandardItem*> items;
		bool dirty = false;
	} markers;

	QStandardItemModel structureModel;

	std::set<unsigned> runningJobs;
};

#endif
