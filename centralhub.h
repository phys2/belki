#ifndef CENTRALHUB_H
#define CENTRALHUB_H

#include "utils.h"
#include "dataset.h" // for Dataset::OrderBy
#include "proteindb.h"
#include "storage.h"

#include <QObject>
#include <map>
#include <memory>

class ProteinDB;
class Storage;

class CentralHub : public QObject
{
	Q_OBJECT
public:
	explicit CentralHub(QObject *parent = nullptr);

	using DataPtr = Dataset::Ptr;
	using ConstDataPtr = Dataset::ConstPtr;

	QVector<QColor> colorset();

signals:
	void ioError(const QString &message);
	void newDataset(DataPtr data);

public slots:
	void setCurrent(unsigned dataset = 0); // 0 for none
	void spawn(ConstDataPtr source, const DatasetConfiguration& config, QString initialDisplay = {});
	void importDataset(const QString &filename, const QString featureCol = {});

	void computeDisplay(const QString &method);
	// todo readDisplay()

	void applyAnnotations(unsigned id);
	void applyHierarchy(unsigned id, unsigned granularity);
	void calculatePartition(unsigned granularity);
	void importAnnotations(const QString &filename);
	void exportAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void runFAMS(float k);
	void changeOrder(Dataset::OrderBy reference, bool synchronize);

	void importDescriptions(const QString &filename);

public:
	ProteinDB proteins;
	Storage store; // TODO make protected

protected:
	void setupSignals();

	DataPtr createDataset(DatasetConfiguration config);

	void runOnCurrent(const std::function<void(DataPtr)> &work);

	struct : public RWLockable {
		std::map<unsigned, DataPtr> sets;
		// we hand out ids starting from 1. So current = 0 means no dataset
		unsigned current = 0;
		unsigned nextId = 1;
	} data;

	// configuration to apply when switching datasets
	struct {
		struct {
			// last-selected structure (annotations/hierarchy) id (used when dataset changes)
			unsigned annotationsId = 0; // 0 means none
			unsigned hierarchyId = 0; // 0 means none
			unsigned granularity = 0; // hierarchy parameter
		} structure;
	} guiState;
};

#endif // CENTRALHUB_H
