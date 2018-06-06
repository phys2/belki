#include "dataset.h"
#include "dimred.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

#include <set>

#include <QDebug>

void Dataset::computeDisplay(const QString& name)
{
	// note: no read lock as we are write thread
	auto result = dimred::compute(name, d.features);

	QWriteLocker _(&l);
	d.display[name] = std::move(result);

	emit newDisplay(name);
}

void Dataset::computeDisplays()
{
	/* compute displays,
	 * TODO: on demand (ie specify through argument what is needed), not be called by storage but by GUI */
	const std::vector<QString> available = {"PCA12", "PCA13", "PCA23", "tSNE"};
	for (auto &m : available) {
		if (!d.display.contains(m))
			computeDisplay(m);
	}
}

bool Dataset::readSource(QTextStream in)
{
	QWriteLocker _(&l);

	/* re-initialize data */
	d = Public();

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

	emit newSource();
	return true;
}

void Dataset::readDisplay(const QString& name, const QByteArray &tsv)
{
	QTextStream in(tsv);
	QVector<QPointF> data;
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() != 2)
			return ioError(QString("Input malformed at line %2 in display %1").arg(name, data.size()+1));

		data.push_back({line[0].toDouble(), line[1].toDouble()});
	}

	QWriteLocker _(&l);

	if (data.size() != d.features.size())
		return ioError(QString("Display %1 length does not match source length!").arg(name));

	d.display[name] = std::move(data);
	emit newDisplay(name);
}

QByteArray Dataset::writeDisplay(const QString &name)
{
	// note: no read lock as we are write thread
	QByteArray ret;
	QTextStream out(&ret, QIODevice::WriteOnly);
	auto &data = d.display[name];
	for (auto it = data.constBegin(); it != data.constEnd(); ++it)
		out << it->x() << "\t" << it->y() << endl;

	return ret;
}

bool Dataset::readAnnotations(const QByteArray &tsv)
{
	QTextStream in(tsv);
	// we use SkipEmptyParts for chomping, but dangerous…
	auto header = in.readLine().split("\t", QString::SkipEmptyParts);
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() < 3 || !header[1].contains(re)) {
		emit ioError("Could not parse file!<p>The second column must contain protein names.</p>");
		return false;
	}
	header.removeFirst();
	header.removeFirst();

	QWriteLocker _(&l);

	/* ensure we have data to annotate */
	qDebug() << d.proteins.size();
	if (d.proteins.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
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
		} catch (std::out_of_range&) {
			qDebug() << "Ignored" << name << "(unknown)";
		}
	}

	emit newClustering();
	return true;
}

bool Dataset::readHierarchy(const QByteArray &json)
{
	auto root = QJsonDocument::fromJson(json).object();
	if (root.isEmpty()) {
		emit ioError("The selected file does not contain valid JSON!");
		return false;
	}

	QWriteLocker _(&l);

	/* ensure we have data to annotate */
	qDebug() << d.proteins.size();
	if (d.proteins.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
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
				c.protein = (int)d.find(name);
			} catch (std::out_of_range&) {
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
	return true;
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
