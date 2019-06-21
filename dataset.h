#ifndef DATASET_H
#define DATASET_H

#include "utils.h"
#include "model.h"
#include "proteindb.h"
#include "meanshift/fams.h"

#include <QObject>
#include <QFlags>
#include <QMap>
#include <QList>

#include <set>
#include <map>
#include <memory>

class QTextStream;

// a configuration that describes processing resulting in a dataset
struct DatasetConfiguration {
	QString name; // user-specified identifier
	unsigned id; // index of dataset
	unsigned parent = 0; // index of dataset this one was spawned from (0 == none)
	std::vector<unsigned> bands; // the feature bands that were kept
	double scoreThresh = 0.; // score cutoff that was applied
};
Q_DECLARE_METATYPE(DatasetConfiguration)

class Dataset : public QObject
{
	Q_OBJECT

public:
	using Ptr = std::shared_ptr<Dataset>;
	using ConstPtr = std::shared_ptr<Dataset const>;
	using Proteins = ProteinDB::Public;

	enum class Direction {
		PER_PROTEIN,
		PER_DIMENSION,
	};
	Q_ENUM(Direction)

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

	struct Base : Features, RWLockable {
		Base &operator=(Features in); // destructive non-copy assignment
		const auto& lookup(View<ProteinDB::Public> &v, unsigned index) const {
			return v->proteins[protIds[index]];
		}
		// pre-cached set of points
		std::vector<QVector<QPointF>> featurePoints;
	};

	struct Representation : RWLockable {
		// feature reduced point sets
		std::map<QString, QVector<QPointF>> display;
		// TODO: put distmats here
	};

	struct Structure : RWLockable {
		// clusters / hierarchy, if available
		Clustering clustering;
		std::vector<HrCluster> hierarchy;

		// order of proteins
		// determined by hierarchy or clusters (if available), pos. in file, or name
		Order order;
	};

	enum class Touch {
		BASE = 0x1,
		DISPLAY = 0x2,
		HIERARCHY = 0x4,
		CLUSTERS = 0x8,
		ORDER = 0x10,
		ALL = 0xFF
	};
	using Touched = QFlags<Touch>;

	explicit Dataset(ProteinDB &proteins, DatasetConfiguration conf);
	const DatasetConfiguration& config() const { return conf; }
	unsigned id() const { return conf.id; }

	static const std::map<OrderBy, QString> availableOrders();

	template<typename T>
	View<T> peek() const; // see specializations in cpp

	void changeFAMS(float k); // to be called from different thread
	void cancelFAMS(); // can be called from different thread

	QByteArray exportDisplay(const QString &name) const;

	void spawn(Features data);
	void spawn(ConstPtr source);

	void computeDisplay(const QString &name);
	void computeDisplays();
	bool readDisplay(const QString &name, QTextStream &tsv);

	void clearClusters();
	bool readAnnotations(QTextStream &tsv);
	bool readHierarchy(const QJsonObject &json);
	void calculatePartition(unsigned granularity);
	void computeFAMS();
	void changeOrder(OrderBy reference, bool synchronize);

	void updateColorset(QVector<QColor> colors);

signals:
	void update(Touched);
	void ioError(const QString &message);

protected:
	void swapClustering(Clustering &cl, bool genericNames, bool pruneCl, bool reorderProts);

	void pruneClusters();
	void computeClusterCentroids();
	void orderClusters(bool genericNames);
	void colorClusters();
	void orderProteins(OrderBy reference);

	// meta information for this dataset
	DatasetConfiguration conf;

	// our current state
	Base b;
	Representation r;
	Structure s;

	struct : public RWLockable { // TODO own small class in compute that does all meanshift stuff
		std::unique_ptr<seg_meanshift::FAMS> fams;
		float k = -1;
	} meanshift;

	QVector<QColor> colorset = {Qt::black};
	ProteinDB &proteins;
};

// forward declarations, see cpp file
template<> View<Dataset::Base> Dataset::peek() const;
template<> View<Dataset::Representation> Dataset::peek() const;
template<> View<Dataset::Structure> Dataset::peek() const;
template<> View<Dataset::Proteins> Dataset::peek() const;

Q_DECLARE_OPERATORS_FOR_FLAGS(Dataset::Touched)
Q_DECLARE_METATYPE(Dataset::Touched)
Q_DECLARE_METATYPE(Dataset::Ptr)
Q_DECLARE_METATYPE(Dataset::ConstPtr)

#endif // DATASET_H
