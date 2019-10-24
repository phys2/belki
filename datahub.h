#ifndef DATAHUB_H
#define DATAHUB_H

#include "dataset.h" // for Dataset::OrderBy
#include "proteindb.h"
#include "storage.h"
#include "utils.h"

#include <QObject>
#include <map>
#include <memory>

class ProteinDB;
class Storage;

class DataHub : public QObject
{
	Q_OBJECT
public:
	explicit DataHub(QObject *parent = nullptr);

	using DataPtr = Dataset::Ptr;
	using ConstDataPtr = Dataset::ConstPtr;

	std::map<unsigned, DataPtr> datasets();

signals:
	void ioError(const QString &message, MessageType type = MessageType::CRITICAL);
	void newDataset(DataPtr data);

public slots:
	void spawn(ConstDataPtr source, const DatasetConfiguration& config, QString initialDisplay = {});
	void importDataset(const QString &filename, const QString featureCol = {});

	void saveProjectAs(const QString &filename);

public:
	ProteinDB proteins;
	Storage store;

protected:
	void setupSignals();

	DataPtr createDataset(DatasetConfiguration config);

	void runOnCurrent(const std::function<void(DataPtr)> &work);

	struct : public RWLockable {
		std::map<unsigned, DataPtr> sets;
		unsigned nextId = 1;
	} data;

	// configuration to apply when switching datasets // TODO THIS GOES TO MAINWINDOW
	struct {
		struct {
			// last-selected structure (annotations/hierarchy) id (used when dataset changes)
			unsigned annotationsId = 0; // 0 means none
			unsigned hierarchyId = 0; // 0 means none
			unsigned granularity = 0; // hierarchy parameter
		} structure;
	} guiState;
};

#endif
