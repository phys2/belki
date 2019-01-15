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

class Dataset : public QObject
{
	Q_OBJECT

	friend class Storage; // NOTE: ensure that Storage object resides in same thread!

public:
	struct Protein {
		// first part of protein name, used as identifier
		QString name;
		// last part of protein name
		QString species;
		// description, if any
		QString description;
		// annotations, if any
		std::set<unsigned> memberOf;
	};

	struct Cluster {
		QString name;
		QColor color;
		unsigned size = 0;
	};

	struct HrCluster {
		double distance;
		int protein;
		std::vector<unsigned> children;
	};

	struct Public {
		// helper for finding proteins, name may contain species, throws
		unsigned find(const QString &name) {
			return protIndex.at(name.split('_').front());
		}

		QStringList dimensions;

		std::map<QString, unsigned> protIndex; // map indentifiers to index in vectors

		// meta data
		std::vector<Protein> proteins;

		// original data
		QVector<std::vector<double>> features;
		// pre-cached set of points
		std::vector<QVector<QPointF>> featurePoints;

		// feature reduced point sets
		QMap<QString, QVector<QPointF>> display;

		// clusters, if available
		std::unordered_map<unsigned, Cluster> clustering;
		std::vector<unsigned> clusterOrder;
		std::vector<HrCluster> hierarchy;
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

	View peek() { return View(d, l); }

	void changeFAMS(float k); // to be called from different thread
	void cancelFAMS(); // can be called from different thread

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void newSource();
	void newDisplay(const QString &name);
	void newClustering();
	void newHierarchy();
	void ioError(const QString &message);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void computeDisplay(const QString &name);
	void computeDisplays();
	void computeFAMS();
	void calculatePartition(unsigned granularity);
	void updateColorset(QVector<QColor> colors);

protected:
	bool readSource(QTextStream in);
	bool readDescriptions(const QByteArray &tsv);
	bool readAnnotations(const QByteArray &tsv);
	bool readHierarchy(const QByteArray &json);
	void readDisplay(const QString &name, const QByteArray &tsv);

	void pruneClusters();
	void orderClusters(bool genericNames);
	void colorClusters();

	QByteArray writeDisplay(const QString &name);

	Public d;
	QReadWriteLock l;

	struct {
		std::unique_ptr<seg_meanshift::FAMS> fams;
		float k = -1;
	} meanshift;

	QVector<QColor> colorset = {Qt::black};
};

#endif // DATASET_H
