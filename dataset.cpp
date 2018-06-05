#include "dataset.h"
#include "dimred.h"

#include <QFile>
#include <QFileInfo>
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

	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		emit ioError(QString("Could not read file %1!").arg(filename));
		return;
	}

	/* load data, right now better lock until the signal is out */
	QWriteLocker _(&l);

	d = Public();
	d.source.filename = filename;

	auto success = read(f); // obtain data and interim results
	if (!success)
		return;

	/* compute displays,
	 * TODO: on demand, in that case we need several signals newData + newDisplay */
	const std::vector<QString> available = {"PCA12", "PCA13", "PCA23", "tSNE"};
	for (auto &m : available) {
		if (!d.display.contains(m)) {
			auto result = dimred::compute(m, d.features);
			//QWriteLocker _(&l);
			d.display[m] = std::move(result);
		}
	}

	/* let the outside world know, see above, would be better to do directly after read */
	emit newData();
}

void Dataset::readAnnotations(QTextStream in)
{
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

	/* ensure we have data to annotate */
	qDebug() << d.proteins.size();
	if (d.proteins.empty()) {
		emit ioError("Please load protein profiles first!");
		return;
	}

	/* setup clusters */
	for (auto &p : d.proteins)
		p.memberOf.clear();
	d.clustering.resize((unsigned)header.size());
	for (auto i = 0; i < header.size(); ++i) {
		d.clustering[(unsigned)i] = {header[i]};
	}

	/* associate to clusters */
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() < 3)
			continue;

		auto name = line[1];
		line.removeFirst();
		line.removeFirst();
		try {
			auto p = d.find(name);
			for (auto i = 0; i < header.size(); ++i) { // run over header to only allow valid columns
				if (line[i].isEmpty() || line[i].contains(QRegularExpression("^\\s*$")))
					continue;
				d.proteins[p].memberOf.push_back((unsigned)i);
			}
		} catch (std::out_of_range) {
			qDebug() << "Ignored" << name << "(unknown)";
		}
	}

	emit newClustering();
}

void Dataset::readHierarchy(const QByteArray &json)
{
	auto root = QJsonDocument::fromJson(json).object();
	if (root.isEmpty()) {
		emit ioError("The selected file does not contain valid JSON!");
		return;
	}

	QWriteLocker _(&l);

	/* ensure we have data to annotate */
	qDebug() << d.proteins.size();
	if (d.proteins.empty()) {
		emit ioError("Please load protein profiles first!");
		return;
	}

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
			auto name = content[0].toString();
			try {
				c.protein = d.find(name);
			} catch (std::out_of_range) {
				qDebug() << "Ignored" << name << "(unknown)";
				c.protein = -1;
			}
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

	/* determine clusters to be displayed */
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

	// helper to recursively assign all proteins to clusters
	std::function<void(unsigned, unsigned)> flood;
	flood = [&] (unsigned hIndex, unsigned cIndex) {
		auto &current = container[hIndex];
		if (current.protein >= 0)
			d.proteins[(unsigned)current.protein].memberOf = {cIndex};
		for (auto &c : current.children)
			flood(c, cIndex);
	};

	/* remove previous cluster assignments in case of incomplete clustering;
	   incomplete clusterings may happen when the clustering was run on different data */
	for (auto &p : d.proteins)
		p.memberOf.clear();

	/* set up clusters based on candidates */
	auto &target = d.clustering;
	target.clear();
	for (auto i : candidates) {
		auto name = QString("Cluster #%1").arg(container.size() - i);
		target.push_back({name});
		flood(i, target.size() - 1);
	}

	emit newClustering();
}

bool Dataset::read(QFile &f) // runs within write lock
{
	// get the source data
	auto success = readSource(f);
	if (!success)
		return false;

	// try to read pre-computed results
	QFile fq(qvName());
	if (!fq.open(QIODevice::ReadOnly)) {
		qDebug() << "No serialized interim results found";
		return true;
	}

	QDataStream in(&fq);
	qint64 size;
	in >> size;
	if (size != d.source.size) {
		qDebug() << "Interim results not compatible with file (size)";
		return true;
	}

	QByteArray checksum;
	in >> checksum;
	if (checksum != d.source.checksum) {
		qDebug() << "Interim results not compatible with file (checksum)";
		return true;
	}

	in >> d.display;
	return true;
}

bool Dataset::readSource(QFile &f) // runs within write lock
{
	QTextStream in(&f);
	auto header = in.readLine().split("\t");
	header.pop_front(); // first column
	d.dimensions = header;
	auto len = d.dimensions.size();
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		Protein p;
		auto parts = line[0].split("_");
		p.name = parts.front();
		p.species = (parts.size() > 1 ? parts.back() : "RAT"); // wild guess

		if (line.size() < len + 1) {
			emit ioError(QString("Stopped at protein '%1', incomplete row!").arg(p.name));
			break; // avoid message flood
		}

		QVector<double> coeffs(len);
		QVector<QPointF> points(len);
		for (int i = 0; i < len; ++i) {
			coeffs[i] = line[i+1].toDouble();
			points[i] = {(qreal)i, coeffs[i]};
		}
		d.features.append(std::move(coeffs));
		d.featurePoints.push_back(std::move(points));

		if (d.protIndex.find(p.name) != d.protIndex.end())
			emit ioError(QString("Multiples of protein '%1' found in the dataset!").arg(p.name));

		d.protIndex[p.name] = d.proteins.size();
		d.proteins.push_back(std::move(p));
	}
	qDebug() << "read" << d.features.size() << "rows with" << len << "columns";

	d.source.size = f.size();
	d.source.checksum = fileChecksum(&f);

	return true;
}

void Dataset::write()
{
	if (d.source.filename.isEmpty() || !d.source.size)
		return; // no data loaded

	QFile f(qvName());
	f.open(QIODevice::WriteOnly);
	QDataStream out(&f);
	out << d.source.size;
	out << d.source.checksum;
	out << d.display;
	qDebug() << "Saved interim results to" << f.fileName();
}

QString Dataset::qvName()
{
	QFileInfo fi(d.source.filename);
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


