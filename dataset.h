#ifndef DATASET_H
#define DATASET_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QList>
#include <QPointF>
#include <QColor>
#include <QReadWriteLock>

class QFile;

class Dataset : public QObject
{
	Q_OBJECT

public:
	struct Cluster {
		QString name;
		QColor color; // TODO: do we use this? nah! get rid.
	};

	struct Protein {
		// <name>_<species> as read from the data, used as identifier
		QString name;
		// first part of protein name
		QString firstName;
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
		QStringList dimensions;

		QMap<QString, unsigned> protIndex; // map indentifiers to index in vectors

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

	QVector<unsigned> loadMarkers(const QString &filename);
	void saveMarkers(const QString &filename, const QVector<unsigned> &indices);

	struct {
		QString filename;
		qint64 size;
		QByteArray checksum;
	} source;

signals: // IMPORTANT: when connecting to lambda, use object pointer for thread-affinity
	void newData(const QString &filename);
	void newClustering();
	void newHierarchy();
	void ioError(const QString &message);

public slots: // IMPORTANT: never call these directly! use signals for thread-affinity
	void loadDataset(const QString &filename);
	void loadAnnotations(const QString &filename);
	void loadHierarchy(const QString &filename);
	void calculatePartition(unsigned granularity);

protected:
	bool read();
	bool readSource();
	void write();

	QString qvName();

	static QByteArray fileChecksum(QFile *file);

	Public d;
	QReadWriteLock l;
};

#endif // DATASET_H
