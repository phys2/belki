#ifndef CENTRALHUB_H
#define CENTRALHUB_H

#include <QObject>
#include <vector>
#include <memory>

class ProteinDB;
class Storage;
class Dataset;
class DatasetConfiguration;

class CentralHub : public QObject
{
	Q_OBJECT
public:
	explicit CentralHub(QObject *parent = nullptr);
	~CentralHub();

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	// to Dataset/Storage thread
	void selectDataset(unsigned index);
	void spawn(const DatasetConfiguration& config, QString initialDisplay = {});
	void openDataset(const QString &filename);
	void readAnnotations(const QString &name);
	void readHierarchy(const QString &name);
	void importDescriptions(const QString &filename);
	void importAnnotations(const QString &filename);
	void importHierarchy(const QString &filename);
	void exportAnnotations(const QString &filename);
	void clearClusters();
	void calculatePartition(unsigned granularity);
	void runFAMS();

	// to GUI
	void ioError(const QString &message);
	void reset(bool haveData);
	void repartition(bool withOrder);
	void reorder();
	void togglePartitions(bool show);

	// other signals
	void updateColorset(QVector<QColor> colors);

public slots:

public:
	// note: not children of us as they may be moved to other threads
	std::unique_ptr<ProteinDB> proteins;
	std::unique_ptr<Dataset> data;
	std::unique_ptr<Storage> store;

protected:
	void setupSignals();
	void setupThreads();
};

#endif // CENTRALHUB_H
