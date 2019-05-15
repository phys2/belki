#ifndef DATASET_H
#define DATASET_H

#include "utils.h"
#include "proteindb.h"
#include "compute/features.h"
#include "meanshift/fams.h"

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include <QList>
#include <QPointF>
#include <QColor>
#include <QTextStream>
#include <QByteArray>

#include <set>
#include <map>
#include <unordered_map>
#include <memory>

class Dataset : public QObject
{
	Q_OBJECT

	friend class Storage; // NOTE: ensure that Storage object resides in same thread!

public:
	enum class OrderBy {
		FILE,
		NAME,
		HIERARCHY,
		CLUSTERING
	};
	Q_ENUM(OrderBy)

	struct Cluster {
		QString name;
		QColor color;
		unsigned size = 0;
		// mode of the cluster, if not available, centroid
		std::vector<double> mode;
	};

	struct Clustering {
		explicit Clustering(size_t numProteins = 0) : memberships(numProteins) {}
		bool empty() const { return clusters.empty(); }

		// cluster definitions
		std::unordered_map<unsigned, Cluster> clusters;
		// order of clusters (based on size/name/etc)
		std::vector<unsigned> order;
		// cluster memberships of each protein
		std::vector<std::set<unsigned>> memberships;
	};

	struct HrCluster {
		double distance;
		int protein;
		unsigned parent;
		std::vector<unsigned> children;
	};

	struct Order {
		OrderBy reference = OrderBy::HIERARCHY;
		bool synchronizing = true; // re-calculate whenever the source changes
		bool fallback = true; // enable one-off synchronization

		std::vector<unsigned> index; // protein indices ordered
		std::vector<unsigned> rankOf; // position of each protein in the order
	};

	// a configuration that describes processing resulting in a dataset
	struct Configuration {
		QString name; // user-specified identifier

		int parent = -1; // index of the dataset this one was spawned from
		std::vector<unsigned> bands; // the feature bands that were kept
		double scoreThresh = 0.; // score cutoff that was applied
	};

	struct Public {
		bool hasScores() const { return !scores.empty(); }
		const auto& lookup(View<ProteinDB::Public> &v, unsigned index) const {
			return v->proteins[protIds[index]];
		}

		QStringList dimensions;

		// meta information for this dataset
		Configuration conf;

		// from protein in vectors (1:1 index) to db index
		std::vector<ProteinId> protIds;
		// from protein db to index in vectors
		std::unordered_map<ProteinId, unsigned> protIndex;

		// original data
		features::vec features;
		features::Range featureRange;
		// pre-cached set of points
		std::vector<QVector<QPointF>> featurePoints;
		// measurement scores
		features::vec scores;
		features::Range scoreRange;

		// feature reduced point sets
		std::map<QString, QVector<QPointF>> display;

		// clusters / hierarchy, if available
		Clustering clustering;
		std::vector<HrCluster> hierarchy;

		// order of proteins
		// determined by hierarchy or clusters (if available), pos. in file, or name
		Order order;
	};

	using View = ::View<Public>;

	Dataset(ProteinDB &proteins);

	static const std::map<Dataset::OrderBy, QString> availableOrders();

	View peek() { return View(*d, l); }
	unsigned current() {  // TODO: temporary until we have a better interface!
		return d - &datasets[0];
	}

	void changeFAMS(float k); // to be called from different thread
	void cancelFAMS(); // can be called from different thread

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void selectedDataset();
	void newDataset(unsigned id);
	void newDisplay(const QString &name);
	void newClustering(bool withOrder = false);
	void newHierarchy(bool withOrder = false);
	void newOrder();
	void ioError(const QString &message);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void select(unsigned index); // reset d*
	void spawn(const Configuration& config, QString initialDisplay = {});
	void computeDisplay(const QString &name);
	void computeDisplays();
	void clearClusters();
	void computeFAMS();
	void calculatePartition(unsigned granularity);
	void updateColorset(QVector<QColor> colors);
	void changeOrder(OrderBy reference, bool synchronize);

public:
	ProteinDB &proteins; // TODO: make protected and let others get it elsewhere

protected:
	bool readSource(QTextStream in, const QString& name);
	bool readScoredSource(QTextStream &in, const QString& name);
	bool readDescriptions(const QByteArray &tsv);
	bool readAnnotations(const QByteArray &tsv);
	bool readHierarchy(const QByteArray &json);
	void readDisplay(const QString &name, const QByteArray &tsv);

	void pruneClusters();
	void computeClusterCentroids();
	void orderClusters(bool genericNames);
	void colorClusters();
	void orderProteins(OrderBy reference);

	QByteArray writeDisplay(const QString &name);
	static QStringList trimCrap(QStringList values);
	static std::vector<QVector<QPointF>> pointify(const std::vector<std::vector<double>> &in);

	// vector of loaded datasets
	std::vector<Public> datasets;
	// the currently used/exposed dataset in datasets
	Public *d;
	// lock that we use when accessing datasets
	QReadWriteLock l{QReadWriteLock::RecursionMode::Recursive};

	struct {
		std::unique_ptr<seg_meanshift::FAMS> fams;
		float k = -1;
	} meanshift;

	QVector<QColor> colorset = {Qt::black};
};

Q_DECLARE_METATYPE(Dataset::Configuration)

#endif // DATASET_H
