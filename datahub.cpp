#include "datahub.h"

#include "proteindb.h"
#include "dataset.h"
#include "storage/storage.h"

#include <QtConcurrent>
#include <QThread>
#include <QVector>

DataHub::DataHub(QObject *parent)
    : QObject(parent),
      storage(std::make_unique<Storage>(proteins))
{
	setupSignals();
}

DataHub::~DataHub() {} // needed here for unique_ptr cleanup with complete type

DataHub::Project DataHub::projectMeta()
{
	QReadLocker _(&data.l);
	return data.project;
}

std::map<unsigned, DataHub::DataPtr> DataHub::datasets()
{
	QReadLocker _(&data.l);
	return data.sets; // return a current copy
}

void DataHub::setupSignals()
{
	connect(storage.get(), &Storage::nameChanged, this, &DataHub::updateProjectName);

	/* signal pass-through */
	connect(&proteins, &ProteinDB::ioError, this, &DataHub::ioError);
	connect(storage.get(), &Storage::ioError, this, &DataHub::ioError);
}

void DataHub::init(std::vector<DataPtr> datasets)
{
	data.l.lockForWrite();
	if (data.nextId != 1)
		throw std::runtime_error("DataHub::init() called on non-empty object");

	for (auto &dataset : datasets) {
		// ensure the object does not live in threadpool (creating thread)!
		dataset->moveToThread(thread());
		data.sets[dataset->id()] = dataset;
		data.nextId = std::max(data.nextId, dataset->id() + 1);
	}
	data.l.unlock();

	/* emit sorted by id to ensure parents are available
	   there is no guarantee that everybody who writes .belki files sorts them */
	std::sort(datasets.begin(), datasets.end(), [] (const DataPtr &a, const DataPtr &b) {
		return a->id() < b->id();
	});
	for (auto &dataset : datasets)
		emit newDataset(dataset);
}

DataHub::DataPtr DataHub::createDataset(DatasetConfiguration config)
{
	data.l.lockForWrite();
	config.id = data.nextId++; // inject id into config
	auto dataset = std::make_shared<Dataset>(proteins, config);
	// ensure the object does not live in threadpool (creating thread)!
	dataset->moveToThread(thread());
	data.sets[config.id] = dataset;
	data.l.unlock();

	return dataset;
}

void DataHub::updateProjectName(const QString &name, const QString &path)
{
	data.l.lockForWrite();
	data.project.name = name;
	data.project.path = path;
	data.l.unlock();
	emit projectNameChanged(name, path);
}

void DataHub::spawn(ConstDataPtr source, const DatasetConfiguration& config, QString initialDisplay)
{
	QtConcurrent::run([=] {
		auto target = createDataset(config);
		target->spawn(source);

		emit newDataset(target);

		/* also compute displays expected by the user – TODO initiate in dimredtab */
		if (target->peek<Dataset::Base>()->dimensions.size() < 3)
			return;

		target->computeDisplays(); // standard set

		// current display
		if (!initialDisplay.isEmpty())
			return;

		if (!target->peek<Dataset::Representations>()->displays.count(initialDisplay))
			target->computeDisplay(initialDisplay);
	});
}

void DataHub::importDataset(const QString &filename, const QString featureCol)
{
	QtConcurrent::run([=] {
		auto dataset = storage->openDataset(filename, featureCol);
		if (!dataset)
			return;

		/* setup a nice name */
		QFileInfo f(filename);
		auto path = f.canonicalPath().split(QDir::separator());
		QString name;
		if (path.size() > 1)
			name.append(*(++path.rbegin()) + "/");
		if (path.size() > 0)
			name.append(path.back() + "/");
		name.append(f.completeBaseName()); // hack
		if (!featureCol.isEmpty() && featureCol != "Dist")
			name += " " + featureCol;
		DatasetConfiguration config;
		config.name = name;

		auto target = createDataset(config);
		target->spawn(std::move(dataset));

		emit newDataset(target);

		/* compute intial set of displays – TODO initiate in dimredtab */
		if (target->peek<Dataset::Base>()->dimensions.size() < 3)
			return;
		target->computeDisplays();
	});
}

void DataHub::openProject(const QString &filename)
{
	auto datasets = storage->openProject(filename); // manipulates ProteinDB
	init(datasets);
}

void DataHub::saveProject(QString filename)
{
	bool newName = !filename.isEmpty();
	if (!newName) {
		data.l.lockForRead();
		filename = data.project.path;
		data.l.unlock();
		if (filename.isEmpty()) // should not happen
			return ioError({"Could not save project!", "No filename specified."});
	}

	QtConcurrent::run([=] {
		QReadLocker _(&data.l);
		std::vector<Dataset::ConstPtr> snapshot;
		for (auto &[k, v] : data.sets)
			snapshot.push_back(v);
		storage->saveProject(filename, snapshot);
	});
}
