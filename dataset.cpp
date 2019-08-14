#include "dataset.h"
#include "compute/features.h"
#include "compute/dimred.h"

#include <QDataStream>
#include <QTextStream>
#include <QRegularExpression>

#include <opencv2/core.hpp>
#include <tbb/parallel_for.h>
#include <unordered_set>

Dataset::Dataset(ProteinDB &proteins, DatasetConfiguration conf)
    : conf(conf), proteins(proteins)
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
	s.clustering = Annotations(b.protIds.size());

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

void Dataset::computeFAMS(float k)
{
	if (!meanshift) {
		b.l.lockForWrite(); // we guard meanshift init with write on base lock (hack)
		if (!meanshift)
			meanshift = std::make_unique<annotations::Meanshift>(b.features);
		b.l.unlock();
	}

	auto result = meanshift->applyK(k);
	if (result) {
		auto name = QString("Mean Shift, k=%1").arg((double)k, 0, 'f', 2);
		applyClustering(name, result->modes, result->associations);
	}
}

void Dataset::applyClustering(const QString& name, const Features::Vec &modes, const std::vector<int>& index) {
	auto d = peek<Base>();

	/* Note: we do not work with our descendant of Annotations and set memberships
	 * directly, as they would need to be yet again updated after pruning. */
	::Annotations target;
	target.name = name;
	target.source = conf.id;

	for (unsigned i = 0; i < modes.size(); ++i)
		target.groups[i] = {QString("Cluster #%1").arg(i+1), {}, {}, modes[i]};

	for (unsigned i = 0; i < index.size(); ++i) {
		auto m = (unsigned)index[i];
		target.groups[m].members.push_back(d->protIds[i]);
	}

	annotations::prune(target);
	annotations::order(target, true);
	annotations::color(target, proteins.groupColors());

	s.l.lockForWrite();
	auto touched = applyAnnotations(target, 0, true);
	s.l.unlock();

	emit update(touched);
}

void Dataset::applyAnnotations(unsigned id)
{
	// just in case it's running
	if (meanshift)
		meanshift->cancel();

	s.l.lockForWrite();
	Touched touched;

	// 0: clean in any case
	if (id == 0 && !s.clustering.empty()) {
		s.clustering = Annotations(peek<Base>()->protIds.size());
		s.clusteringId = 0;
		touched |= Touch::CLUSTERS;
	}

	// others: apply from proteindb, if not already applied
	if (s.clusteringId != id) {
		touched |= applyAnnotations(std::get<::Annotations>(proteins.peek()->structures.at(id)), id);
	}

	s.l.unlock();
	emit update(touched);
}

void Dataset::applyHierarchy(unsigned id, unsigned granularity)
{
	// note: to remove a hierarchy (by passing id 0) is not supported yet

	Touched touched = Touch::HIERARCHY;

	s.l.lockForWrite();
	s.hierarchy = std::get<HrClustering>(proteins.peek()->structures.at(id));

	if (s.order.synchronizing &&
	    (s.order.reference == OrderBy::HIERARCHY ||
	     s.order.reference == OrderBy::CLUSTERING)) {
		orderProteins(OrderBy::HIERARCHY);
		touched |= Touch::ORDER;
	}

	if (granularity != 0) {
		touched |= calculatePartition(granularity);
	}
	s.l.unlock();

	emit update(touched);
}

void Dataset::createPartition(unsigned granularity)
{
	// just in case it's running
	if (meanshift)
		meanshift->cancel();

	s.l.lockForWrite();
	auto touched = calculatePartition(granularity);
	s.l.unlock();

	emit update(touched);
}

Dataset::Touched Dataset::calculatePartition(unsigned granularity)
{
	auto target = annotations::partition(s.hierarchy, granularity);
	target.name = QString("%2 at granularity %1").arg(granularity).arg(s.hierarchy.name);
	target.source = conf.id;

	annotations::prune(target);
	annotations::order(target, true);
	annotations::color(target, proteins.groupColors());

	return applyAnnotations(target, 0, false);
}

Dataset::Touched Dataset::applyAnnotations(const ::Annotations &source, unsigned id, bool reorderProts)
{
	/* Note: caller has locked s for us for writing */

	s.clustering = Annotations(source, *peek<Base>());

	/* calculate centroids, if not already there and compatible */
	bool needCentroids = s.clustering.source != conf.id;
	if (!s.clustering.empty() && source.groups.begin()->second.mode.empty())
		needCentroids = true;
	if (needCentroids)
		computeCentroids(s.clustering);

	Touched touched = Touch::CLUSTERS;
	if (reorderProts && s.order.synchronizing && s.order.reference == OrderBy::CLUSTERING) {
		orderProteins(OrderBy::CLUSTERING);
		touched |= Touch::ORDER;
	}

	s.clusteringId = id;

	return touched;
}

// TODO: move to storage
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

void Dataset::computeCentroids(Annotations &target)
{
	auto d = peek<Base>();

	std::unordered_map<unsigned, size_t> effective_sizes;
	for (auto &[i, g]: target.groups) {
		g.mode = std::vector<double>((size_t)d->dimensions.size(), 0.);
		effective_sizes[i] = 0;
	}

	for (unsigned i = 0; i < target.memberships.size(); ++i) {
		for (auto ci : target.memberships[i]) {
			cv::add(target.groups[ci].mode, d->features[i], target.groups[ci].mode);
			effective_sizes[ci]++;
		}
	}

	for (auto& [i, g] : target.groups) {
		auto scale = 1./effective_sizes[i];
		auto &m = g.mode;
		std::for_each(m.begin(), m.end(), [scale] (double &e) { e *= scale; });
	}
}

void Dataset::orderProteins(OrderBy reference)
{
	/* Note: caller has locked s for us for writing */
	auto& target = s.order;

	/* initialize replacement with current configuration */
	// note that our argument 'reference' might _not_ be the configured one
	target = {s.order.reference, s.order.synchronizing, false, {}, {}};

	/* use reasonable fallbacks */
	if (reference == OrderBy::CLUSTERING && s.clustering.empty()) {
		reference = OrderBy::HIERARCHY;
		target.fallback = true;
	}
	if (reference == OrderBy::HIERARCHY && s.hierarchy.clusters.empty()) {
		reference = OrderBy::NAME;
		target.fallback = true;
	}

	auto d = peek<Base>();
	auto p = peek<Proteins>();
	auto &index = target.index;

	auto byName = [&] (auto a, auto b) {
		return d->lookup(p, a).name < d->lookup(p, b).name;
	};

	// residual fill
	auto addUnseen = [&] (const auto &seen) {
		int start = index.size(); // where we start adding
		for (unsigned i = 0; i < d->protIds.size(); ++i) {
			if (!seen.count(i))
				index.push_back(i);
		}
		std::sort(index.begin() + start, index.end(), byName);
	};

	switch (reference) {
	/* order based on hierarchy */
	case OrderBy::HIERARCHY: {
		std::unordered_set<unsigned> seen;
		std::function<void(unsigned)> collect;
		collect = [&] (unsigned hIndex) {
			auto &current = s.hierarchy.clusters[hIndex];
			if (current.protein) {
				try {
					auto i = d->protIndex.at(current.protein.value());
					index.push_back(i);
					seen.insert(i);
				} catch (std::out_of_range &) {}
			}
			for (auto c : current.children)
				collect(c);
		};
		collect(s.hierarchy.clusters.size()-1);

		// add all proteins not covered yet
		addUnseen(seen);
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
					                       cl.groups[ci].mode, cv::NORM_L2SQR);
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
		addUnseen(seen);
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
}

Dataset::Annotations::Annotations(const ::Annotations &in, const Features &data)
    : ::Annotations(in), memberships(data.protIds.size())
{
	for (auto &[k,v] : in.groups) {
		for (auto id : v.members) {
			try {
				auto index = data.protIndex.at(id);
				memberships[index].insert(k);
			} catch (std::out_of_range&) {}
		}
	}
}
