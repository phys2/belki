#ifndef JOBREGISTRY_H
#define JOBREGISTRY_H

#include "utils.h"

#include <QReadWriteLock>
#include <QString>
#include <QPointer>
#include <QVariant>
#include <unordered_map>
#include <memory>

class QThread;

/**
 * @brief A background task description
 * This struct is used to annotate a function to be run in the background with its type and
 * additional textual information. Use it when calling JobRegistry::run(), JobRegistry::pipeline().
 */
struct Task {
	enum class Type {
		GENERIC,
		COMPUTE,
		COMPUTE_FAMS,
		PARTITION_HIERARCHY,
		ANNOTATE,
		ORDER,
		SPAWN,
		IMPORT_DATASET,
		IMPORT_DESCRIPTIONS,
		IMPORT_MARKERS,
		EXPORT_MARKERS,
		IMPORT_HIERARCHY,
		IMPORT_ANNOTATIONS,
		EXPORT_ANNOTATIONS,
		PERSIST_ANNOTATIONS,
		LOAD,
		SAVE,
	};

	std::function<void()> fun;
	Type type = Type::GENERIC;
	std::vector<QString> fields = {};
	QVariant userData = {};
};

/**
 * @brief A simple registry for background job monitoring with convenience methods for job control
 * The registry identifies jobs based on their thread id; as one thread only runs one job at a time.
 * When running a job, call startCurrentJob() before starting computation and endCurrentJob() after,
 * in the same thread as the job is running.
 *
 * This is a singleton so it can be accessible from everywhere. It is application-global just like
 * threads are.
 *
 * Progress updates and cancellation mechanics will be added later, and they rely on the caller to
 * call from the respective thread or provide the correct job id that was obtained by the respective
 * thread's call to getCurrentJob() after startCurrentJob().
 *
 * Monitors are QObjects with slots addJob(unsigned), updateJob(unsigned), and removeJob(unsigned).
 * These methods are invoked so they will run in the QObject's thread. A monitor need not to
 * survive until the job ends, due to QPointer mechanics.
 *
 * The methods run() and pipeline() use QtConcurrent to run a function (or several) in
 * the background as well as registering it/them with us, and adding any monitors.
 * The pipeline is stupid; it just runs one job after another in the same thread.
 */
class JobRegistry : public NonCopyable
{
public:
	struct Entry {
		bool isValid() const { return id; }

		unsigned id = 0; // empty job
		QString name;
		QVariant userData;
	};

	static std::shared_ptr<JobRegistry> get(); // singleton
	static void run(const Task &task, const std::vector<QPointer<QObject>> &monitors);
	static void pipeline(const std::vector<Task> &tasks,
	                     const std::vector<QPointer<QObject>> &monitors);

	Entry job(unsigned id);
	Entry getCurrentJob();

	void startCurrentJob(Task::Type type, const std::vector<QString> &fields,
	                     const QVariant &userData = {});
	void addCurrentJobMonitor(QPointer<QObject> monitor);
	void endCurrentJob();

protected:
	using JobMap = std::unordered_map<QThread*, Entry>;

	JobMap::iterator threadToEntry();
	void createEntry(Task::Type type, const std::vector<QString> &fields, const QVariant &userData);
	void eraseEntry(JobMap::iterator entry);

	unsigned nextJobId = 1; // 0 is no job
	JobMap jobs;
	std::unordered_multimap<unsigned, QPointer<QObject>> monitors;
	QReadWriteLock lock{QReadWriteLock::RecursionMode::Recursive};
};

#endif // JOBREGISTRY_H
