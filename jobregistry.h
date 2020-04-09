#ifndef JOBREGISTRY_H
#define JOBREGISTRY_H

#include "utils.h"

#include <QReadWriteLock>
#include <QString>
#include <QPointer>
#include <unordered_map>
#include <memory>

class QThread;

struct Task {
	enum class Type {
		GENERIC,
		COMPUTE,
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

	std::function<void ()> fun;
	Type type = Type::GENERIC;
	std::vector<QString> fields = {};
};

class JobRegistry : public NonCopyable
{
public:
	struct Entry {
		bool isValid() const { return id; }

		unsigned id = 0; // empty job
		QString name;
	};

	static std::shared_ptr<JobRegistry> get(); // singleton
	static void run(const Task &task, const std::vector<QPointer<QObject>> &listeners);
	static void pipeline(const std::vector<Task> &tasks, const std::vector<QPointer<QObject>> &listeners);

	Entry job(unsigned id);
	Entry getCurrentJob();

	void startCurrentJob(Task::Type type, const std::vector<QString> &fields);
	void addCurrentJobListener(QPointer<QObject> listener);
	void endCurrentJob();

protected:
	using JobMap = std::unordered_map<QThread*, Entry>;

	JobMap::iterator threadToEntry();
	void createEntry(Task::Type type, const std::vector<QString> &fields);
	void eraseEntry(JobMap::iterator entry);

	unsigned nextJobId = 1; // 0 is no job
	JobMap jobs;
	std::unordered_multimap<unsigned, QPointer<QObject>> listeners;
	QReadWriteLock lock{QReadWriteLock::RecursionMode::Recursive};
};

#endif // JOBREGISTRY_H
