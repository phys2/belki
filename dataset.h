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

	/* local (enhanced) copy of global annotations or internal annotations */
	struct Annotations : ::Annotations {
		Annotations(const ::Annotations &source, const Features &data);

		// memberships of each protein from dataset perspective
		std::vector<std::set<unsigned>> memberships;
	};

	struct Order : ::Order {
		std::vector<unsigned> index = {}; // protein indices ordered
		std::vector<unsigned> rankOf = {}; // position of each protein in the order
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
		// TODO: put distmats here & put this in model.h
	};

	struct Structure : RWLockable {
		// picks from annotations if available
		const Annotations* fetch(const Annotations::Meta &desc) const;
		// picks from orders or picks fallback
		const Order& fetch(::Order desc) const;

		// available annotations by global id, 0 means internal
		std::multimap<unsigned, Annotations> annotations;

		// available protein orderings (in dataset scope) by global id,
		// 0 means based on internal annot.
		std::multimap<unsigned, Order> orders;
		// default orders (always available)
		Order fileOrder, nameOrder;
	};

	enum class Touch {
		BASE = 0x1,
		DISPLAY = 0x2,
		// unused HIERARCHY = 0x4,
		CLUSTERS = 0x8,
		ORDER = 0x10,
		ALL = 0xFF
	};
	using Touched = QFlags<Touch>;

	explicit Dataset(ProteinDB &proteins, DatasetConfiguration conf);
	const DatasetConfiguration& config() const { return conf; }
	unsigned id() const { return conf.id; }

	template<typename T>
	View<T> peek() const; // see specializations in cpp

	QByteArray exportDisplay(const QString &name) const;

	void spawn(Features::Ptr data);
	void spawn(ConstPtr source);

	void computeDisplay(const QString &name);
	void computeDisplays();
	bool readDisplay(const QString &name, QTextStream &tsv);

	void prepareAnnotations(const Annotations::Meta &desc);
	void prepareOrder(const ::Order &desc);

signals:
	void update(Touched);

protected:
	Touched storeAnnotations(const ::Annotations &source, bool withOrder);
	::Annotations computeFAMS(float k);
	::Annotations createPartition(unsigned id, unsigned granularity);
	void computeOrder(const ::Order &desc);
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
