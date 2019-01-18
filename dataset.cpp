#include "dataset.h"
#include "dimred.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

#include <unordered_set>

#include <QDebug>

void Dataset::computeDisplay(const QString& name)
{
	// empty data shouldn't happen but right now can when a file cannot be read completely,
	// in the future this should result in IOError already earlier
	if (d.features.empty())
		return;

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
	// empty data shouldn't happen but right now can when a file cannot be read completely,
	// in the future this should result in IOError already earlier
	if (d.features.empty())
		return;

	auto &fams = meanshift.fams;
	qDebug() << "FAMS " << meanshift.k << " vs " << (fams ? fams->config.k : 0);
	if (fams && (fams->config.k == meanshift.k || meanshift.k <= 0))
		return; // already done

	fams.reset(new seg_meanshift::FAMS({
	                                       .k=meanshift.k,
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
	auto modes = meanshift.fams->exportModes();
	for (unsigned i = 0; i < modes.size(); ++i)
		d.clustering[i] = {QString("Cluster #%1").arg(i+1), {}, 0, modes[i]};

	auto &index = meanshift.fams->getModePerPoint();
	for (unsigned i = 0; i < index.size(); ++i) {
		auto m = (unsigned)index[i];
		d.proteins[i].memberOf = {m};
		d.clustering[m].size++;
	}

	pruneClusters();
	computeClusterCentroids();
	orderClusters(true);
	colorClusters();

	orderProteins(OrderBy::CLUSTERING);

	emit newClustering();
}

void Dataset::changeFAMS(float k)
{
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

	auto header = in.readLine().split("\t");
	header.pop_front(); // first column
	if (header.contains("") || header.removeDuplicates()) {
		emit ioError("Malformed header: Duplicate or empty columns!");
		return false;
	}

	/* re-initialize data – no return false from this point on! */
	cancelFAMS();
	d = Public();

	/* fill it up */
	d.dimensions = header;
	auto len = d.dimensions.size();
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		if (line.size() < len + 1) {
			emit ioError(QString("Stopped at '%1', incomplete row!").arg(line[0]));
			break; // avoid message flood
		}

		/* setup metadata */
		Protein p;
		auto parts = line[0].split("_");
		p.name = parts.front();
		p.species = (parts.size() > 1 ? parts.back() : "RAT"); // wild guess

		/* check index */
		if (d.protIndex.find(p.name) != d.protIndex.end())
			emit ioError(QString("Multiples of protein '%1' found in the dataset!").arg(p.name));

		/* read coefficients */
		bool success = true;
		std::vector<double> coeffs((size_t)len);
		for (int i = 0; i < len; ++i) {
			bool ok;
			coeffs[(size_t)i] = line[i+1].toDouble(&ok);
			success = success && ok;
		}
		if (!success) {
			emit ioError(QString("Stopped at protein '%1', malformed row!").arg(p.name));
			break; // avoid message flood
		}

		/* append */
		d.protIndex[p.name] = d.proteins.size();
		d.proteins.push_back(std::move(p));
		d.features.append(std::move(coeffs));
	}

	qDebug() << "read" << d.features.size() << "rows with" << len << "columns";
	if (d.features.empty() || len == 0)
		return true;

	/* normalize, if needed */
	auto minVal = d.features[0][0], maxVal = d.features[0][0];
	for (auto in : qAsConst(d.features)) {
		double mi, ma;
		cv::minMaxLoc(in, &mi, &ma);
		minVal = std::min(minVal, mi);
		maxVal = std::max(maxVal, ma);
	}
	if (minVal < 0 || maxVal > 1) { // simple heuristic to auto-normalize
		emit ioError(QString("Values outside expected range (instead [%1, %2])."
		                     "<br>Normalizing to [0, 1].").arg(minVal).arg(maxVal));
		auto scale = 1. / (maxVal - minVal);
		for (int i = 0; i < d.features.size(); ++i) {
			auto &v = d.features[i];
			std::for_each(v.begin(), v.end(), [minVal, scale] (double &e) {
				e = (e - minVal) * scale;
			});
		}
	}

	/* pre-cache features as QPoints for plotting */
	for (auto in : qAsConst(d.features)) {
		QVector<QPointF> points(in.size());
		for (size_t i = 0; i < in.size(); ++i)
			points[i] = {(qreal)i, in[i]};
		d.featurePoints.push_back(std::move(points));
	}

	orderProteins(OrderBy::NAME);

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

			d.clustering[clusterIndex] = {line[0], {}, 0, {}};
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
			d.clustering[(unsigned)i] = {header[i], {}, 0, {}};
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

	computeClusterCentroids();
	orderClusters(false);
	colorClusters();

	orderProteins(OrderBy::CLUSTERING);

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

	orderProteins(OrderBy::HIERARCHY);

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
		auto useChildrenInstead =
		        std::any_of(current.children.begin(), current.children.end(),
		                    [lowBound] (auto c) { return c >= lowBound; });
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
		for (auto c : current.children)
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
		target[i] = {name, {}, 0, {}};
		flood(i, i);
	}

	pruneClusters();

	computeClusterCentroids();
	orderClusters(true);
	colorClusters();

	emit newClustering();
}

void Dataset::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
	colorClusters();
}

void Dataset::pruneClusters()
{
	QWriteLocker _(&l);

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

void Dataset::computeClusterCentroids()
{
	QWriteLocker _(&l);

	for (auto &c: d.clustering) {
		c.second.mode = std::vector<double>((size_t)d.dimensions.size(), 0.);
	}

	for (unsigned i = 0; i < d.proteins.size(); ++i) {
		for (auto ci : d.proteins[i].memberOf)
			cv::add(d.clustering[ci].mode, d.features[(int)i], d.clustering[ci].mode);
	}

	for (auto &c: d.clustering) {
		auto scale = 1./c.second.size;
		auto &m = c.second.mode;
		std::for_each(m.begin(), m.end(), [scale] (double &e) { e *= scale; });
	}
}

void Dataset::orderClusters(bool genericNames)
{
	QWriteLocker _(&l);

	std::multimap<std::pair<int, QString>, unsigned> clustersOrdered;
	if (genericNames) {
		for (auto &c : d.clustering) // insert ordered by size, desc; name asc
			clustersOrdered.insert({{-c.second.size, c.second.name}, c.first});
	} else {
		for (auto &c : d.clustering) // insert ordered by name
			clustersOrdered.insert({{0, c.second.name}, c.first});
	}

	d.clusterOrder.clear();
	for (auto i : clustersOrdered)
		d.clusterOrder.push_back(i.second);
}

void Dataset::colorClusters()
{
	QWriteLocker _(&l);

	for (unsigned i = 0; i < d.clusterOrder.size(); ++i) {
		d.clustering[d.clusterOrder[i]].color = colorset[(int)i % colorset.size()];
	}
}

void Dataset::orderProteins(OrderBy by)
{
	QWriteLocker _(&l);

	auto &target = d.proteinOrder;
	target.clear();

	auto byName = [this] (auto a, auto b) {
		return d.proteins[a].name < d.proteins[b].name;
	};

	/* order based on hierarchy */
	if (by == OrderBy::HIERARCHY) {
		std::function<void(unsigned)> collect;
		collect = [&] (unsigned hIndex) {
			auto &current = d.hierarchy[hIndex];
			if (current.protein >= 0)
				d.proteinOrder.push_back((unsigned)current.protein);
			for (auto c : current.children)
				collect(c);
		};
		collect(d.hierarchy.size()-1);
		return;
	}

	/* order based on ordered clusters */
	if (by == OrderBy::CLUSTERING) {

		// ensure that each protein appears only once
		std::unordered_set<unsigned> seen;
		for (auto ci : d.clusterOrder) {
			// assemble all affected proteins, and their spread from cluster core
			std::vector<std::pair<unsigned, double>> members;
			for (unsigned i = 0; i < d.proteins.size(); ++i) {
				if (seen.count(i))
					continue; // protein was part of bigger cluster
				if (d.proteins[i].memberOf.count(ci)) {
					double dist = cv::norm(d.features[(int)i], d.clustering[ci].mode, cv::NORM_L2SQR);
					members.push_back({i, dist});
					seen.insert(i);
				}
			}
			// sort by distance to mode/centroid
			std::sort(members.begin(), members.end(), [] (auto a, auto b) {
				return a.second < b.second;
			});
			// now append to global list
			std::transform(members.begin(), members.end(), std::back_inserter(target),
			               [] (const auto &i) { return i.first; });
		}

		// add all proteins not covered yet
		std::vector<unsigned> missing;
		for (unsigned i = 0; i < d.proteins.size(); ++i) {
			if (!seen.count(i))
				missing.push_back(i);
		}
		std::sort(missing.begin(), missing.end(), byName);
		target.insert(target.end(), missing.begin(), missing.end());
		return;
	}

	/* replicate file order */
	target.resize(d.proteins.size());
	std::iota(target.begin(), target.end(), 0);

	/* order based on name (some prots have common prefixes) */
	if (by == OrderBy::NAME)
		std::sort(target.begin(), target.end(), byName);
}
