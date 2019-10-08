#include "datahub.h"

#include "proteindb.h"
#include "dataset.h"
#include "storage.h"

#include <compute/colors.h>

#include <QtConcurrent>
#include <QThread>
#include <QVector>
#include <QColor>

DataHub::DataHub(QObject *parent)
    : QObject(parent),
      store(proteins)
{
	proteins.updateColorset(colorset());
	store.updateColorset(colorset());

	setupSignals();
}

QVector<QColor> DataHub::colorset()
{
	//return Palette::tableau20;
	return Palette::iwanthue20;
}

std::map<unsigned, DataHub::DataPtr> DataHub::datasets()
{
	QReadLocker _(&data.l);
	return data.sets; // return a current copy
}

void DataHub::setupSignals()
{
	/* signal multiplexing */
	for (auto o : std::vector<QObject*>{&proteins, &store})
		connect(o, SIGNAL(ioError(const QString&, MessageType)),
		        this, SIGNAL(ioError(const QString&, MessageType)));
}

void DataHub::setCurrent(unsigned dataset)
{
	data.l.lockForWrite();
	data.current = dataset;
	data.l.unlock();

	if (guiState.structure.hierarchyId)
		applyHierarchy(guiState.structure.hierarchyId, guiState.structure.granularity);
	else
		applyAnnotations(guiState.structure.annotationsId);
}

DataHub::DataPtr DataHub::createDataset(DatasetConfiguration config)
{
	data.l.lockForWrite();
	config.id = data.nextId++; // inject id into config
	auto dataset = std::make_shared<Dataset>(proteins, config);
	dataset->moveToThread(thread()); // ensure the object does not live in threadpool!
	data.sets[config.id] = dataset;
	data.l.unlock();

	return dataset;
}

void DataHub::runOnCurrent(const std::function<void(DataPtr)> &work)
{
	QtConcurrent::run([=] {
		QReadLocker _(&data.l); // RAII
		if (!data.current)
			return;
		auto target = data.sets.at(data.current);

		/* Target is a shared_ptr and can be used without the container lock.
		 * Dataset does its own locking. It is important to unlock early here,
		 * so that long computations do not affect the ability to switch current
		 * dataset. */
		_.unlock();
		work(target);
	});
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

		if (!target->peek<Dataset::Representation>()->display.count(initialDisplay))
			target->computeDisplay(initialDisplay);
	});
}

void DataHub::importDataset(const QString &filename, const QString featureCol)
{
	QtConcurrent::run([=] {
		auto dataset = store.openDataset(filename, featureCol);
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

void DataHub::computeDisplay(const QString &method)
{
	runOnCurrent([=] (DataPtr d) { d->computeDisplay(method); });
}

void DataHub::applyAnnotations(unsigned id)
{
	guiState.structure.annotationsId = id;
	runOnCurrent([=] (DataPtr d) {
		d->applyAnnotations(id);
	});
}

void DataHub::exportAnnotations(const QString &filename)
{
	runOnCurrent([=] (DataPtr d) { store.exportAnnotations(filename, d); });
}

void DataHub::applyHierarchy(unsigned id, unsigned granularity)
{
	guiState.structure.hierarchyId = id;
	guiState.structure.annotationsId = 0;
	runOnCurrent([=] (DataPtr d) {
		d->applyHierarchy(id, granularity);
	});
}

void DataHub::createPartition(unsigned granularity)
{
	guiState.structure.granularity = granularity;
	runOnCurrent([=] (DataPtr d) { d->createPartition(granularity); });
}

void DataHub::runFAMS(float k)
{
	runOnCurrent([=] (DataPtr d) { d->computeFAMS(k); });
}

void DataHub::changeOrder(Dataset::OrderBy reference, bool synchronize)
{
	runOnCurrent([=] (DataPtr d) { d->changeOrder(reference, synchronize); });
}

void DataHub::importAnnotations(const QString &filename)
{
	QtConcurrent::run([=] { store.importAnnotations(filename); });
}

void DataHub::importHierarchy(const QString &filename)
{
	QtConcurrent::run([=] { store.importHierarchy(filename); });
}

void DataHub::importDescriptions(const QString &filename)
{
	QtConcurrent::run([=] { store.importDescriptions(filename); });
}

void DataHub::saveProjectAs(const QString &filename)
{
	QtConcurrent::run([=] {
		QReadLocker _(&data.l);
		std::vector<Dataset::ConstPtr> snapshot;
		for (auto &[k, v] : data.sets)
			snapshot.push_back(v);
		store.saveProjectAs(filename, snapshot);
	});
}
