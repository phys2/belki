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
	qRegisterMetaType<Ptr>("DataPtr");
	qRegisterMetaType<ConstPtr>("ConstDataPtr");
}

template<>
View<Dataset::Base> Dataset::peek() const { return View(b); }
template<>
View<Dataset::Representations> Dataset::peek() const { return View(r); }
template<>
View<Dataset::Structure> Dataset::peek() const { return View(s); }
template<>
View<Dataset::Proteins> Dataset::peek() const { return proteins.peek(); }

void Dataset::spawn(Features::Ptr base, std::unique_ptr<::Representations> repr)
{
	b.dimensions = std::move(base->dimensions);
	b.protIds = std::move(base->protIds);
	b.protIndex = std::move(base->protIndex);
	b.features = std::move(base->features);
	b.featureRange = std::move(base->featureRange);
	b.logSpace = std::move(base->logSpace);
	b.scores = std::move(base->scores);
	b.scoreRange = std::move(base->scoreRange);

	if (repr)
		r.displays = std::move(repr->displays);

	/* build protein index if missing */
	if (b.protIndex.empty()) {
		for (unsigned i = 0; i < b.protIds.size(); ++i)
			b.protIndex[b.protIds[i]] = i;
	}

	/* pre-cache features as QPoints for plotting */
	b.featurePoints = features::pointify(b.features);

	/* calculate default orders */
	computeOrder({Order::FILE});
	computeOrder({Order::NAME});
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
		if (conf.scoreThresh > 0.) {
			features::apply_cutoff(b.features, b.scores, conf.scoreThresh);
		}
		b.scoreRange = features::range_of(b.scores);
	}

	b.featureRange = features::range_of(b.features);
	b.featurePoints = features::pointify(b.features);

	auto sIn = srcholder->peek<Structure>();
	s.fileOrder = sIn->fileOrder;
	s.nameOrder = sIn->nameOrder;
	/* we do not keep other structure data; modes may be invalid for registered
	 * annotations, and internal clusters (hiercut/meanshift) are fully invalid. */
}

void Dataset::computeDisplay(const QString& request)
{
	b.l.lockForRead();
	auto result = dimred::compute(request, b.features);
	b.l.unlock();

	r.l.lockForWrite();
	for (auto name : result.keys()) {
		r.displays[name] = result[name]; // TODO std::move
		// TODO: lookup in datasets[d->conf->parent].displays and perform rigid registration
	}
	r.l.unlock();

	emit update(Touch::DISPLAY);
}

void Dataset::computeDisplays()
{
	/* compute PCA displays as a fast starting point */
	r.l.lockForWrite(); // proactive write lock, avoid gap that may lead to double computation
	if (!r.displays.count("PCA 12"))
		computeDisplay("PCA");
	r.l.unlock();
}

void Dataset::addDisplay(const QString& name, const Representations::Pointset &points)
{
	r.l.lockForWrite();
	r.displays[name] = std::move(points);
	r.l.unlock();

	emit update(Touch::DISPLAY);
}

void Dataset::prepareAnnotations(const Annotations::Meta &desc)
{
	if (peek<Structure>()->fetch(desc))
		return; // already there

	Touched touched;

	if (desc.id > 0) {
		/* apply existing annotations from proteindb */
		// would use std::get(), but not available on MacOS 10.13
		const auto &src = *std::get_if<::Annotations>(&proteins.peek()->structures.at(desc.id));
		touched |= storeAnnotations(src, true);
	} else {
		/* special case: meanshift */
		if (desc.type == Annotations::Meta::MEANSHIFT) {
			auto src = computeFAMS(desc.k);
			if (!src.groups.empty())
				touched |= storeAnnotations(src, true);
		}

		/* special case: hierarchy cut */
		if (desc.type == Annotations::Meta::HIERCUT) {
			auto src = createPartition(desc.hierarchy, desc.granularity);
			touched |= storeAnnotations(src, false);
		}
	}

	emit update(touched);
}

void Dataset::prepareOrder(const ::Order &desc)
{
	if (peek<Structure>()->fetch(desc).type == desc.type) // didn't fall back
		return; // already there

	s.l.lockForWrite();
	computeOrder(desc);
	s.l.unlock();
	emit update(Touch::ORDER);
}

Annotations Dataset::computeFAMS(float k)
{
	if (!meanshift) {
		b.l.lockForWrite(); // we guard meanshift init with write on base lock (hack)
		if (!meanshift)
			meanshift = std::make_unique<annotations::Meanshift>(b.features);
		b.l.unlock();
	}

	auto result = meanshift->run(k);
	if (!result)
		return {};

	/* Note: we do not work with our descendant of Annotations and set
	 * memberships directly, as pruning invalidates them. */
	::Annotations ret;
	ret.meta = {Annotations::Meta::MEANSHIFT};
	ret.meta.name = QString("Mean Shift, k=%1").arg((double)k, 0, 'f', 2);
	ret.meta.dataset = conf.id;
	ret.meta.k = k;

	auto d = peek<Base>();
	for (unsigned i = 0; i < result->modes.size(); ++i)
		ret.groups[i] = {QString("Cluster #%1").arg(i+1), {}, {}, result->modes[i]};

	for (unsigned i = 0; i < result->associations.size(); ++i) {
		auto m = (unsigned)result->associations[i];
		ret.groups[m].members.push_back(d->protIds[i]);
	}

	annotations::prune(ret);
	annotations::order(ret, true);
	annotations::color(ret, proteins.groupColors());

	return ret;
}

Annotations Dataset::createPartition(unsigned id, unsigned granularity)
{
	auto hierarchy = *std::get_if<HrClustering>(&proteins.peek()->structures.at(id)); // Apple no std::get
	auto ret = annotations::partition(hierarchy, granularity);

	annotations::prune(ret);
	annotations::order(ret, true);
	annotations::color(ret, proteins.groupColors());

	return ret;
}

Dataset::Touched Dataset::storeAnnotations(const ::Annotations &source, bool withOrder)
{
	s.l.lockForWrite();

	auto it = s.annotations.emplace(source.meta.id, Annotations{source, *peek<Base>()});
	auto &target = it->second;

	/* calculate centroids, if not already there and compatible */
	bool needCentroids = target.meta.dataset != conf.id;
	if (!target.groups.empty() && target.groups.begin()->second.mode.empty())
		needCentroids = true;
	if (needCentroids)
		computeCentroids(target);

	Touched touched = Touch::CLUSTERS;
	if (withOrder) {
		computeOrder({Order::CLUSTERING, target.meta});
		touched |= Touch::ORDER;
	}

	s.l.unlock();
	return touched;
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

void Dataset::computeOrder(const ::Order &desc)
{
	/* Note: caller has locked s for us for writing */

	auto p = peek<Proteins>();

	/* initialize order object */
	Order *target;
	const Annotations *asource = nullptr;
	unsigned sourceid = 0;
	switch (desc.type) {
	case Order::FILE:	target = &s.fileOrder; break;
	case Order::NAME:	target = &s.nameOrder; break;
	case Order::CLUSTERING:
		asource = s.fetch(*std::get_if<Annotations::Meta>(&desc.source)); // Apple no std::get
		if (!asource)
			return;
		target = &s.orders.emplace(asource->meta.id, Order{desc})->second;
		break;
	case Order::HIERARCHY:
		sourceid = std::get_if<HrClustering::Meta>(&desc.source)->id; // Apple no std::get
		if (!p->structures.count(sourceid))
			return;
		target = &s.orders.emplace(sourceid, Order{desc})->second;
	}

	/* work on target */
	auto d = peek<Base>();
	auto &index = target->index;

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

	switch (desc.type) {
	/* order based on hierarchy */
	case Order::HIERARCHY: {
		std::unordered_set<unsigned> seen;
		std::function<void(unsigned)> collect;
		auto source = std::get_if<HrClustering>(&p->structures.at(sourceid)); // Apple no std::get
		collect = [&] (unsigned hIndex) {
			auto &current = source->clusters[hIndex];
			if (current.protein) {
				try {
					auto i = d->protIndex.at(current.protein.value_or(0));
					index.push_back(i);
					seen.insert(i);
				} catch (std::out_of_range &) {}
			}
			for (auto c : current.children)
				collect(c);
		};
		collect(source->clusters.size()-1);

		// add all proteins not covered yet
		addUnseen(seen);
		break;
	}
	/* order based on ordered clusters */
	case Order::CLUSTERING: {
		// ensure that each protein appears only once
		std::unordered_set<unsigned> seen;
		for (auto ci : asource->order) {
			// assemble all affected proteins, and their spread from cluster core
			std::vector<std::pair<unsigned, double>> members;
			for (unsigned i = 0; i < d->protIds.size(); ++i) {
				if (seen.count(i))
					continue; // protein was part of bigger cluster
				if (asource->memberships[i].count(ci)) {
					double dist = cv::norm(d->features[i],
					                       asource->groups.at(ci).mode,
					                       cv::NORM_L2SQR);
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
		if (desc.type == Order::NAME)
			std::sort(index.begin(), index.end(), byName);
	}

	/* now fill the back-references */
	target->rankOf.resize(index.size());
	for (size_t i = 0; i < index.size(); ++i)
		target->rankOf[index[i]] = i;
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

const Dataset::Annotations *Dataset::Structure::fetch(const Annotations::Meta &desc) const
{
	if (desc.id > 0) {
		auto it = annotations.find(desc.id);
		return (it == annotations.end() ? nullptr : &it->second);
	}

	// now to the special cases
	auto candidates = annotations.equal_range(0);
	for (auto it = candidates.first; it != candidates.second; ++it) {
		const auto &meta = it->second.meta;
		if (meta.type != desc.type)
			continue;
		if (meta.type == Annotations::Meta::MEANSHIFT) {
			if (meta.k == desc.k)
				return &it->second;
		}
		if (meta.type == Annotations::Meta::HIERCUT) {
			if (meta.hierarchy == desc.hierarchy && meta.granularity == desc.granularity)
				return &it->second;
		}
	}
	return nullptr;
}

const Dataset::Order& Dataset::Structure::fetch(::Order desc) const
{
	/* simple cases */
	if (desc.type == Order::FILE)
		return fileOrder;
	if (desc.type == Order::NAME)
		return nameOrder;

	/* try to find by-annotation/hierarchy */
	unsigned key = 0;
	if (desc.type == Order::CLUSTERING)
		key = std::get_if<Annotations::Meta>(&desc.source)->id; // Apple no std::get
	if (desc.type == Order::HIERARCHY)
		key = std::get_if<HrClustering::Meta>(&desc.source)->id; // Apple no std::get
	if (key > 0) {
		auto it = orders.find(key);
		return (it == orders.end() ? nameOrder : it->second);
	}

	/* try to find for internal annotation */
	auto candidates = orders.equal_range(0);
	for (auto it = candidates.first; it != candidates.second; ++it) {
		if (it->second.type != desc.type)
			continue;
		if (desc.type == Order::CLUSTERING) {
			auto a = std::get_if<Annotations::Meta>(&desc.source); // Apple no std::get
			auto b = std::get_if<Annotations::Meta>(&it->second.source); // Apple no std::get
			if (a->type != b->type)
				continue;
			if (a->type == Annotations::Meta::MEANSHIFT && a->k != b->k)
				continue;
			if (a->type == Annotations::Meta::HIERCUT &&
			    (a->hierarchy != b->hierarchy || a->granularity != b->granularity))
				continue;
		}
		return it->second;
	}
	return nameOrder;
}
