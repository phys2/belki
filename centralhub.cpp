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
	//auto data = this->data

	/* signals to workers */
	/*connect(this, &CentralHub::openDataset, store, &Storage::openDataset);
	connect(this, &CentralHub::readAnnotations, store, &Storage::readAnnotations);
	connect(this, &CentralHub::readHierarchy, store, &Storage::readHierarchy);
	connect(this, &CentralHub::importDescriptions, store, &Storage::importDescriptions);
	connect(this, &CentralHub::importAnnotations, store, &Storage::importAnnotations);
	connect(this, &CentralHub::importHierarchy, store, &Storage::importHierarchy);
	connect(this, &CentralHub::exportAnnotations, store, &Storage::exportAnnotations);
	connect(this, &CentralHub::spawn, data, &Dataset::spawn);
	connect(this, &CentralHub::clearClusters, data, &Dataset::clearClusters);
	connect(this, &CentralHub::calculatePartition, data, &Dataset::calculatePartition);
	connect(this, &CentralHub::runFAMS, data, &Dataset::computeFAMS);*/

	/* signal multiplexing */
	for (auto o : std::vector<QObject*>{&proteins, &store})
		connect(o, SIGNAL(ioError(const QString&)), this, SIGNAL(ioError(const QString&)));
}

void CentralHub::setCurrent(unsigned dataset)
{
	QWriteLocker (&data.l);
	data.current = dataset;
}

void CentralHub::addDataset(DataPtr dataset)
{
	data.l.lockForWrite();
	unsigned id = data.nextId++;
	data.sets[id] = dataset;
	dataset->setId(id);
	data.l.unlock();

	emit newDataset(dataset);
}

CentralHub::DataPtr CentralHub::createDataset()
{
	auto ret = std::make_shared<Dataset>(proteins);
	ret->updateColorset(tableau20);
	connect(ret.get(), &Dataset::ioError, this, &CentralHub::ioError);
	return ret;
}

CentralHub::DataPtr CentralHub::current()
{
	QReadLocker _(&data.l);
	return (data.current ? data.sets.at(data.current) : DataPtr{});
}

void CentralHub::runOnCurrent(const std::function<void(DataPtr)> &work)
{
	QtConcurrent::run([=] {
		auto d = current();
		if (d)
			work(d);
	});
}

void CentralHub::spawn(ConstDataPtr source, const DatasetConfiguration& config, QString initialDisplay)
{
	QtConcurrent::run([=] {
		auto target = createDataset();
		target->spawn(source, config);
		addDataset(target);

		/* also compute displays expected by the user – TODO initiate in dimredtab? */
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
		auto stream = store.openDataset(filename);
		if (!stream)
			return;

		auto target = createDataset();
		bool success = target->readSource(*stream, filename, featureCol);
		if (!success)
			return;
		addDataset(target);

		/* compute intial set of displays – TODO initiate in dimredtab? */
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
