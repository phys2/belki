#include "jobregistry.h"

#include <QThread>
#include <QMetaObject>
#include <QtConcurrent>

std::shared_ptr<JobRegistry> JobRegistry::get()
{
	static auto instance = std::make_shared<JobRegistry>();
	return instance;
}

void JobRegistry::run(const Task &task, const std::vector<QPointer<QObject>> &monitors)
{
	pipeline({task}, monitors);
}

void JobRegistry::pipeline(const std::vector<Task> &tasks,
                           const std::vector<QPointer<QObject>> &monitors)
{
	QtConcurrent::run([tasks,monitors] {
		auto reg = JobRegistry::get();
		for (auto &task : tasks) {
			reg->startCurrentJob(task.type, task.fields, task.userData);
			for (auto i : monitors)
				reg->addCurrentJobMonitor(i);
			task.fun();
			reg->endCurrentJob();
		}
	});
}

JobRegistry::Entry JobRegistry::job(unsigned id)
{
	QReadLocker _(&lock);
	auto it = idToEntry(id);
	return (it != jobs.end() ? it->second : Entry{});
}

void JobRegistry::cancelJob(unsigned id)
{
	QWriteLocker _(&lock);
	auto it = idToEntry(id);
	if (it != jobs.end()) {
		it->second.isCancelled = true;
		notifyMonitors(id, "updateJob");
	}
}

void JobRegistry::setJobProgress(unsigned id, float progress)
{
	QWriteLocker _(&lock);
	auto it = idToEntry(id);
	if (it == jobs.end())
		return; // TODO complain
	updateProgress(it, progress);
}

JobRegistry::Entry JobRegistry::getCurrentJob()
{
	QReadLocker _(&lock);
	auto it = threadToEntry();
	return (it != jobs.end() ? it->second : Entry{});
}

bool JobRegistry::isCurrentJobCancelled()
{
	QReadLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end())
		return it->second.isCancelled;
	// TODO complain else
	return false;
}

void JobRegistry::startCurrentJob(Task::Type type, const std::vector<QString> &fields,
                                  const QVariant &userData)
{
	QWriteLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end()) {
		// TODO: this should not happen, so complain
		erase(it);
	}
	createEntry(type, fields, userData);
}

void JobRegistry::addCurrentJobMonitor(QPointer<QObject> monitor)
{
	if (!monitor)
		return;
	QWriteLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end()) {
		monitors.insert({it->second.id, monitor});
		// let them know we exist
		QMetaObject::invokeMethod(monitor, "addJob", Qt::ConnectionType::QueuedConnection,
		                          Q_ARG(unsigned, it->second.id));
	}
	// TODO complain else
}

void JobRegistry::setCurrentJobProgress(float progress)
{
	QWriteLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end())
		updateProgress(it, progress);
	// TODO complain else
}

void JobRegistry::endCurrentJob()
{
	QWriteLocker _(&lock);
	auto it = threadToEntry();
	if (it != jobs.end())
		erase(it);
	// TODO complain else
}

JobRegistry::JobMap::iterator JobRegistry::idToEntry(unsigned id)
{
	// caller needs to hold lock
	return std::find_if(jobs.begin(), jobs.end(),
	                    [id] (const auto &i) { return i.second.id == id; });
}

JobRegistry::JobMap::iterator JobRegistry::threadToEntry()
{
	// caller needs to hold lock
	return jobs.find(QThread::currentThread());
}

void JobRegistry::createEntry(Task::Type type, const std::vector<QString> &fields,
                              const QVariant &userData)
{
	// caller needs to hold lock
	using T = Task::Type;
	static const std::map<T, QString> names = {
	    {T::GENERIC, "Background computation running"},
	    {T::COMPUTE, "Computing %1 on %2"},
	    {T::COMPUTE_FAMS, "Computing Mean Shift with k=%1 on %2"},
	    {T::COMPUTE_HIERARCHY, "Computing hierarchy on %1"},
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
	jobs[QThread::currentThread()] = {id, name, userData};
}

void JobRegistry::erase(JobMap::iterator entry)
{
	// caller needs to hold lock
	auto jobId = entry->second.id;
	jobs.erase(entry);

	// notify monitors after the fact. avoid race condition if they call job()
	notifyMonitors(jobId, "removeJob");
	monitors.erase(jobId);
}

void JobRegistry::updateProgress(JobMap::iterator entry, float progress)
{
	// caller needs to hold lock
	entry->second.progress = progress;
	notifyMonitors(entry->second.id, "updateJob");
}

void JobRegistry::notifyMonitors(unsigned jobId, const char *signal)
{
	auto range = monitors.equal_range(jobId);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second) {
			QMetaObject::invokeMethod(it->second, signal, Qt::ConnectionType::QueuedConnection,
			                          Q_ARG(unsigned, jobId));
		}
	}
}
