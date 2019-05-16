#include "centralhub.h"

#include "proteindb.h"
#include "dataset.h"
#include "storage.h"

#include <QThread>

CentralHub::CentralHub(QObject *parent) : QObject(parent),
    proteins(new ProteinDB),
    data(new Dataset(*proteins)),
    store(new Storage(*data))
{
	setupThreads();
	setupSignals();
}

CentralHub::~CentralHub()
{
	/* cleanup worker threads */
	auto threads = findChildren<QThread*>("", Qt::FindDirectChildrenOnly);
	for (auto t : qAsConst(threads))
		t->quit();
	for (auto t : qAsConst(threads))
		t->wait();
}

void CentralHub::setupSignals()
{
	auto store = this->store.get();
	auto data = this->data.get();

	/* signals to workers */
	connect(this, &CentralHub::openDataset, store, &Storage::openDataset);
	connect(this, &CentralHub::readAnnotations, store, &Storage::readAnnotations);
	connect(this, &CentralHub::readHierarchy, store, &Storage::readHierarchy);
	connect(this, &CentralHub::importDescriptions, store, &Storage::importDescriptions);
	connect(this, &CentralHub::importAnnotations, store, &Storage::importAnnotations);
	connect(this, &CentralHub::importHierarchy, store, &Storage::importHierarchy);
	connect(this, &CentralHub::exportAnnotations, store, &Storage::exportAnnotations);
	connect(this, &CentralHub::selectDataset, data, &Dataset::select);
	connect(this, &CentralHub::spawn, data, &Dataset::spawn);
	connect(this, &CentralHub::clearClusters, data, &Dataset::clearClusters);
	connect(this, &CentralHub::calculatePartition, data, &Dataset::calculatePartition);
	connect(this, &CentralHub::runFAMS, data, &Dataset::computeFAMS);

	/* signal multiplexing */
	for (auto o : std::vector<QObject*>{data, store})
		connect(o, SIGNAL(ioError(const QString&)), this, SIGNAL(ioError(const QString&)));

	/* general signalling */
	connect(this, &CentralHub::updateColorset, data, &Dataset::updateColorset);
	connect(this, &CentralHub::updateColorset, proteins.get(), &ProteinDB::updateColorset);
}

void CentralHub::setupThreads()
{
	/* move worker objects to respective threads */
	auto dataThread = new QThread(this);
	store->moveToThread(dataThread);
	data->moveToThread(dataThread);
	dataThread->start();
}
