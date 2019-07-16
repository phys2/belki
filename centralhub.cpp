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

	setupSignals();
}

QVector<QColor> CentralHub::colorset()
{
	return tableau20;
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
}

CentralHub::DataPtr CentralHub::createDataset(DatasetConfiguration config)
{
	data.l.lockForWrite();
	config.id = data.nextId++; // inject id into config
	auto dataset = std::make_shared<Dataset>(proteins, config);
	dataset->moveToThread(thread()); // ensure the object does not live in threadpool!
	data.sets[config.id] = dataset;
	data.l.unlock();

	dataset->updateColorset(tableau20);
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
		name.append(f.baseName()); // hack
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

void CentralHub::clearClusters()
{
	runOnCurrent([=] (DataPtr d) { d->clearClusters(); });
}

void CentralHub::importAnnotations(const QString &filename)
{
	runOnCurrent([=] (DataPtr d) {
		/* TODO: this is a mess. better read into intermediate representation
		 * first in store, then hand over to dataset.
		 * But we will keep this until project-concept store rework. */

		auto content = store.readFile(filename);
		if (content.isNull())
			return;

		QTextStream stream(content);
		bool success = d->readAnnotations(stream);
		if (!success)
			return;

		store.importAnnotations(filename, content);
	});
}

void CentralHub::readAnnotations(const QString &name)
{
	runOnCurrent([=] (DataPtr d) {
		auto stream = store.readAnnotations(name);
		if (!stream)
			return;

		d->readAnnotations(*stream);
	});
}

void CentralHub::exportAnnotations(const QString &filename)
{
	runOnCurrent([=] (DataPtr d) { store.exportAnnotations(filename, d); });
}

void CentralHub::importHierarchy(const QString &filename)
{
	runOnCurrent([=] (DataPtr d) {
		/* TODO: this is a mess. better read into intermediate representation
		 * first in store, then hand over to dataset.
		 * But we will keep this until project-concept store rework. */

		auto content = store.readFile(filename);
		if (content.isNull())
			return;

		auto json = QJsonDocument::fromJson(content);
		bool success = d->readHierarchy(json.object());
		if (!success)
			return;

		store.importHierarchy(filename, content);
	});
}

void CentralHub::readHierarchy(const QString &name)
{
	runOnCurrent([=] (DataPtr d) {
		auto json = store.readHierarchy(name);
		if (!json)
			return;

		d->readHierarchy(*json);
	});
}

void CentralHub::calculatePartition(unsigned granularity)
{
	runOnCurrent([=] (DataPtr d) { d->calculatePartition(granularity); });
}

void CentralHub::runFAMS()
{
	runOnCurrent([=] (DataPtr d) { d->computeFAMS(); });
}

void CentralHub::changeOrder(Dataset::OrderBy reference, bool synchronize)
{
	runOnCurrent([=] (DataPtr d) { d->changeOrder(reference, synchronize); });
}

void CentralHub::importDescriptions(const QString &filename)
{
	QtConcurrent::run([=] { store.importDescriptions(filename); });
}
