#include "datahub.h"

#include "proteindb.h"
#include "dataset.h"
#include "storage.h"

#include <QtConcurrent>
#include <QThread>
#include <QVector>

DataHub::DataHub(QObject *parent)
    : QObject(parent),
      store(proteins)
{
	setupSignals();
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
