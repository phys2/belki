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

	Clustering cl(d.proteins.size());
	auto modes = meanshift.fams->exportModes();
	for (unsigned i = 0; i < modes.size(); ++i)
		cl.clusters[i] = {QString("Cluster #%1").arg(i+1), {}, 0, modes[i]};

	auto &index = meanshift.fams->getModePerPoint();
	for (unsigned i = 0; i < index.size(); ++i) {
		auto m = (unsigned)index[i];
		cl.memberships[i] = {m};
		cl.clusters[m].size++;
	}

	QWriteLocker _(&l); // makes sense to keep the lock until everything is done
	d.clustering = std::move(cl);

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
	// ensure clustering is properly initialized if accessed
	d.clustering = Clustering(d.proteins.size());

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
		//maxVal = std::log1p(maxVal);
		//minVal = std::log1p(minVal);
		auto scale = 1. / (maxVal - minVal);
		for (int i = 0; i < d.features.size(); ++i) {
			auto &v = d.features[i];
			std::for_each(v.begin(), v.end(), [minVal, scale] (double &e) {
				//e = std::log1p(e);
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

	Clustering cl(d.proteins.size());

	QTextStream in(tsv);
	// we use SkipEmptyParts for chomping at the end, but dangerous…
	auto header = in.readLine().split("\t", QString::SkipEmptyParts);
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() == 2 && header[1].contains("Members")) {
		/* expect name + list of proteins per-cluster per-line */

		/* build new clusters */
		unsigned clusterIndex = 0;
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t");
			if (line.size() < 2)
				continue;

			cl.clusters[clusterIndex] = {line[0], {}, 0, {}};
			line.removeFirst();

			for (auto &name : qAsConst(line)) {
				try {
					auto p = d.find(name);
					cl.memberships[p].insert(clusterIndex);
					cl.clusters[clusterIndex].size++;
				} catch (std::out_of_range&) {
					qDebug() << "Ignored" << name << "(unknown)";
				}
			}

			clusterIndex++;
		}
	} else if (header.size() > 1 && header[0].contains(re)) {
		/* expect matrix layout, first column protein names */
		header.removeFirst();

		/* setup clusters */
		cl.clusters.reserve((unsigned)header.size());
		for (auto i = 0; i < header.size(); ++i)
			cl.clusters[(unsigned)i] = {header[i], {}, 0, {}};

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
					cl.memberships[p].insert((unsigned)i);
					cl.clusters[(unsigned)i].size++;
				}
			} catch (std::out_of_range&) {
				qDebug() << "Ignored" << name << "(unknown)";
			}
		}
	} else {
		emit ioError("Could not parse file!<p>The first column must contain protein or group names.</p>");
		return false;
	}

	QWriteLocker _(&l); // makes sense to keep the lock until everything is done
	d.clustering = std::move(cl);

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

		/* back-association */
		if (node.contains("parent"))
			c.parent = (unsigned)node["parent"].toInt();
	}

	orderProteins(OrderBy::HIERARCHY);

	emit newHierarchy();
	return true;
}

void Dataset::calculatePartition(unsigned granularity)
{
	auto &hrclusters = d.hierarchy;

	granularity = std::min(granularity, (unsigned)hrclusters.size());
	unsigned lowBound = hrclusters.size() - granularity - 1;

	/* determine clusters to be displayed */
	std::set<unsigned> candidates;
	// we use the fact that input is sorted by distance, ascending
	for (auto i = lowBound; i < hrclusters.size(); ++i) {
		auto &current = hrclusters[i];

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

	/* set up clustering based on candidates */
	Clustering cl(d.proteins.size());

	// helper to recursively assign all proteins to clusters
	std::function<void(unsigned, unsigned)> flood;
	flood = [&] (unsigned hIndex, unsigned cIndex) {
		auto &current = hrclusters[hIndex];
		if (current.protein >= 0) {
			cl.memberships[(unsigned)current.protein] = {cIndex};
			cl.clusters[cIndex].size++;
		}
		for (auto c : current.children)
			flood(c, cIndex);
	};

	cl.clusters.reserve(candidates.size());
	for (auto i : candidates) {
		auto name = QString("Cluster #%1").arg(hrclusters.size() - i);
		// use index in hierarchy as cluster index as well
		cl.clusters[i] = {name, {}, 0, {}};
		flood(i, i);
	}

	QWriteLocker _(&l); // makes sense to keep the lock until everything is done
	d.clustering = std::move(cl);

	pruneClusters();

	computeClusterCentroids();
	orderClusters(true);
	colorClusters();
	// note: we do not re-order proteins as the hierarchy maintains precedence

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
	auto &c = d.clustering.clusters;
	auto it = c.begin();
	while (it != c.end()) {
		if (it->second.size < minSize) {
			for (auto &m : d.clustering.memberships)
				m.erase(it->first);
			it = c.erase(it);
		} else {
			it++;
		}
	}
}

void Dataset::computeClusterCentroids()
{
	QWriteLocker __(&l);

	auto &cl = d.clustering;
	for (auto& [_, c]: cl.clusters)
		c.mode = std::vector<double>((size_t)d.dimensions.size(), 0.);

	for (unsigned i = 0; i < cl.memberships.size(); ++i) {
		for (auto ci : cl.memberships[i])
			cv::add(cl.clusters[ci].mode, d.features[(int)i], cl.clusters[ci].mode);
	}

	for (auto& [_, c] : cl.clusters) {
		auto scale = 1./c.size;
		auto &m = c.mode;
		std::for_each(m.begin(), m.end(), [scale] (double &e) { e *= scale; });
	}
}

void Dataset::orderClusters(bool genericNames)
{
	QWriteLocker _(&l);

	auto &cl = d.clustering;
	std::multimap<std::pair<int, QString>, unsigned> clustersOrdered;
	if (genericNames) {
		for (auto& [i, c] : cl.clusters) // insert ordered by size, desc; name asc
			clustersOrdered.insert({{-c.size, c.name}, i});
	} else {
		for (auto& [i, c] : cl.clusters) // insert ordered by name
			clustersOrdered.insert({{0, c.name}, i});
	}

	cl.order.clear();
	for (auto& [_, c] : clustersOrdered)
		cl.order.push_back(c);
}

void Dataset::colorClusters()
{
	QWriteLocker _(&l);

	auto &cl = d.clustering;
	for (unsigned i = 0; i < cl.clusters.size(); ++i) {
		cl.clusters[cl.order[i]].color = colorset[(int)i % colorset.size()];
	}
}

void Dataset::orderProteins(OrderBy by)
{
	Order target;
	auto &index = target.index;

	auto byName = [this] (auto a, auto b) {
		return d.proteins[a].name < d.proteins[b].name;
	};

	switch (by) {
	/* order based on hierarchy */
	case OrderBy::HIERARCHY: {
		std::function<void(unsigned)> collect;
		collect = [&] (unsigned hIndex) {
			auto &current = d.hierarchy[hIndex];
			if (current.protein >= 0)
				index.push_back((unsigned)current.protein);
			for (auto c : current.children)
				collect(c);
		};
		collect(d.hierarchy.size()-1);
		break;
	}
	/* order based on ordered clusters */
	case OrderBy::CLUSTERING: {

		// ensure that each protein appears only once
		std::unordered_set<unsigned> seen;
		for (auto ci : d.clustering.order) {
			// assemble all affected proteins, and their spread from cluster core
			std::vector<std::pair<unsigned, double>> members;
			for (unsigned i = 0; i < d.proteins.size(); ++i) {
				if (seen.count(i))
					continue; // protein was part of bigger cluster
				if (d.clustering.memberships[i].count(ci)) {
					double dist = cv::norm(d.features[(int)i],
					        d.clustering.clusters[ci].mode, cv::NORM_L2SQR);
					members.push_back({i, dist});
					seen.insert(i);
				}
			}
			// sort by distance to mode/centroid
			std::sort(members.begin(), members.end(), [] (auto a, auto b) {
				return a.second < b.second;
			});
			// now append to global list
			std::transform(members.begin(), members.end(), std::back_inserter(index),
			               [] (const auto &i) { return i.first; });
		}

		// add all proteins not covered yet
		std::vector<unsigned> missing;
		for (unsigned i = 0; i < d.proteins.size(); ++i) {
			if (!seen.count(i))
				missing.push_back(i);
		}
		std::sort(missing.begin(), missing.end(), byName);
		index.insert(index.end(), missing.begin(), missing.end());
		break;
	}
	default: {
		/* replicate file order */
		index.resize(d.proteins.size());
		std::iota(index.begin(), index.end(), 0);

		/* order based on name (some prots have common prefixes) */
		if (by == OrderBy::NAME)
			std::sort(index.begin(), index.end(), byName);
	}
	}

	/* now fill the back-references */
	target.rankOf.resize(index.size());
	for (size_t i = 0; i < index.size(); ++i)
		target.rankOf[index[i]] = i;

	QWriteLocker _(&l);
	d.order = target;
}
