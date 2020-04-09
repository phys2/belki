#include "jobregistry.h"

#include <QThread>
#include <QMetaObject>
#include <QtConcurrent>

#include <iostream>

std::shared_ptr<JobRegistry> JobRegistry::get()
{
	static auto instance = std::make_shared<JobRegistry>();
	return instance;
}

void JobRegistry::run(const Task &task, const std::vector<QPointer<QObject>> &listeners)
{
	pipeline({task}, listeners);
}

void JobRegistry::pipeline(const std::vector<Task> &tasks,
                           const std::vector<QPointer<QObject>> &listeners)
{
	QtConcurrent::run([tasks,listeners] {
		auto reg = JobRegistry::get();
		for (auto &task : tasks) {
			reg->startCurrentJob(task.type, task.fields);
			for (auto i : listeners)
				reg->addCurrentJobListener(i);
			task.fun();
			reg->endCurrentJob();
		}
	});
}

JobRegistry::Entry JobRegistry::job(unsigned id)
{
	QReadLocker _(&lock);
	for (auto &[k, v] : jobs) {
		if (v.id == id)
			return v;
	}
	return {};
}

JobRegistry::Entry JobRegistry::getCurrentJob()
{
	QReadLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end())
		return it->second;
	return {};
}

void JobRegistry::startCurrentJob(Task::Type type, const std::vector<QString> &fields)
{
	QWriteLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end()) {
		// TODO: this should not happen, so complain
		eraseEntry(it);
	}
	createEntry(type, fields);
}

void JobRegistry::addCurrentJobListener(QPointer<QObject> listener)
{
	if (!listener)
		return;
	QWriteLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end()) {
		listeners.insert({it->second.id, listener});
		// let them know we exist
		QMetaObject::invokeMethod(listener, "addJob", Qt::ConnectionType::QueuedConnection,
		                          Q_ARG(unsigned, it->second.id));
	}
	// TODO complain else
}

void JobRegistry::endCurrentJob()
{
	QWriteLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end())
		eraseEntry(it);
	// TODO complain else
}

JobRegistry::JobMap::iterator JobRegistry::threadToEntry()
{
	// caller needs to hold lock
	return jobs.find(QThread::currentThread());
}

void JobRegistry::createEntry(Task::Type type, const std::vector<QString> &fields)
{
	// caller needs to hold lock
	using T = Task::Type;
	static const std::map<T, QString> names = {
	    {T::GENERIC, "Background computation running"},
	    {T::COMPUTE, "Computing %1 on %2"},
	    {T::PARTITION_HIERARCHY, "Partitioning %1 on %2"},
	    {T::ORDER, "Ordering %2 based on %1"},
	    {T::ANNOTATE, "Annotating %2 with %1"},
	    {T::COMPUTE, "Computing %1 for %2"},
	    {T::IMPORT_DATASET, "Importing dataset %1"},
	    {T::IMPORT_DESCRIPTIONS, "Importing protein descriptions from %1"},
	    {T::IMPORT_MARKERS, "Importing markers from %1"},
	    {T::EXPORT_MARKERS, "Exporting markers to %1"},
	    {T::IMPORT_HIERARCHY, "Importing hierarchy %1"},
	    {T::IMPORT_ANNOTATIONS, "Importing annotations %1"},
	    {T::EXPORT_ANNOTATIONS, "Exporting %2 to %1"},
	    {T::PERSIST_ANNOTATIONS, "Persisting annotations %1"},
	    {T::SPAWN, "Splicing new dataset %1"},
	    {T::LOAD, "Opening project %1"},
	    {T::SAVE, "Saving project"},
	};

	auto id = nextJobId++;
	auto name = names.at(type);
	for (auto i : fields)
		name = name.arg(i);
	// TODO: check for nullptr & complain
	jobs[QThread::currentThread()] = {id, name};

	std::cerr << "Job created: " << id << "\t" << name.toStdString() << std::endl;
}

void JobRegistry::eraseEntry(JobMap::iterator entry)
{
	std::cerr << "Job finished: " << entry->second.id << "\t" << entry->second.name.toStdString() << std::endl;

	// caller needs to hold lock
	auto jobId = entry->second.id;
	auto range = listeners.equal_range(jobId);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second) {
			QMetaObject::invokeMethod(it->second, "removeJob", Qt::ConnectionType::QueuedConnection,
			                          Q_ARG(unsigned, jobId));
		}
	}
	listeners.erase(jobId);
	jobs.erase(entry);
}
