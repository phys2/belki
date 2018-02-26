#include "dataset.h"
#include "dimred.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QMessageBox>

#include <set>

#include <QDebug>

void Dataset::loadDataset(const QString &filename)
{
	write(); // save interim results on previous dataset

	source = {filename, {}, {}};

	auto success = read(); // obtain data and interim results
	if (!success)
		return;

	// TODO: compute on-demand
	const std::vector<QString> available = {"PCA12", "PCA13", "PCA23", "tSNE"};
	for (auto &m : available) {
		if (!d.display.contains(m)) {
			auto result = dimred::compute(m, d.features);
			QWriteLocker _(&l);
			d.display[m] = std::move(result);
		}
	}

	emit newData(filename);
}

void Dataset::loadAnnotations(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return;
	}

	QTextStream in(&f);
	// we use SkipEmptyParts for chomping, but dangerous…
	auto header = in.readLine().split("\t", QString::SkipEmptyParts);
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() < 3 || !header[1].contains(re)) {
		emit ioError("Could not parse file!<p>The second column must contain protein names.</p>");
		return;
	}
	header.removeFirst();
	header.removeFirst();

	QWriteLocker _(&l);

	/* setup clusters */
	for (auto &p : d.proteins)
		p.memberOf.clear();
	d.clustering.resize((unsigned)header.size());
	for (auto i = 0; i < header.size(); ++i) {
		d.clustering[(unsigned)i] = {header[i], {}};
	}

	/* associate to clusters */
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() < 3)
			continue;

		auto name = line[1];
		line.removeFirst();
		line.removeFirst();
		auto p = d.protIndex.find(name);
		if (p == d.protIndex.end()) {
			qDebug() << "Ignored" << name << "(unknown)";
			continue;
		}

		for (auto i = 0; i < line.size(); ++i) {
			if (line[i].isEmpty() || line[i].contains(QRegularExpression("^\\s*$")))
				continue;
			d.proteins[p.value()].memberOf.push_back((unsigned)i);
		}
	}

	emit newClustering();
}

void Dataset::loadHierarchy(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return;
	}
	auto root = QJsonDocument::fromJson(f.readAll()).object();
	if (root.isEmpty()) {
		emit ioError(QString("File %1 does not contain valid JSON!").arg(filename));
		return;
	}

	QWriteLocker _(&l);
	auto nodes = root["data"].toObject()["nodes"].toObject();
	auto &container = d.hierarchy;

	// some preparation. we can expect at least as much clusters:
	container.reserve(2 * d.proteins.size()); // binary tree
	container.resize(d.proteins.size()); // cluster-per-protein

	for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
		unsigned id = it.key().toUInt();
		auto node = it.value().toObject();
		if (id + 1 > container.size())
			container.resize(id + 1);

		auto &c = container[id];
		c.distance = node["distance"].toDouble();

		/* leaf: associate proteins */
		auto content = node["objects"].toArray();
		if (content.size() == 1) {
			auto pIt = d.protIndex.find(content[0].toString());
			if (pIt == d.protIndex.end()) {
				qDebug() << "Ignored" << content[0].toString() << "(unknown)";
			}
			c.protein = (int)pIt.value();
		} else {
			c.protein = -1;
		}

		/* non-leaf: associate children */
		if (node.contains("left_child")) {
			c.children = {(unsigned)node["left_child"].toInt(),
			              (unsigned)node["right_child"].toInt()};
		}
	}

	emit newHierarchy();
}

void Dataset::calculatePartition(unsigned granularity)
{
	QWriteLocker _(&l);

	auto &container = d.hierarchy;

	granularity = std::min(granularity, (unsigned)container.size());
	unsigned lowBound = container.size() - granularity - 1;

	std::set<unsigned> candidates;
	// we use the fact that input is sorted by distance, ascending
	for (auto i = lowBound; i < container.size(); ++i) {
		auto &current = container[i];

		// add either parent or childs, if any of them is eligible by-itself
		auto useChildrenInstead = false;
		for (auto c : current.children) {
			if (c >= lowBound)
				useChildrenInstead = true;
		}
		if (useChildrenInstead) {
			for (auto c : current.children) {
				if (c < lowBound) // only add what's not covered by granularity
					candidates.insert(c);
			}
		} else {
			candidates.insert(i);
		}
	}

	// recursively assign all proteins to clusters
	std::function<void(unsigned, unsigned)> flood;
	flood = [&] (unsigned hIndex, unsigned cIndex) {
		auto &current = container[hIndex];
		if (current.protein >= 0)
			d.proteins[(unsigned)current.protein].memberOf = {cIndex};
		for (auto &c : current.children)
			flood(c, cIndex);
	};

	// set up clusters based on candidates
	auto &target = d.clustering;
	target.clear();
	for (auto i : candidates) {
		auto name = QString("Cluster #%1").arg(container.size() - i);
		target.push_back({name, {}});
		flood(i, target.size() - 1);
	}

	emit newClustering();
}

QVector<unsigned> Dataset::loadMarkers(const QString &filename)
{
	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return {};
	}

	/* public method -> called from other thread -> we must lock! */
	QReadLocker _(&l);
	QVector<unsigned> ret;
	QTextStream in(&f);
	while (!in.atEnd()) {
		QString name;
		in >> name;

		auto it = d.protIndex.find(name);
		if (it == d.protIndex.end()) {
			qDebug() << "Ignored" << name << "(unknown)";
			continue;
		}
		ret.append(it.value());
	}
	return ret;
}

void Dataset::saveMarkers(const QString &filename, const QVector<unsigned> &indices)
{
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly)) {
		emit ioError(QString("Could not write file %1!").arg(filename));
		return;
	}

	/* public method -> called from other thread -> we must lock! */
	QReadLocker _(&l);
	QTextStream out(&f);
	for (auto i : indices) {
		out << d.proteins[i].name << endl;
	}
}

bool Dataset::read()
{
	// get the source data
	auto success = readSource();
	if (!success)
		return false;

	// try to read pre-computed results
	QFile f(qvName());
	if (!f.open(QIODevice::ReadOnly)) {
		qDebug() << "No serialized interim results found";
		return true;
	}

	QDataStream in(&f);
	qint64 size;
	in >> size;
	if (size != source.size) {
		qDebug() << "Interim results not compatible with file (size)";
		return true;
	}

	QByteArray checksum;
	in >> checksum;
	if (checksum != source.checksum) {
		qDebug() << "Interim results not compatible with file (checksum)";
		return true;
	}

	l.lockForWrite();
	in >> d.display;
	l.unlock();
	return true;
}

bool Dataset::readSource()
{
	QFile f(source.filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(source.filename));
		return false;
	}

	QWriteLocker _(&l);
	d.proteins.clear();
	d.protIndex.clear();
	d.features.clear();
	d.featurePoints.clear();

	QTextStream in(&f);
	d.dimensions = in.readLine().split("\t", QString::SkipEmptyParts);
	auto len = d.dimensions.size();
	unsigned index = 0;
	while (!in.atEnd()) {
		QString name;
		in >> name;
		if (name.length() < 1)
			break; // early EOF
		auto parts = name.split("_");
		Protein p{name, parts.first(), parts.last(), {}};

		QVector<double> coeffs(len);
		QVector<QPointF> points(len);
		for (int i = 0; i < len; ++i) {
			in >> coeffs[i];
			points[i] = {(qreal)i, coeffs[i]};
		}
		d.features.append(std::move(coeffs));
		d.featurePoints.push_back(std::move(points));
		d.proteins.push_back(std::move(p));
		d.protIndex[name] = index;
		index++;
	}
	qDebug() << "read" << d.features.size() << "rows with" << len << "columns";

	source.size = f.size();
	source.checksum = fileChecksum(&f);

	return true;
}

void Dataset::write()
{
	if (source.filename.isEmpty() || !source.size)
		return; // no data loaded

	QFile f(qvName());
	f.open(QIODevice::WriteOnly);
	QDataStream out(&f);
	out << source.size;
	out << source.checksum;
	out << d.display;
	qDebug() << "Saved interim results to" << f.fileName();
}

QString Dataset::qvName()
{
	QFileInfo fi(source.filename);
	return fi.path() + "/" + fi.completeBaseName() + ".qv";
}

QByteArray Dataset::fileChecksum(QFile *file)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (hash.addData(file)) {
		return hash.result();
	}
	return QByteArray();
}
