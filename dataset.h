#ifndef DATASET_H
#define DATASET_H

#include <QtCore/QString>
#include <QtCore/QMap>
#include <QtCore/QVector>
#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtGui/QColor>
#include <QtCore/QReadWriteLock>
#include <QtCore/QTextStream>
#include <QtCore/QByteArray>

#include <map>

class QFile;

class Dataset : public QObject
{
	Q_OBJECT

	friend class Storage; // NOTE: ensure that Storage object resides in same thread!

public:
	struct Cluster {
		QString name;
	};

	struct Protein {
		// first part of protein name, used as identifier
		QString name;
		// last part of protein name
		QString species;
		// annotations, if any
		std::vector<unsigned> memberOf;
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

		struct {
			QString filename;
			qint64 size;
			QByteArray checksum;
		} source;

		QStringList dimensions;

		std::map<QString, unsigned> protIndex; // map indentifiers to index in vectors

		// meta data
		std::vector<Protein> proteins;

		// original data
		QVector<QVector<double>> features;
		// pre-cached set of points
		std::vector<QVector<QPointF>> featurePoints;

		// feature reduced point sets
		QMap<QString, QVector<QPointF>> display;

		// clusters, if available
		std::vector<Cluster> clustering;
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

	~Dataset() {
		write(); // save interim results
	}

	View peek() { return View(d, l); }

signals: // IMPORTANT: when connecting to lambda, provide target object pointer for thread-affinity
	void newData();
	void newClustering();
	void newHierarchy();
	void ioError(const QString &message);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void loadDataset(const QString &filename);
	void calculatePartition(unsigned granularity);

protected:
	bool read(QFile &f); // helper to loadDataset(), assumes write lock
	bool readSource(QFile &f); // helper to read(), assumes write lock
	void readAnnotations(QTextStream in);
	void readHierarchy(const QByteArray &json);
	void write();

	QString qvName();

	static QByteArray fileChecksum(QFile *file);

	Public d;
	QReadWriteLock l;
};

#endif // DATASET_H
