#ifndef DATASET_H
#define DATASET_H

#include "meanshift/fams.h"

#include <QString>
#include <QMap>
#include <QVector>
#include <QList>
#include <QPointF>
#include <QColor>
#include <QReadWriteLock>
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

	struct Range {
		explicit Range(const std::vector<std::vector<double>>& source);
		Range() = default;
		Range(double min, double max) : min(min), max(max) {}

		double scale();

		double min;
		double max;
	};

	struct Protein {
		// first part of protein name, used as identifier
		QString name;
		// last part of protein name
		QString species;
		// description, if any
		QString description;
		// random or user-set color
		QColor color;
	};

	struct Cluster {
		QString name;
		QColor color;
		unsigned size = 0;
		// mode of the cluster, if not available, centroid
		std::vector<double> mode;
	};

	struct Clustering {
		explicit Clustering(size_t numProteins = 0) : memberships(numProteins) {}
		bool empty() { return clusters.empty(); }

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

		unsigned parent = 0; // index of the dataset this one was spawned from
		std::vector<unsigned> bands; // the feature bands that were kept
	};

	struct Public {
		// helper for finding proteins, name may contain species, throws
		unsigned find(const QString &name) {
			return protIndex.at(name.split('_').front());
		}
		bool hasScores() { return !scores.empty(); }

		QStringList dimensions;

		// meta information for this dataset
		Configuration conf;

		std::map<QString, unsigned> protIndex; // map indentifiers to index in vectors

		// protein meta data (TODO: keep outside of dataset and have pointer in dataset?)
		std::vector<Protein> proteins;

		// original data
		std::vector<std::vector<double>> features;
		Range featureRange;
		// pre-cached set of points
		std::vector<QVector<QPointF>> featurePoints;
		// measurement scores
		std::vector<std::vector<double>> scores;
		Range scoreRange;

		// feature reduced point sets
		QMap<QString, QVector<QPointF>> display;

		// clusters / hierarchy, if available
		Clustering clustering;
		std::vector<HrCluster> hierarchy;

		// order of proteins
		// determined by hierarchy or clusters (if available), pos. in file, or name
		Order order;
	};

	struct View {
		View(Public &d, QReadWriteLock &l) : data(d), l(l) { l.lockForRead(); }
		View(const View&) = delete;
		View(View&& o) : data(o.data), l(o.l) {}
		~View() { l.unlock(); }
		Public& operator()() { return data; }
		Public* operator->() { return &data; }
	protected:
		Public &data;
		QReadWriteLock &l;
	};

	Dataset();

	static const std::map<Dataset::OrderBy, QString> availableOrders();

	View peek() { return View(*d, l); }

	void changeFAMS(float k); // to be called from different thread
	void cancelFAMS(); // can be called from different thread

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void newSource();
	void newDisplay(const QString &name);
	void newClustering(bool withOrder = false);
	void newHierarchy(bool withOrder = false);
	void newOrder();
	void ioError(const QString &message);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void computeDisplay(const QString &name);
	void computeDisplays();
	void clearClusters();
	void computeFAMS();
	void calculatePartition(unsigned granularity);
	void updateColorset(QVector<QColor> colors);
	void changeOrder(OrderBy reference, bool synchronize);

protected:
	bool readSource(QTextStream in);
	bool readScoredSource(QTextStream &in);
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
