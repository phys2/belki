#include "centralhub.h"

#include "proteindb.h"
#include "dataset.h"
#include "storage.h"

#include <QtConcurrent>
#include <QThread>
#include <QVector>
#include <QColor>

const QVector<QColor> tableau20 = {
    {31, 119, 180}, {174, 199, 232}, {255, 127, 14}, {255, 187, 120},
    {44, 160, 44}, {152, 223, 138}, {214, 39, 40}, {255, 152, 150},
    {148, 103, 189}, {197, 176, 213}, {140, 86, 75}, {196, 156, 148},
    {227, 119, 194}, {247, 182, 210}, {127, 127, 127}, {199, 199, 199},
    {188, 189, 34}, {219, 219, 141}, {23, 190, 207}, {158, 218, 229}
};

CentralHub::CentralHub(QObject *parent)
    : QObject(parent),
      store(proteins)
{
	proteins.updateColorset(tableau20);
	store.updateColorset(tableau20);

	setupSignals();
}

QVector<QColor> CentralHub::colorset()
{
	return tableau20;
}

std::map<unsigned, CentralHub::DataPtr> CentralHub::datasets()
{
	QReadLocker _(&data.l);
	return data.sets; // return a current copy
}

void CentralHub::setupSignals()
{
	/* signal multiplexing */
	for (auto o : std::vector<QObject*>{&proteins, &store})
		connect(o, SIGNAL(ioError(const QString&)), this, SIGNAL(ioError(const QString&)));
}

void CentralHub::setCurrent(unsigned dataset)
{
	data.l.lockForWrite();
	data.current = dataset;
	data.l.unlock();

	if (guiState.structure.hierarchyId)
		applyHierarchy(guiState.structure.hierarchyId, guiState.structure.granularity);
	else
		applyAnnotations(guiState.structure.annotationsId);
}

CentralHub::DataPtr CentralHub::createDataset(DatasetConfiguration config)
{
	data.l.lockForWrite();
	config.id = data.nextId++; // inject id into config
	auto dataset = std::make_shared<Dataset>(proteins, config);
	dataset->moveToThread(thread()); // ensure the object does not live in threadpool!
	data.sets[config.id] = dataset;
	data.l.unlock();

	connect(dataset.get(), &Dataset::ioError, this, &CentralHub::ioError);
	return dataset;
}

void CentralHub::runOnCurrent(const std::function<void(DataPtr)> &work)
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

void CentralHub::spawn(ConstDataPtr source, const DatasetConfiguration& config, QString initialDisplay)
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

void CentralHub::importDataset(const QString &filename, const QString featureCol)
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

void CentralHub::computeDisplay(const QString &method)
{
	runOnCurrent([=] (DataPtr d) { d->computeDisplay(method); });
}

void CentralHub::applyAnnotations(unsigned id)
{
	guiState.structure.annotationsId = id;
	runOnCurrent([=] (DataPtr d) {
		d->applyAnnotations(id);
	});
}

void CentralHub::exportAnnotations(const QString &filename)
{
	runOnCurrent([=] (DataPtr d) { store.exportAnnotations(filename, d); });
}

void CentralHub::applyHierarchy(unsigned id, unsigned granularity)
{
	guiState.structure.hierarchyId = id;
	guiState.structure.annotationsId = 0;
	runOnCurrent([=] (DataPtr d) {
		d->applyHierarchy(id, granularity);
	});
}

void CentralHub::createPartition(unsigned granularity)
{
	guiState.structure.granularity = granularity;
	runOnCurrent([=] (DataPtr d) { d->createPartition(granularity); });
}

void CentralHub::runFAMS(float k)
{
	runOnCurrent([=] (DataPtr d) { d->computeFAMS(k); });
}

void CentralHub::changeOrder(Dataset::OrderBy reference, bool synchronize)
{
	runOnCurrent([=] (DataPtr d) { d->changeOrder(reference, synchronize); });
}

void CentralHub::importAnnotations(const QString &filename)
{
	QtConcurrent::run([=] { store.importAnnotations(filename); });
}

void CentralHub::importHierarchy(const QString &filename)
{
	QtConcurrent::run([=] { store.importHierarchy(filename); });
}

void CentralHub::importDescriptions(const QString &filename)
{
	QtConcurrent::run([=] { store.importDescriptions(filename); });
}
