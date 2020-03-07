#ifndef DATAHUB_H
#define DATAHUB_H

#include "dataset.h" // for DatasetConfiguration
#include "proteindb.h"
#include "utils.h"

#include <QObject>
#include <map>
#include <memory>

class Storage;

class DataHub : public QObject
{
	Q_OBJECT
public:
	using DataPtr = Dataset::Ptr;
	using ConstDataPtr = Dataset::ConstPtr;

	explicit DataHub(QObject *parent = nullptr);
	~DataHub();

	Storage *store() { return storage.get(); };
	std::map<unsigned, DataPtr> datasets();

signals:
	void ioError(const QString &message, MessageType type = MessageType::CRITICAL);
	void newDataset(DataPtr data);

public slots:
	void spawn(ConstDataPtr source, const DatasetConfiguration& config, QString initialDisplay = {});
	void importDataset(const QString &filename, const QString featureCol = {});
	void openProject(const QString &filename);
	void saveProjectAs(const QString &filename);

public:
	ProteinDB proteins;
	std::unique_ptr<Storage> storage;

protected:
	void setupSignals();
	void init(std::vector<DataPtr> datasets);

	DataPtr createDataset(DatasetConfiguration config);

	void runOnCurrent(const std::function<void(DataPtr)> &work);

	struct : public RWLockable {
		std::map<unsigned, DataPtr> sets;
		unsigned nextId = 1;
	} data;
};

#endif
