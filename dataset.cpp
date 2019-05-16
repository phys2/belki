#include "dataset.h"
#include "compute/dimred.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QCollator>

#include <tbb/parallel_for_each.h>
#include <unordered_set>

#include <QDebug>

Dataset::Dataset(ProteinDB &proteins)
    : proteins(proteins),
      // start with an empty dataset and make it current
      datasets(1), d(&datasets.front())
{
	qRegisterMetaType<OrderBy>();
	qRegisterMetaType<DatasetConfiguration>();
}

void Dataset::select(unsigned index)
{
	// TODO: this whole method looks extremely suspicious

	if (index == (d - &datasets[0]))
		return;

	QWriteLocker _(&l);
	d = &datasets[index];

	emit selectedDataset();

	for (auto &[i, _] : d->display)
		emit newDisplay(i);

	if (!d->clustering.empty())
		emit newClustering(false);
	if (!d->hierarchy.empty())
		emit newHierarchy(false);
	// TODO: synchronization of GUI order states and the order in the data
}

void Dataset::spawn(const DatasetConfiguration &conf, QString initialDisplay)
{
	const Public &source = datasets[conf.parent];
	Public target;
	target.conf = conf;

	// only carry over dimensions we keep
	for (auto i : conf.bands)
		target.dimensions.append(source.dimensions.at((size_t)i));

	target.protIndex = source.protIndex;
	target.protIds = source.protIds;

	// only carry over features/scores we keep
	auto fill_stripped = [&conf] (const auto &source, auto &target) {
		target.resize(source.size(), std::vector<double>(conf.bands.size()));
		tbb::parallel_for(size_t(0), target.size(), [&] (size_t i) {
			for (size_t x = 0; x < conf.bands.size(); ++x)
				target[i][x] = source[i][conf.bands[x]];
		});
	};

	fill_stripped(source.features, target.features);
	if (source.hasScores()) {
		fill_stripped(source.scores, target.scores);
		target.scoreRange = features::Range(target.scores);

		if (conf.scoreThresh > 0.)
			features::apply_cutoff(target.features, target.scores, conf.scoreThresh);
	}

	target.featureRange = source.featureRange; // note: no adaptive handling yet
	target.featurePoints = pointify(target.features);

	target.clustering = source.clustering;
	target.hierarchy = source.hierarchy;
	target.order = source.order;

	{
		QWriteLocker _(&l);
		datasets.push_back(std::move(target));
		d = &datasets.back();
		emit newDataset(datasets.size() - 1);
	}

	/* also compute displays expected by the user */
	// standard set
	computeDisplays();

	// current display
	if (!initialDisplay.isEmpty() && !d->display.count(initialDisplay))
		computeDisplay(initialDisplay);
}

void Dataset::computeDisplay(const QString& request)
{
	// empty data shouldn't happen but right now can when a file cannot be read completely,
	// in the future this should result in IOError already earlier
	if (d->features.empty())
		return;

	// note: no read lock as we are write thread
	auto result = dimred::compute(request, d->features);

	QWriteLocker _(&l);
	for (auto name : result.keys()) {
		d->display[name] = result[name]; // TODO move

		// TODO: lookup in datasets[d->conf->parent].displays and perform rigid registration

		emit newDisplay(name);
	}
}

void Dataset::computeDisplays()
{
	/* compute PCA displays as a fast starting point */
	if (!d->display.count("PCA 12"))
		computeDisplay("PCA");
}

void Dataset::clearClusters()
{
	QWriteLocker _(&l);
	d->clustering = Clustering(d->protIds.size());

	emit newClustering();
}

void Dataset::computeFAMS()
{
	// empty data shouldn't happen but right now can when a file cannot be read completely,
	// in the future this should result in IOError already earlier
	if (d->features.empty())
		return;

	auto &fams = meanshift.fams;
	qDebug() << "FAMS " << meanshift.k << " vs " << (fams ? fams->config.k : 0);
	if (fams && (fams->config.k == meanshift.k || meanshift.k <= 0))
		return; // already done

	fams.reset(new seg_meanshift::FAMS({
	                                       .k=meanshift.k,
	                                       .pruneMinN = 0, // we use pruneClusters() instead
	                                   }));
	fams->importPoints(d->features, true);
	bool success = fams->prepareFAMS();
	if (!success)
		return;
	fams->selectStartPoints(0., 1);
	success = fams->finishFAMS();
	if (!success)
		return;

	fams->pruneModes();

	Clustering cl(d->protIds.size());
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
	d->clustering = std::move(cl);

	pruneClusters();
	computeClusterCentroids();
	orderClusters(true);
	colorClusters();

	bool reorder = d->order.synchronizing && d->order.reference == OrderBy::CLUSTERING;
	if (reorder)
		orderProteins(OrderBy::CLUSTERING);

	emit newClustering(reorder);
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

bool Dataset::readSource(QTextStream in, const QString &name)
{
	return readScoredSource(in, name); // TODO

	cancelFAMS(); // abort unwanted calculation

	auto header = in.readLine().split("\t");
	header.pop_front(); // first column
	if (header.contains("") || header.removeDuplicates()) {
		emit ioError("Malformed header: Duplicate or empty columns!");
		return false;
	}

	Public target;

	/* fill it up */
	target.dimensions = trimCrap(std::move(header));
	auto len = target.dimensions.size();
	std::set<QString> seen; // names of read proteins
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		if (line.size() < len + 1) {
			emit ioError(QString("Stopped at '%1', incomplete row!").arg(line[0]));
			break; // avoid message flood
		}

		/* setup metadata */
		auto protid = proteins.add(line[0]);
		auto name = proteins.peek()->proteins[protid].name;

		/* check duplicates */
		if (seen.count(name))
			emit ioError(QString("Multiples of protein '%1' found in the dataset!").arg(name));
		seen.insert(name);

		/* read coefficients */
		bool success = true;
		std::vector<double> coeffs((size_t)len);
		for (int i = 0; i < len; ++i) {
			bool ok;
			coeffs[(size_t)i] = line[i+1].toDouble(&ok);
			success = success && ok;
		}
		if (!success) {
			emit ioError(QString("Stopped at protein '%1', malformed row!").arg(name));
			break; // avoid message flood
		}

		/* append */
		target.protIndex[protid] = target.protIds.size();
		target.protIds.push_back(protid);
		target.features.push_back(std::move(coeffs));
	}
	// ensure clustering is properly initialized if accessed
	target.clustering = Clustering(target.protIds.size());

	//qDebug() << "read" << d.features.size() << "rows with" << len << "columns";
	if (target.features.empty() || len == 0) {
		emit ioError(QString("Could not read any valid data rows from file!"));
		return false;
	}

	features::Range range(target.features);
	/* normalize, if needed */
	if (range.min < 0 || range.max > 1) { // simple heuristic to auto-normalize
		emit ioError(QString("Values outside expected range (instead [%1, %2])."
		                     "<br>Normalizing to [0, 1].").arg(range.min).arg(range.max));
		// cut off negative values
		range.min = 0.;
		auto scale = 1. / (range.max - range.min);
		for (auto &v : target.features) {
			std::for_each(v.begin(), v.end(), [min=range.min, scale] (double &e) {
				e = std::max(e - min, 0.) * scale;
			});
		}
	}
	target.featureRange = {0., 1.}; // TODO: we enforced normalization (make config.)

	/* pre-cache features as QPoints for plotting */
	target.featurePoints = pointify(target.features);

	/* time to flip */ // TODO: create new entry in datasets instead
	QWriteLocker _(&l);
	*d = std::move(target);

	/* calculate initial order */
	orderProteins(d->order.reference);

	emit newDataset(0); // TODO
	return true;
}

bool Dataset::readScoredSource(QTextStream &in, const QString &name)
{
	cancelFAMS(); // abort unwanted calculation

	auto header = in.readLine().split("\t");
	if (header.contains("") || header.removeDuplicates()) {
		emit ioError("Malformed header: Duplicate or empty columns!");
		return false;
	}
	if (header.size() == 0 || header.first() != "Protein") {
		emit ioError("Could not parse file!<p>The first column must contain protein names.</p>");
		return false;
	}
	int nameCol = header.indexOf("Pair");
	int featureCol = header.indexOf("Dist");
	int scoreCol = header.indexOf("Score");
	if (nameCol == -1 || featureCol == -1 || scoreCol == -1) {
		emit ioError("Could not parse file!<p>Not all necessary columns found.</p>");
		return false;
	}

	Public target;
	target.conf.name = name; // TODO

	/* fill it up */
	std::map<QString, unsigned> dimensions;
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		if (line.size() < header.size()) {
			emit ioError(QString("Stopped at '%1', incomplete row!").arg(line[0]));
			break; // avoid message flood
		}

		/* setup metadata */
		auto protid = proteins.add(line[0]);

		/* determine protein index */
		size_t row; // the protein id we are altering
		auto index = target.protIndex.find(protid);
		if (index == target.protIndex.end()) {
			target.protIds.push_back(protid);
			auto len = target.protIds.size();
			target.features.resize(len, std::vector<double>(dimensions.size()));
			target.scores.resize(len, std::vector<double>(dimensions.size()));
			row = len - 1;
			target.protIndex[protid] = row;
		} else {
			row = index->second;
		}

		/* determine dimension index */
		size_t col; // the dimension we are altering
		auto dIndex = dimensions.find(line[nameCol]);
		if (dIndex == dimensions.end()) {
			target.dimensions.append(line[nameCol]);
			auto len = (size_t)target.dimensions.size();
			for (auto &i : target.features)
				i.resize(len);
			for (auto &i : target.scores)
				i.resize(len);
			col = len - 1;
			dimensions[line[nameCol]] = col;
		} else {
			col = dIndex->second;
		}

		/* read coefficients */
		bool success = true;
		bool ok;
		double feat, score;
		feat = line[featureCol].toDouble(&ok);
		success = success && ok;
		score = line[scoreCol].toDouble(&ok);
		if (!success) {
			auto name = proteins.peek()->proteins[protid].name;
			auto err = QString("Stopped at protein '%1', malformed row!").arg(name);
			emit ioError(err);
			break; // avoid message flood
		}

		/* fill-in features and scores */
		target.features[row][col] = feat;
		target.scores[row][col] = std::max(score, 0.); // TODO temporary clipping
	}

	// ensure clustering is properly initialized if accessed
	target.clustering = Clustering(target.protIds.size());

	if (target.features.empty() || target.dimensions.empty()) {
		emit ioError(QString("Could not read any valid data rows from file!"));
		return false;
	}

	/* setup ranges */
	features::Range range(target.features);
	/* normalize, if needed */
	if (range.min < 0 || range.max > 1) { // simple heuristic to auto-normalize
		emit ioError(QString("Values outside expected range (instead [%1, %2])."
		                     "<br>Normalizing to [0, 1].").arg(range.min).arg(range.max));
		// cut off negative values
		range.min = 0.;
		auto scale = 1. / (range.max - range.min);
		for (auto &v : target.features) {
			std::for_each(v.begin(), v.end(), [min=range.min, scale] (double &e) {
				e = std::max(e - min, 0.) * scale;
			});
		}
	}
	target.featureRange = {0., 1.}; // TODO: we enforced normalization (make config.)
	target.scoreRange = features::Range(target.scores);

	/* pre-cache features as QPoints for plotting */
	target.featurePoints = pointify(target.features);

	/* time to flip/add */
	QWriteLocker _(&l);
	if (d->dimensions.empty()) { // replace
		*d = std::move(target);
	} else { // append
		datasets.push_back(std::move(target));
		select(datasets.size() - 1);
	}

	/* calculate initial order */
	orderProteins(d->order.reference);

	emit newDataset(current()); // TODO
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

	if (data.size() != (int)d->features.size())
		return ioError(QString("Display %1 length does not match source length!").arg(name));

	d->display[name] = std::move(data);
	emit newDisplay(name);
}

QByteArray Dataset::writeDisplay(const QString &name)
{
	// note: no read lock as we are write thread
	QByteArray ret;
	QTextStream out(&ret, QIODevice::WriteOnly);
	auto &data = d->display.at(name);
	for (auto it = data.constBegin(); it != data.constEnd(); ++it)
		out << it->x() << "\t" << it->y() << endl;

	return ret;
}

// TODO: move to storage, only deals with ProteinDB
bool Dataset::readDescriptions(const QByteArray &tsv)
{
	QTextStream in(tsv);
	auto header = in.readLine().split("\t");
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() != 2 || !header[0].contains(re)) {
		emit ioError("Could not parse file!<p>The first column must contain protein names, second descriptions.</p>");
		return false;
	}

	/* ensure we have data to annotate */
	if (proteins.peek()->proteins.empty()) {
		emit ioError("Please load proteins first!");
		return false;
	}

	/* fill-in descriptions */
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() < 2)
			continue;

		bool success = proteins.addDescription(line[0], line[1]);
		if (!success)
			qDebug() << "Ignored" << line[0] << "(unknown)";
	}
	return true;
}

bool Dataset::readAnnotations(const QByteArray &tsv)
{
	/* ensure we have data to annotate */
	if (d->protIds.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
	}

	Clustering cl(d->protIds.size());

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
					auto p = d->protIndex.at(proteins.peek()->find(name));
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
				auto p = d->protIndex.at(proteins.peek()->find(name));
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
	d->clustering = std::move(cl);

	computeClusterCentroids();
	orderClusters(false);
	colorClusters();

	bool reorder = d->order.synchronizing && d->order.reference == OrderBy::CLUSTERING;
	if (reorder)
		orderProteins(OrderBy::CLUSTERING);

	emit newClustering(reorder);
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
	if (d->protIds.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
	}

	auto nodes = root["data"].toObject()["nodes"].toObject();
	auto &container = d->hierarchy;

	// some preparation. we can expect at least as much clusters:
	container.reserve(2 * d->protIds.size()); // binary tree
	container.resize(d->protIds.size()); // cluster-per-protein

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
				c.protein = (int)d->protIndex.at(proteins.peek()->find(name));
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

	/* re-order for both hierarchy or clustering being chosen as reference.
	 * we will not re-order in calculatePartition() */
	bool reorder = d->order.synchronizing;
	reorder = reorder && (d->order.reference == OrderBy::HIERARCHY ||
	                      d->order.reference == OrderBy::CLUSTERING);
	if (reorder)
		orderProteins(OrderBy::HIERARCHY);

	emit newHierarchy(reorder);
	return true;
}

void Dataset::calculatePartition(unsigned granularity)
{
	auto &hrclusters = d->hierarchy;

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
	Clustering cl(d->protIds.size());

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
	d->clustering = std::move(cl);

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

void Dataset::changeOrder(OrderBy reference, bool synchronize)
{
	QWriteLocker _(&l);
	d->order.synchronizing = synchronize;
	if (d->order.reference == reference)
		return; // nothing to do

	d->order.reference = reference; // save preference for future changes
	orderProteins(reference);

	emit newOrder();
}

void Dataset::pruneClusters()
{
	QWriteLocker _(&l);

	/* defragment clusters (un-assign and remove small clusters) */
	// TODO: make configurable; instead keep X biggest clusters?
	auto minSize = unsigned(0.005f * (float)d->protIds.size());
	auto &c = d->clustering.clusters;
	auto it = c.begin();
	while (it != c.end()) {
		if (it->second.size < minSize) {
			for (auto &m : d->clustering.memberships)
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

	auto &cl = d->clustering;
	for (auto& [_, c]: cl.clusters)
		c.mode = std::vector<double>((size_t)d->dimensions.size(), 0.);

	for (unsigned i = 0; i < cl.memberships.size(); ++i) {
		for (auto ci : cl.memberships[i])
			cv::add(cl.clusters[ci].mode, d->features[i], cl.clusters[ci].mode);
	}

	for (auto& [_, c] : cl.clusters) {
		auto scale = 1./c.size;
		auto &m = c.mode;
		std::for_each(m.begin(), m.end(), [scale] (double &e) { e *= scale; });
	}
}

void Dataset::orderClusters(bool genericNames)
{
	auto &cl = d->clustering.clusters;
	std::vector<unsigned> target;
	for (auto & [i, _] : cl)
		target.push_back(i);

	QCollator col;
	col.setNumericMode(true);
	if (col("a", "a")) {
		qDebug() << "Falling back to non-numeric sorting.";
		col.setNumericMode(false);
	}

	col.setCaseSensitivity(Qt::CaseInsensitive);
	std::function<bool(unsigned,unsigned)> byName = [&] (auto a, auto b) {
		return col(cl[a].name, cl[b].name);
	};
	auto bySizeName = [&] (auto a, auto b) {
		if (cl[a].size == cl[b].size)
			return byName(a, b);
		return cl[a].size > cl[b].size;
	};

	std::sort(target.begin(), target.end(), genericNames ? bySizeName : byName);

	QWriteLocker _(&l);
	d->clustering.order = std::move(target);
}

void Dataset::colorClusters()
{
	QWriteLocker _(&l);

	auto &cl = d->clustering;
	for (unsigned i = 0; i < cl.clusters.size(); ++i) {
		cl.clusters[cl.order[i]].color = colorset[(int)i % colorset.size()];
	}
}

void Dataset::orderProteins(OrderBy reference)
{
	/* initialize replacement with current configuration */
	// note that our argument 'reference' might _not_ be the configured one
	Order target{d->order.reference, d->order.synchronizing, false, {}, {}};

	/* use reasonable fallbacks */
	if (reference == OrderBy::CLUSTERING && d->clustering.empty()) {
		reference = OrderBy::HIERARCHY;
		target.fallback = true;
	}
	if (reference == OrderBy::HIERARCHY && d->hierarchy.empty()) {
		reference = OrderBy::NAME;
		target.fallback = true;
	}

	auto p = proteins.peek();
	auto &index = target.index;

	auto byName = [this,&p] (auto a, auto b) {
		return d->lookup(p, a).name < d->lookup(p, b).name;
	};

	switch (reference) {
	/* order based on hierarchy */
	case OrderBy::HIERARCHY: {
		std::function<void(unsigned)> collect;
		collect = [&] (unsigned hIndex) {
			auto &current = d->hierarchy[hIndex];
			if (current.protein >= 0)
				index.push_back((unsigned)current.protein);
			for (auto c : current.children)
				collect(c);
		};
		collect(d->hierarchy.size()-1);
		break;
	}
	/* order based on ordered clusters */
	case OrderBy::CLUSTERING: {

		// ensure that each protein appears only once
		std::unordered_set<unsigned> seen;
		for (auto ci : d->clustering.order) {
			// assemble all affected proteins, and their spread from cluster core
			std::vector<std::pair<unsigned, double>> members;
			for (unsigned i = 0; i < d->protIds.size(); ++i) {
				if (seen.count(i))
					continue; // protein was part of bigger cluster
				if (d->clustering.memberships[i].count(ci)) {
					double dist = cv::norm(d->features[i],
					        d->clustering.clusters[ci].mode, cv::NORM_L2SQR);
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
		for (unsigned i = 0; i < d->protIds.size(); ++i) {
			if (!seen.count(i))
				missing.push_back(i);
		}
		std::sort(missing.begin(), missing.end(), byName);
		index.insert(index.end(), missing.begin(), missing.end());
		break;
	}
	default:
		/* replicate file order */
		index.resize(d->protIds.size());
		std::iota(index.begin(), index.end(), 0);

		/* order based on name (some prots have common prefixes) */
		if (reference == OrderBy::NAME)
			std::sort(index.begin(), index.end(), byName);
	}

	/* now fill the back-references */
	target.rankOf.resize(index.size());
	for (size_t i = 0; i < index.size(); ++i)
		target.rankOf[index[i]] = i;

	QWriteLocker _(&l);
	d->order = target;
}

std::vector<QVector<QPointF>> Dataset::pointify(const std::vector<std::vector<double> > &in)
{
	std::vector<QVector<QPointF>> ret;
	for (auto f : in) {
		QVector<QPointF> points(f.size());
		for (size_t i = 0; i < f.size(); ++i)
			points[i] = {(qreal)i, f[i]};
		ret.push_back(std::move(points));
	}
	return ret;
}

QStringList Dataset::trimCrap(QStringList values)
{
	if (values.empty())
		return values;

	/* remove custom shit in our data */
	QString match("[A-Z]{2}20\\d{6}.*?\\([A-Z]{2}(?:-[A-Z]{2})?\\)_(.*?)_\\(?(?:band|o|u)(?:\\+(?:band|o|u))+\\)?_.*?$");
	values.replaceInStrings(QRegularExpression(match), "\\1");

	/* remove common prefix & suffix */
	QString reference = values.front();
	int front = reference.size(), back = reference.size();
	for (auto it = ++values.cbegin(); it != values.cend(); ++it) {
		front = std::min(front, it->size());
		back = std::min(back, it->size());
		for (int i = 0; i < front; ++i) {
			if (it->at(i) != reference[i]) {
				front = i;
				break;
			}
		}
		for (int i = 0; i < back; ++i) {
			if (it->at(it->size()-1 - i) != reference[reference.size()-1 - i]) {
				back = i;
				break;
			}
		}
	}
	match = QString("^.{%1}(.*?).{%2}$").arg(front).arg(back);
	values.replaceInStrings(QRegularExpression(match), "\\1");

	return values;
}

const std::map<Dataset::OrderBy, QString> Dataset::availableOrders()
{
	return {
		{OrderBy::FILE, "Position in File"},
		{OrderBy::NAME, "Protein Name"},
		{OrderBy::HIERARCHY, "Hierarchy"},
		{OrderBy::CLUSTERING, "Cluster/Annotations"},
	};
}
