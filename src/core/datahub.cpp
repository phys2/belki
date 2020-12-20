#include "datahub.h"

#include "proteindb.h"
#include "dataset.h"
#include "../storage/storage.h"

#include <QThread>
#include <QVector>
#include <QFileInfo>
#include <QDir>

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
	connect(&proteins, &ProteinDB::message, this, &DataHub::message);
	connect(storage.get(), &Storage::message, this, &DataHub::message);
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
	QWriteLocker _(&data.l);
	// do not accept unknown parents which would lead to stale backreference
	if (config.parent && !data.sets.count(config.parent)) {
		emit message({"Could not create new dataset.", "The parent dataset is missing."});
		return {};
	}

	config.id = data.nextId++; // inject id into config
	auto dataset = std::make_shared<Dataset>(proteins, config);
	// ensure the object does not live in threadpool (creating thread)!
	dataset->moveToThread(thread());
	data.sets[config.id] = dataset;

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

void DataHub::spawn(ConstDataPtr source, const DatasetConfiguration& config)
{
	auto target = createDataset(config);
	if (!target)
		return;
	target->spawn(source);

	emit newDataset(target);

	/* also compute displays expected by the user – TODO initiate in dimredtab */
	if (target->peek<Dataset::Base>()->dimensions.size() < 3)
		return;
	// standard set PCA
	target->computeDisplay("PCA");
	// current display TODO dead code
	/*if (!initialDisplay.isEmpty() &&
		!target->peek<Dataset::Representations>()->displays.count(initialDisplay))
		target->computeDisplay(initialDisplay);*/
}

void DataHub::importDataset(const QString &filename, const QString featureCol)
{
	// TODO: using feature column name as normalize decision is a hack
	Storage::ReadConfig readCfg{featureCol, featureCol.isEmpty() || featureCol == "Dist"};
	auto dataset = storage->openDataset(filename, readCfg);
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
	config.normalized = readCfg.normalize;

	auto target = createDataset(config);
	target->spawn(std::move(dataset));

	emit newDataset(target);

	/* compute intial set of displays – TODO initiate in dimredtab */
	if (target->peek<Dataset::Base>()->dimensions.size() < 3)
		return;
	target->computeDisplay("PCA");
}

void DataHub::removeDataset(unsigned id)
{
	std::set<unsigned> removals;
	data.l.lockForWrite();
	/* recursively erase them */
	for (auto& [k, v] : data.sets) {
		/* we rely on the fact that keys are ordered and parent ids are always
		   lower, so parents come before their children */
		if (k == id || removals.count(v->config().parent))
			removals.insert(k);
	}
	for (auto i : removals)
		data.sets.erase(i);
	data.l.unlock();
	/* Emit in bottom-up order. Otherwise some GUI code may crash.
	   Qt models really hate if you delete an item that has children. */
	for (auto it = removals.rbegin(); it != removals.rend(); ++it)
		emit datasetRemoved(*it);
}

void DataHub::openProject(const QString &filename)
{
	auto datasets = storage->openProject(filename); // manipulates ProteinDB
	init(datasets);
}

bool DataHub::saveProject(QString filename)
{
	QReadLocker l(&data.l);
	if (filename.isEmpty()) {
		filename = data.project.path;
		if (filename.isEmpty()) { // should not happen
			message({"Could not save project!", "No filename specified."});
			return false;
		}
	}
	std::vector<Dataset::ConstPtr> snapshot;
	for (auto &[k, v] : data.sets)
		snapshot.push_back(v);
	l.unlock();

	return storage->saveProject(filename, snapshot); // might lock for write to update filename
}
