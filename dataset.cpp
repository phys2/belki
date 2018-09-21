#include "dataset.h"
#include "dimred.h"

#include <QtCore/QDataStream>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>

#include <QDebug>

void Dataset::computeDisplay(const QString& name)
{
	// note: no read lock as we are write thread
	auto result = dimred::compute(name, d.features);

	QWriteLocker _(&l);
	for (auto name : result.keys()) {
		d.display[name] = result[name];
		emit newDisplay(name);
	}
}

void Dataset::computeDisplays()
{
	/* compute PCA displays as a fast starting point */
	if (!d.display.contains("PCA 12"))
		computeDisplay("PCA");
}

void Dataset::computeFAMS()
{
	auto &fams = meanshift.fams;
	qDebug() << "FAMS " << meanshift.k << " vs " << (fams ? fams->config.k : 0);
	if (fams && (fams->config.k == meanshift.k || meanshift.k <= 0))
		return; // already done

	fams.reset(new seg_meanshift::FAMS({
	                                       .k=meanshift.k,
	                                       .use_LSH=false,
	                                       .pruneMinN = 0, // we use pruneClusters() instead
	                                   }));
	fams->importPoints(d.features, true);
	bool success = fams->prepareFAMS();
	if (!success)
		return;
	fams->selectStartPoints(0., 1);
	success = fams->finishFAMS();
	if (!success)
		return;

	fams->pruneModes();

	QWriteLocker _(&l);
	d.clustering.clear();
	auto nmodes = meanshift.fams->getModes().size();
	for (unsigned i = 0; i < nmodes; ++i)
		d.clustering[i] = {QString("Cluster #%1").arg(i+1)};

	auto &index = meanshift.fams->getModePerPoint();
	for (unsigned i = 0; i < index.size(); ++i) {
		auto m = (unsigned)index[i];
		d.proteins[i].memberOf = {m};
		d.clustering[m].size++;
	}
	pruneClusters();

	emit newClustering();
}

void Dataset::changeFAMS(float k)
{
	qDebug() << "changing to " << k;
	meanshift.k = k;
	if (meanshift.fams) {
		meanshift.fams->cancel();
		meanshift.fams->config.k = 0;
	}
}

void Dataset::cancelFAMS()
{
	changeFAMS(-1);
}

bool Dataset::readSource(QTextStream in)
{
	QWriteLocker _(&l);

	/* re-initialize data */
	cancelFAMS();
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

bool Dataset::readDescriptions(const QByteArray &tsv)
{
	QTextStream in(tsv);
	auto header = in.readLine().split("\t");
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() != 2 || !header[0].contains(re)) {
		emit ioError("Could not parse file!<p>The first column must contain protein names, second descriptions.</p>");
		return false;
	}

	QWriteLocker _(&l);

	/* ensure we have data to annotate */
	if (d.proteins.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
	}

	/* fill-in descriptions */
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() < 2)
			continue;

		auto name = line[0];
		try {
			auto p = d.find(name);
			d.proteins[p].description = line[1];
		} catch (std::out_of_range&) {
			qDebug() << "Ignored" << name << "(unknown)";
		}
	}
	return true;
}

bool Dataset::readAnnotations(const QByteArray &tsv)
{
	/* ensure we have data to annotate */
	if (d.proteins.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
	}

	QTextStream in(tsv);
	// we use SkipEmptyParts for chomping at the end, but dangerous…
	auto header = in.readLine().split("\t", QString::SkipEmptyParts);
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() == 2 && header[1].contains("Members")) {
		/* expect name + list of proteins per-cluster per-line */

		QWriteLocker _(&l);

		/* clear clusters */
		for (auto &p : d.proteins)
			p.memberOf.clear();
		d.clustering.clear();

		/* build new clusters */
		unsigned clusterIndex = 0;
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t");
			if (line.size() < 2)
				continue;

			d.clustering[clusterIndex] = {line[0]};
			line.removeFirst();

			for (auto &name : qAsConst(line)) {
				try {
					auto p = d.find(name);
					d.proteins[p].memberOf.insert(clusterIndex);
					d.clustering[clusterIndex].size++;
				} catch (std::out_of_range&) {
					qDebug() << "Ignored" << name << "(unknown)";
				}
			}

			clusterIndex++;
		}
	} else if (header.size() > 1 && header[0].contains(re)) {
		/* expect matrix layout, first column protein names */
		header.removeFirst();

		QWriteLocker _(&l);

		/* setup clusters */
		for (auto &p : d.proteins)
			p.memberOf.clear();
		d.clustering.clear();
		d.clustering.reserve((unsigned)header.size());
		for (auto i = 0; i < header.size(); ++i) {
			d.clustering[(unsigned)i] = {header[i]};
		}

		/* associate to clusters */
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t");
			if (line.size() < 2)
				continue;

			auto name = line[0];
			line.removeFirst();
			try {
				auto p = d.find(name);
				for (auto i = 0; i < header.size(); ++i) { // run over header to only allow valid columns
					if (line[i].isEmpty() || line[i].contains(QRegularExpression("^\\s*$")))
						continue;
					d.proteins[p].memberOf.insert((unsigned)i);
					d.clustering[(unsigned)i].size++;
				}
			} catch (std::out_of_range&) {
				qDebug() << "Ignored" << name << "(unknown)";
			}
		}
	} else {
		emit ioError("Could not parse file!<p>The first column must contain protein or group names.</p>");
		return false;
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
		if (current.protein >= 0) {
			d.proteins[(unsigned)current.protein].memberOf = {cIndex};
			d.clustering[cIndex].size++;
		}
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
	target.reserve(candidates.size());
	for (auto i : candidates) {
		auto name = QString("Cluster #%1").arg(container.size() - i);
		// use index in hierarchy as cluster index as well
		target[i] = {name};
		flood(i, i);
	}

	pruneClusters();

	emit newClustering();
}

void Dataset::pruneClusters()
{
	/* defragment clusters (un-assign and remove small clusters) */
	// TODO: make configurable; instead keep X biggest clusters?
	auto minSize = unsigned(0.005f * (float)d.proteins.size());
	auto it = d.clustering.begin();
	while (it != d.clustering.end()) {
		if (it->second.size < minSize) {
			for (auto &p : d.proteins)
				p.memberOf.erase(it->first);
			it = d.clustering.erase(it);
		} else {
			it++;
		}
	}
}
