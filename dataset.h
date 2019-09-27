#ifndef DATASET_H
#define DATASET_H

#include "utils.h"
#include "model.h"
#include "proteindb.h"
#include "compute/annotations.h"

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
	unsigned id = 0; // index of dataset (given by hub, starts from 1)
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

	struct Annotations : ::Annotations {
		explicit Annotations(size_t numProteins = 0) : memberships(numProteins) {}
		Annotations(const ::Annotations &source, const Features &data);
		bool empty() const { return groups.empty(); }

		// memberships of each protein from dataset perspective
		std::vector<std::set<unsigned>> memberships;
	};

	struct Order {
		OrderBy reference = OrderBy::HIERARCHY;
		bool synchronizing = true; // re-calculate whenever the source changes
		bool fallback = true; // enable one-off synchronization

		std::vector<unsigned> index; // protein indices ordered
		std::vector<unsigned> rankOf; // position of each protein in the order
	};

	struct Base : Features, RWLockable {
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
		Annotations clustering;
		HrClustering hierarchy;

		unsigned clusteringId = 0; // state info to avoid recomputation (0 = none/special)

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

	QByteArray exportDisplay(const QString &name) const;

	void spawn(Features::Ptr data);
	void spawn(ConstPtr source);

	void computeDisplay(const QString &name);
	void computeDisplays();
	bool readDisplay(const QString &name, QTextStream &tsv);
	void computeFAMS(float k);

	void applyClustering(const QString &name, const Features::Vec &modes, const std::vector<int>& index);
	void applyAnnotations(unsigned id);
	void applyHierarchy(unsigned id, unsigned granularity = 0);
	void createPartition(unsigned granularity);
	void cancelFAMS();
	void changeOrder(OrderBy reference, bool synchronize);

signals:
	void update(Touched);

protected:
	/* note: our protected helpers typically assume write locks in place and
	 * do not emit updates. */
	Touched calculatePartition(unsigned granularity);
	Touched applyAnnotations(const ::Annotations &source, unsigned id = 0, bool reorderProts = true);
	void orderProteins(OrderBy reference);

	void computeCentroids(Annotations &target);

	// meta information for this dataset
	DatasetConfiguration conf;

	// our current state
	Base b;
	Representation r;
	Structure s;

	// our meanshift worker. if set, holds a copy of features
	std::unique_ptr<annotations::Meanshift> meanshift;

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
