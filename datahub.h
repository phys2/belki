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
	struct Project {
		QString name;
		QString path;
	};

	using DataPtr = Dataset::Ptr;
	using ConstDataPtr = Dataset::ConstPtr;

	explicit DataHub(QObject *parent = nullptr);
	~DataHub();

	Project projectMeta();
	Storage *store() { return storage.get(); };
	std::map<unsigned, DataPtr> datasets();

signals:
	void projectNameChanged(const QString &name, const QString &path);
	void message(const GuiMessage &message);
	void newDataset(DataPtr data);

public slots:
	void updateProjectName(const QString &name, const QString &path);
	void spawn(ConstDataPtr source, const DatasetConfiguration& config);
	void importDataset(const QString &filename, const QString featureCol = {});
	void openProject(const QString &filename);
	void saveProject(QString filename = {});

public:
	ProteinDB proteins;
	std::unique_ptr<Storage> storage;

protected:
	void setupSignals();
	void init(std::vector<DataPtr> datasets);

	DataPtr createDataset(DatasetConfiguration config);

	void runOnCurrent(const std::function<void(DataPtr)> &work);

	struct : public RWLockable {
		Project project;
		std::map<unsigned, DataPtr> sets;
		unsigned nextId = 1;
	} data;
};

#endif
