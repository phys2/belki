#include "dataset.h"
#include "compute/features.h"
#include "compute/dimred.h"

#include <QDataStream>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonArray>
#include <QCollator>
#include <QRegularExpression>

#include <tbb/parallel_for_each.h>
#include <unordered_set>

#include <iostream>

Dataset::Dataset(ProteinDB &proteins, DatasetConfiguration conf)
    : proteins(proteins), conf(conf)
{
	qRegisterMetaType<DatasetConfiguration>();
	qRegisterMetaType<Touched>("Touched");
	qRegisterMetaType<OrderBy>();
	qRegisterMetaType<Ptr>("DataPtr");
	qRegisterMetaType<ConstPtr>("ConstDataPtr");
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

template<>
View<Dataset::Base> Dataset::peek() const { return View(b); }
template<>
View<Dataset::Representation> Dataset::peek() const { return View(r); }
template<>
View<Dataset::Structure> Dataset::peek() const { return View(s); }
template<>
View<Dataset::Proteins> Dataset::peek() const { return proteins.peek(); }

void Dataset::spawn(Features::Ptr in)
{
	b.dimensions = std::move(in->dimensions);
	b.protIds = std::move(in->protIds);
	b.protIndex = std::move(in->protIndex);
	b.features = std::move(in->features);
	b.featureRange = std::move(in->featureRange);
	b.scores = std::move(in->scores);
	b.scoreRange = std::move(in->scoreRange);

	/* pre-cache features as QPoints for plotting */
	b.featurePoints = features::pointify(b.features);

	// ensure clustering is properly initialized if accessed
	s.clustering = Clustering(b.protIds.size());

	// calculate initial order
	orderProteins(s.order.reference);
}

void Dataset::spawn(ConstPtr srcholder)
{
	auto bIn = srcholder->peek<Base>();

	// only carry over dimensions we keep
	for (auto i : conf.bands)
		b.dimensions.append(bIn->dimensions.at((size_t)i));

	b.protIndex = bIn->protIndex;
	b.protIds = bIn->protIds;

	// only carry over features/scores we keep
	auto fill_stripped = [this] (const auto &source, auto &target) {
		target.resize(source.size(), std::vector<double>(conf.bands.size()));
		tbb::parallel_for(size_t(0), target.size(), [&] (size_t i) {
			for (size_t x = 0; x < conf.bands.size(); ++x)
				target[i][x] = source[i][conf.bands[x]];
		});
	};

	fill_stripped(bIn->features, b.features);
	if (bIn->hasScores()) {
		fill_stripped(bIn->scores, b.scores);
		b.scoreRange = features::range_of(b.scores);

		if (conf.scoreThresh > 0.)
			features::apply_cutoff(b.features, b.scores, conf.scoreThresh);
	}

	b.featureRange = bIn->featureRange; // note: no adaptive handling yet
	b.featurePoints = features::pointify(b.features);

	// also copy structure
	auto sIn = srcholder->peek<Structure>();
	s.hierarchy = sIn->hierarchy;
	s.clustering = sIn->clustering;
	s.order = sIn->order;
}

void Dataset::computeDisplay(const QString& request)
{
	b.l.lockForRead();
	auto result = dimred::compute(request, b.features);
	b.l.unlock();

	r.l.lockForWrite();
	for (auto name : result.keys()) {
		r.display[name] = result[name]; // TODO move
		// TODO: lookup in datasets[d->conf->parent].displays and perform rigid registration
	}
	r.l.unlock();

	emit update(Touch::DISPLAY);
}

void Dataset::computeDisplays()
{
	/* compute PCA displays as a fast starting point */
	r.l.lockForWrite(); // proactive write lock, avoid gap that may lead to double computation
	if (!r.display.count("PCA 12"))
		computeDisplay("PCA");
	r.l.unlock();
}

void Dataset::clearClusters()
{
	s.l.lockForWrite();
	s.clustering = Clustering(peek<Base>()->protIds.size());
	s.l.unlock();

	emit update(Touch::CLUSTERS);
}

void Dataset::computeFAMS()
{
	QWriteLocker _m(&meanshift.l);
	auto &fams = meanshift.fams;
	if (fams && (fams->config.k == meanshift.k || meanshift.k <= 0))
		return; // already done

	fams.reset(new seg_meanshift::FAMS({
	                                       .k=meanshift.k,
	                                       .pruneMinN = 0, // we use pruneClusters() instead
	                                   }));

	auto d = peek<Base>();
	fams->importPoints(d->features, true); // scales vectors
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
	_m.unlock();

	swapClustering(cl, true, true, true);
}

void Dataset::changeFAMS(float k)
{
	// no lock, we _want_ to interfere
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

bool Dataset::readDisplay(const QString& name, QTextStream &in)
{
	QVector<QPointF> data;
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.size() != 2) {
			ioError(QString("Input malformed at line %2 in display %1").arg(name, data.size()+1));
			return false;
		}

		data.push_back({line[0].toDouble(), line[1].toDouble()});
	}

	if (data.size() != (int)peek<Base>()->features.size()) {
		ioError(QString("Display %1 length does not match source length!").arg(name));
		return false;
	}

	r.l.lockForWrite();
	r.display[name] = std::move(data);
	r.l.unlock();

	emit update(Touch::DISPLAY);
	return true;
}

void Dataset::swapClustering(Dataset::Clustering &cl, bool genericNames, bool pruneCl, bool reorderProts)
{
	s.l.lockForWrite(); // guarding lock until finished (consistent state)

	s.clustering = std::move(cl);
	if (pruneCl)
		pruneClusters();
	computeClusterCentroids();
	orderClusters(genericNames);
	colorClusters();

	Touched touched = Touch::CLUSTERS;
	if (reorderProts && s.order.synchronizing && s.order.reference == OrderBy::CLUSTERING) {
		orderProteins(OrderBy::CLUSTERING);
		touched |= Touch::ORDER;
	}

	s.l.unlock();
	emit update(touched);
}

QByteArray Dataset::exportDisplay(const QString &name) const
{
	QByteArray ret;
	QTextStream out(&ret, QIODevice::WriteOnly);
	auto in = peek<Representation>();
	// TODO: check if display exists
	auto &data = in->display.at(name);
	for (auto it = data.constBegin(); it != data.constEnd(); ++it)
		out << it->x() << "\t" << it->y() << endl;

	return ret;
}

bool Dataset::readAnnotations(QTextStream &in)
{
	auto d = peek<Base>();
	/* ensure we have data to annotate */
	if (d->protIds.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
	}

	Clustering cl(d->protIds.size());

	// we use SkipEmptyParts for chomping at the end, but dangerous…
	auto header = in.readLine().split("\t", QString::SkipEmptyParts);
	QRegularExpression re("^Protein$|Name$", QRegularExpression::CaseInsensitiveOption);
	if (header.size() == 2 && header[1].contains("Members")) {
		/* expect name + list of proteins per-cluster per-line */

		/* build new clusters */
		auto p = proteins.peek();
		unsigned clusterIndex = 0;
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t");
			if (line.size() < 2)
				continue;

			cl.clusters[clusterIndex] = {line[0], {}, 0, {}};
			line.removeFirst();

			for (auto &name : qAsConst(line)) {
				try {
					auto prot = d->protIndex.at(p->find(name));
					cl.memberships[prot].insert(clusterIndex);
					cl.clusters[clusterIndex].size++;
				} catch (std::out_of_range&) {}
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
		auto p = proteins.peek();
		while (!in.atEnd()) {
			auto line = in.readLine().split("\t");
			if (line.size() < 2)
				continue;

			auto name = line[0];
			line.removeFirst();
			try {
				auto prot = d->protIndex.at(p->find(name));
				for (auto i = 0; i < header.size(); ++i) { // run over header to only allow valid columns
					if (line[i].isEmpty() || line[i].contains(QRegularExpression("^\\s*$")))
						continue;
					cl.memberships[prot].insert((unsigned)i);
					cl.clusters[(unsigned)i].size++;
				}
			} catch (std::out_of_range&) {}
		}
	} else {
		emit ioError("Could not parse file!<p>The first column must contain protein or group names.</p>");
		return false;
	}

	swapClustering(cl, false, false, true);
	return true;
}

bool Dataset::readHierarchy(const QJsonObject &root)
{
	auto d = peek<Base>();

	/* ensure we have data to annotate */
	if (d->protIds.empty()) {
		emit ioError("Please load protein profiles first!");
		return false;
	}

	auto nodes = root["data"].toObject()["nodes"].toObject();

	std::vector<HrCluster> container;
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

	s.l.lockForWrite(); // guarding lock until finished (consistent state)

	s.hierarchy = std::move(container);

	Touched touched = Touch::HIERARCHY;
	/* re-order for both hierarchy or clustering being chosen as reference.
	 * we will not re-order in calculatePartition() */
	if ((s.order.synchronizing) && (s.order.reference == OrderBy::HIERARCHY ||
	                                s.order.reference == OrderBy::CLUSTERING)) {
		orderProteins(OrderBy::HIERARCHY);
		touched |= Touch::ORDER;
	}

	s.l.unlock();

	emit update(touched);
	return true;
}

void Dataset::calculatePartition(unsigned granularity)
{
	s.l.lockForRead(); // don't use RAI as we will switch to W-lock later
	auto &hrclusters = s.hierarchy;

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
	Clustering cl(peek<Base>()->protIds.size());

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
	s.l.unlock();

	// do not reorder clusters when based on hierarchy
	swapClustering(cl, true, true, false);
}

void Dataset::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
	colorClusters();
}

void Dataset::changeOrder(OrderBy reference, bool synchronize)
{
	QWriteLocker _s(&s.l);
	s.order.synchronizing = synchronize;
	if (s.order.reference == reference)
		return; // nothing to do

	s.order.reference = reference; // save preference for future changes
	orderProteins(reference);

	_s.unlock();
	emit update(Touch::ORDER);
}

void Dataset::pruneClusters()
{
	/* Note: caller has locked s for us for writing */

	/* defragment clusters (un-assign and remove small clusters) */
	// TODO: make configurable; instead keep X biggest clusters?
	auto minSize = unsigned(0.005f * (float)peek<Base>()->protIds.size());
	auto &c = s.clustering.clusters;
	for (auto it = c.begin(); it != c.end();) {
		if (it->second.size < minSize) {
			for (auto &m : s.clustering.memberships)
				m.erase(it->first);
			it = c.erase(it);
		} else {
			it++;
		}
	}
}

void Dataset::computeClusterCentroids()
{
	/* Note: caller has locked s for us for writing */

	auto d = peek<Base>();
	QWriteLocker _s(&s.l);

	auto &cl = s.clustering;
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
	/* Note: caller has locked s for us for writing */

	auto &cl = s.clustering.clusters;
	std::vector<unsigned> target;
	for (auto & [i, _] : cl)
		target.push_back(i);

	QCollator col;
	col.setNumericMode(true);
	if (col("a", "a")) {
		std::cerr << "Falling back to non-numeric sorting." << std::endl;
		col.setNumericMode(false);
	}

	col.setCaseSensitivity(Qt::CaseInsensitive);
	std::function<bool(unsigned,unsigned)> byName = [&] (auto a, auto b) {
		return col(cl.at(a).name, cl.at(b).name);
	};
	auto bySizeName = [&] (auto a, auto b) {
		if (cl.at(a).size == cl.at(b).size)
			return byName(a, b);
		return cl.at(a).size > cl.at(b).size;
	};

	std::sort(target.begin(), target.end(), genericNames ? bySizeName : byName);

	s.clustering.order = std::move(target);
}

void Dataset::colorClusters()
{
	/* Note: caller has locked s for us for writing */

	auto &cl = s.clustering;
	for (unsigned i = 0; i < cl.clusters.size(); ++i) {
		cl.clusters[cl.order[i]].color = colorset[(int)i % colorset.size()];
	}
}

void Dataset::orderProteins(OrderBy reference)
{
	/* Note: caller has locked s for us for writing */

	/* initialize replacement with current configuration */
	// note that our argument 'reference' might _not_ be the configured one
	Order target{s.order.reference, s.order.synchronizing, false, {}, {}};

	/* use reasonable fallbacks */
	if (reference == OrderBy::CLUSTERING && s.clustering.empty()) {
		reference = OrderBy::HIERARCHY;
		target.fallback = true;
	}
	if (reference == OrderBy::HIERARCHY && s.hierarchy.empty()) {
		reference = OrderBy::NAME;
		target.fallback = true;
	}

	auto d = peek<Base>();
	auto p = peek<Proteins>();
	auto &index = target.index;

	auto byName = [&] (auto a, auto b) {
		return d->lookup(p, a).name < d->lookup(p, b).name;
	};

	switch (reference) {
	/* order based on hierarchy */
	case OrderBy::HIERARCHY: {
		std::function<void(unsigned)> collect;
		collect = [&] (unsigned hIndex) {
			auto &current = s.hierarchy[hIndex];
			if (current.protein >= 0)
				index.push_back((unsigned)current.protein);
			for (auto c : current.children)
				collect(c);
		};
		collect(s.hierarchy.size()-1);
		break;
	}
	/* order based on ordered clusters */
	case OrderBy::CLUSTERING: {
		auto &cl = s.clustering;
		// ensure that each protein appears only once
		std::unordered_set<unsigned> seen;
		for (auto ci : cl.order) {
			// assemble all affected proteins, and their spread from cluster core
			std::vector<std::pair<unsigned, double>> members;
			for (unsigned i = 0; i < d->protIds.size(); ++i) {
				if (seen.count(i))
					continue; // protein was part of bigger cluster
				if (cl.memberships[i].count(ci)) {
					double dist = cv::norm(d->features[i],
					                       cl.clusters[ci].mode, cv::NORM_L2SQR);
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

	s.order = target;
}
