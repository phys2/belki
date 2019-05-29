#ifndef DATASET_H
#define DATASET_H

#include "utils.h"
#include "proteindb.h"
#include "compute/features.h"
#include "meanshift/fams.h"

#include <QObject>
#include <QFlags>
#include <QString>
#include <QMap>
#include <QVector>
#include <QList>
#include <QPointF>
#include <QColor>

#include <set>
#include <map>
#include <unordered_map>
#include <memory>

class QTextStream;

// a configuration that describes processing resulting in a dataset
struct DatasetConfiguration {
	QString name; // user-specified identifier
	unsigned id; // index of dataset (temporary)
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

	enum class Touch {
		BASE = 0x1,
		DISPLAY = 0x2,
		HIERARCHY = 0x4,
		CLUSTERS = 0x8,
		ORDER = 0x10,
		ALL = 0xFF
	};
	using Touched = QFlags<Touch>;

	struct Base : RWLockable {
		bool hasScores() const { return !scores.empty(); }
		const auto& lookup(View<ProteinDB::Public> &v, unsigned index) const {
			return v->proteins[protIds[index]];
		}

		QStringList dimensions;

		// meta information for this dataset
		DatasetConfiguration conf;

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
	};

	struct Representation : public RWLockable {
		// feature reduced point sets
		std::map<QString, QVector<QPointF>> display;
	};

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

	struct Structure : public RWLockable {
		// clusters / hierarchy, if available
		Clustering clustering;
		std::vector<HrCluster> hierarchy;

		// order of proteins
		// determined by hierarchy or clusters (if available), pos. in file, or name
		Order order;
	};

	explicit Dataset(ProteinDB &proteins);

	static const std::map<Dataset::OrderBy, QString> availableOrders();

	template<typename T>
	View<T> peek() const; // see specializations in cpp

	unsigned id() const;
	void setId(unsigned id);
	void changeFAMS(float k); // to be called from different thread
	void cancelFAMS(); // can be called from different thread

	QByteArray exportDisplay(const QString &name) const;

	void spawn(ConstPtr source, const DatasetConfiguration& config);
	bool readSource(QTextStream &in, const QString& name, bool scored);

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
	bool readScoredSource(QTextStream &in, const QString& name);
	bool finalizeRead();
	void swapClustering(Clustering &cl, bool genericNames, bool pruneCl, bool reorderProts);

	void pruneClusters();
	void computeClusterCentroids();
	void orderClusters(bool genericNames);
	void colorClusters();
	void orderProteins(OrderBy reference);

	static QStringList trimCrap(QStringList values);

	Base b;
	Representation r;
	Structure s;

	struct { // TODO own small class in compute that does all meanshift stuff
		std::unique_ptr<seg_meanshift::FAMS> fams;
		float k = -1;
	} meanshift;

	QVector<QColor> colorset = {Qt::black};
	ProteinDB &proteins;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Dataset::Touched)
Q_DECLARE_METATYPE(Dataset::Touched)
Q_DECLARE_METATYPE(Dataset::Ptr)
Q_DECLARE_METATYPE(Dataset::ConstPtr)

#endif // DATASET_H
