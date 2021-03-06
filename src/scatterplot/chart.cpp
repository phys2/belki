#include "chart.h"
#include "windowstate.h"

#include <QAbstractAxis>
#include <QScatterSeries>
#include <QSplineSeries>
#include <QValueAxis>
#include <QLegendMarker>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPolygonF>
#include <QHash>
#include <QTimer>
#include <QCursor>
#include <QMenu>

#include <map>
#include <cmath>

Chart::Chart(Dataset::ConstPtr data, const ChartConfig *config) :
    ax(new QtCharts::QValueAxis), ay(new QtCharts::QValueAxis),
    animReset(new QTimer(this)),
    config(config), data(data)
{
	/* set up general appearance */
	// disable grid animations as a lot of distracting stuff happens there
	// e.g. axis reset (and animate) when changing animation duration…
	setAnimationOptions(SeriesAnimations);
	legend()->setAlignment(Qt::AlignLeft);

	addAxis(ax, Qt::AlignBottom);
	addAxis(ay, Qt::AlignLeft);
	for (auto axis : {ax, ay}) {
		/* setup nice ticks with per-axis update mechanism */
		connect(axis, &QtCharts::QValueAxis::rangeChanged, [axis] {
			// defer custom ticks to avoid penalty when going through animation
			axis->setTickType(QtCharts::QValueAxis::TickType::TicksFixed);
			QTimer::singleShot(0, [axis] {
				if (axis->tickType() == QtCharts::QValueAxis::TickType::TicksFixed) {
					updateTicks(axis);
				}
			});
		});
	}

	/* set up master series */
	master = new Proteins("All proteins", Qt::gray, this);

	/* set up tracker ellipse used in track() */
	tracker = new QGraphicsEllipseItem(this);
	tracker->setPen({Qt::red});
	tracker->setZValue(1000);

	/* setup signal for range changes */
	// HACK: we expect ay to always be involved, and always update after ax!
	connect(ay, &QtCharts::QValueAxis::rangeChanged, this, &Chart::areaChanged);

	/* reset cursor whenever the zoom changes */
	connect(this, &Chart::areaChanged, this, &Chart::resetCursor);

	/* setup zoom history */
	connect(this, &Chart::areaChanged, [this] {
		if (zoom.current.isValid())
			zoom.history.push(zoom.current);
		zoom.current = {{ax->min(), ay->min()}, QPointF{ax->max(), ay->max()}};
	});

	/* setup animation reset timer */
	animReset->setSingleShot(true);
	connect(animReset, &QTimer::timeout, [this] {
		setAnimationDuration(1000);
		setAnimationOptions(SeriesAnimations);
	});
}

void Chart::setState(std::shared_ptr<WindowState> s) {
	hibernate();
	state = s;
}

void Chart::setConfig(const ChartConfig *cfg)
{
	config = cfg;
	emit proteinStyleUpdated();	// TODO this currently does not update colors
	refreshCursor();
}

void Chart::hibernate()
{
	awake = false;
	if (state) {
		state->disconnect(this);
		state->proteins().disconnect(this);
	}
	data->disconnect(this);
}

void Chart::wakeup()
{
	if (awake)
		return;

	awake = true;
	// note: this always rebuilds partitions, even if already good 😕
	changeAnnotations(); // also calls toggleAnnotations()
	updateMarkers();

	/* get updates from state (specify receiver so signal is cleaned up!) */
	auto s = state.get();
	connect(s, &WindowState::annotationsToggled, this, &Chart::toggleAnnotations);
	connect(s, &WindowState::annotationsChanged, this, &Chart::changeAnnotations);
	connect(&state->proteins(), &ProteinDB::markersToggled, this, &Chart::toggleMarkers);

	/* get updates from dataset (specify receiver so signal is cleaned up!) */
	connect(data.get(), &Dataset::update, this, [this] (Dataset::Touched touched) {
		if (touched & Dataset::Touch::ANNOTATIONS)
			updatePartitions();
	});
}

void Chart::setTitles(const QString &x, const QString &y)
{
	ax->setTitleText(x);
	ay->setTitleText(y);
}

void Chart::display(const QVector<QPointF> &coords)
{
	/* avoid custom ticks to explode to huge number during extrem range changes */
	for (auto axis : {ax, ay})
		axis->setTickType(QtCharts::QValueAxis::TickType::TicksFixed);

	/* disable fancy transition on full reset */
	animate(master->pointsVector().empty() ? 0 : 1000);

	resetCursor();
	zoom = {};

	/* update point set */
	master->replace(coords);

	/* update ranges cheap & dirty */
	auto bbox = QPolygonF(master->pointsVector()).boundingRect();
	// TODO: maintain w/h ratio of 1
	auto offset = bbox.width() * 0.05; // give some breathing space
	bbox.adjust(-offset, -offset, offset, offset);
	ax->setRange(bbox.left(), bbox.right());
	ay->setRange(bbox.top(), bbox.bottom());

	/* get back to appropriate custom ticks */
	for (auto axis : {ax, ay})
		updateTicks(axis);

	/* update other sets */
	updatePartitions();
	updateMarkers(true);
}

void Chart::changeAnnotations()
{
	partitions.clear(); // invalidate old stuff
	updatePartitions(); // in case new ones are already available
}

void Chart::updatePartitions()
{
	auto source = master->pointsVector();
	if (source.empty())
		return; // we're not displaying anything

	auto d = data->peek<Dataset::Base>();
	auto s = data->peek<Dataset::Structure>(); // keep while we operate with Annot*!
	auto annotations = s->fetch(state->annotations);
	if (!annotations) {
		toggleAnnotations();
		return; // no clusters means nothing more to do!
	}

	bool fresh = partitions.empty();

	/* set up partition series */
	if (fresh) {
		animate(0);

		// series needed for soft clustering
		partitions[-2] = std::make_unique<Proteins>("Unlabeled", config->proteinStyle.color.unlabeled, this);
		partitions[-1] = std::make_unique<Proteins>("Mixed", config->proteinStyle.color.mixed, this);

		// go through clusters in their designated order
		for (auto i : annotations->order) {
			auto &g = annotations->groups.at(i);
			auto series = new Proteins(g.name, g.color, this);
			partitions.try_emplace((int)i, series);
			/* enable profile view updates on legend label hover */
			auto lm = legend()->markers(series).first();
			connect(lm, &QtCharts::QLegendMarker::hovered, [this, series] (bool active) {
				if (!active)
					return;
				if (cursorLocked) // do not override locked-in user selection
					return;
				emit cursorChanged(series->proteins, series->name());
				for (auto& [_, p] : partitions)
					p->redecorate(false, series == p.get());
			});
		}
	} else {
		for (auto &[_, p] : partitions)
			p->clear();
	}

	/* populate with proteins */
	for (unsigned i = 0; i < annotations->memberships.size(); ++i) {
		auto &m = annotations->memberships[i];
		int target = -2; // first series, unlabeled
		if (m.size() > 1)
			target++; // second series, mixed
		if (m.size() == 1)
			target = (int)*m.begin();
		partitions.at(target)->add(d->protIds[i], i, source[(int)i]);
	}
	// the partitions use deferred addition, need to finalize
	for (auto &[_, p] : partitions)
		p->apply();

	/* hide empty series from legend (in case of hard clustering) */
	if (fresh) {
		for (auto s : {partitions[-2].get(), partitions[-1].get()})
			if (s->pointsVector().empty())
				removeSeries(s);
	}
	toggleAnnotations();
}

void Chart::moveCursor(const QPointF &pos)
{
	if (cursorLocked)
		return;

	cursorCenter = pos;
	if (cursorCenter.isNull() || !plotArea().contains(cursorCenter)) {
		nearestProtein = -1;
		tracker->hide();

		// do not interfer with cluster profile view
		if (pos.isNull() || !legend()->contains(pos)) // would ret. true for {}
			emit cursorChanged({});
		return;
	}

	refreshCursor();
}

void Chart::resetCursor()
{
	cursorLocked = false;
	moveCursor();
}

void Chart::toggleCursorLock()
{
	if (cursorCenter.isNull() || !plotArea().contains(cursorCenter)) {
		/* do not enable lock while cursor is invisible:
		 * user would not see the lock is on, and the lock would be on empty selection → useless */
		cursorLocked = false;
		return;
	}

	cursorLocked = !cursorLocked;
}

void Chart::openProteinMenu()
{
	if (nearestProtein < 0)
		return;

	auto protid = data->peek<Dataset::Base>()->protIds[(unsigned)nearestProtein];
	state->proteinMenu((ProteinId)protid)->exec(QCursor::pos());
}

void Chart::refreshCursor()
{
	if (cursorCenter.isNull())
		return;

	// find cursor in feature space (center + range)
	auto center = mapToValue(cursorCenter);
	auto diff = center - mapToValue(cursorCenter + QPointF{config->cursorRadius, 0});
	auto range = QPointF::dotProduct(diff, diff);

	// shape the corresponding ellipse in viewport space
	auto o = std::sqrt(range);
	QPointF offset(o, o);
	auto topLeft = mapToPosition(center - offset);
	auto botRight = mapToPosition(center + offset);
	tracker->setRect({topLeft, botRight});
	tracker->setTransformOriginPoint(center);

	// show tracker
	tracker->show();

	// determine all proteins that fall into the cursor
	std::vector<ProteinId> list;
	std::set<int> affectedPartitions;
	auto d = data->peek<Dataset::Base>();
	auto s = data->peek<Dataset::Structure>(); // keep while we work with annot.
	auto annotations = s->fetch(state->annotations);
	auto pv = master->pointsVector();

	nearestProtein = -1;
	auto nearest = std::numeric_limits<double>::max();
	for (int i = 0; i < pv.size(); ++i) {
		auto diffVec = pv[i] - center;
		auto dist = QPointF::dotProduct(diffVec, diffVec);
		if (dist < range) {
			if (dist < nearest) {
				nearest = dist;
				nearestProtein = i;
			}

			list.push_back(d->protIds[(size_t)i]);
			if (!annotations)
				continue;
			for (auto m : annotations->memberships[(unsigned)i])
				affectedPartitions.insert((int)m);
		}
	}
	s.unlock();
	d.unlock();

	for (auto& [i, p] : partitions)
		p->redecorate(false, affectedPartitions.count(i));

	emit cursorChanged(list);
}

void Chart::undoZoom(bool full)
{
	if (zoom.history.empty())
		return;

	auto range = (full ? zoom.history.first() : zoom.history.pop());
	if (full) {
		zoom.history.clear();
		/* avoid custom ticks to explode to huge number during extrem range changes */
		for (auto axis : {ax, ay})
			axis->setTickType(QtCharts::QValueAxis::TickType::TicksFixed);
	}
	ax->setRange(range.left(), range.right());
	ay->setRange(range.top(), range.bottom());
	// undo triggered push
	zoom.history.pop();
}

void Chart::toggleAnnotations()
{
	bool showPartitions = state->showAnnotations && !partitions.empty();
	// always apply, we may be inconsistent in master/partitions visibility
	master->setVisible(!showPartitions);
	for (auto &[_, p]: partitions)
		p->setVisible(showPartitions);
}

void Chart::zoomAt(const QPointF &pos, qreal factor)
{
	animate(0);
	auto stretch = 1./factor;
	auto center = mapToValue(pos);

	/* zoom in a way that point under the mouse stays fixed */
	auto dl = center.x() - ax->min(), dr = ax->max() - center.x();
	ax->setRange(center.x() - dl*stretch, center.x() + dr*stretch);

	auto dt = center.y() - ay->min(), db = ay->max() - center.y();
	ay->setRange(center.y() - dt*stretch, center.y() + db*stretch);
}

void Chart::updateMarkers(bool newDisplay)
{
	auto p = data->peek<Dataset::Proteins>();

	// remove outdated
	erase_if(markers, [&p] (auto it) { return !p->markers.count(it->first); });

	// update existing
	if (newDisplay) {
		for (auto &[_, m] : markers)
			m.series->replace(0, master->pointsVector()[(int)m.sampleIndex]);
	}

	// insert missing
	toggleMarkers({p->markers.begin(), p->markers.end()}, true);
}

void Chart::toggleMarkers(const std::vector<ProteinId> &ids, bool present)
{
	if (present && master->pointsVector().empty()) // we are not ready yet
		return;

	for (auto id : ids) {
		if (present) {
			try {
				markers.try_emplace(id, this, data->peek<Dataset::Base>()->protIndex.at(id), id);
			} catch (...) {}
		} else {
			markers.erase(id);
			if (firstMarker == id)
				firstMarker = 0; // invalidate
		}
	}
}

void Chart::animate(int msec) {
	setAnimationDuration(msec);
	if (msec == 0)
		setAnimationOptions(NoAnimation); // yes, this avoids slowdown…
	animReset->start(msec + 1000);
}

void Chart::updateTicks(QtCharts::QValueAxis *axis)
{
	auto cleanNumber = QString::number(axis->max() - axis->min(), 'g', 1).toDouble();
	auto interval = cleanNumber * 0.25;
	axis->setTickAnchor(0.);
	axis->setTickInterval(interval);
	axis->setTickType(QtCharts::QValueAxis::TickType::TicksDynamic);
}

ProteinId Chart::findFirstMarker()
{
	if (!firstMarker) {
		unsigned lowest = Marker::nextIndex;
		for (auto &[id, m] : markers) {
			if (m.index < lowest) {
				lowest = m.index;
				firstMarker = id;
			}
		}
	}
	return firstMarker; // note: will still be 0 if there are no markers
}

Chart::Proteins::Proteins(const QString &label, QColor color, Chart *chart)
{
	setName(label);
	/* insert _before_ any markers */
	auto markerId = chart->findFirstMarker();
	if (markerId)
		chart->insertSeries(chart->markers.at(markerId).series.get(), this);
	else
		chart->addSeries(this);
	attachAxis(chart->ax);
	attachAxis(chart->ay);

	setColor(color);
	redecorate();

	chart->legend()->markers(this).first()->setShape(QtCharts::QLegend::MarkerShapeCircle);

	// follow style changes (note: receiver specified for cleanup on delete!)
	connect(chart, &Chart::proteinStyleUpdated, this, [this] {
		redecorate();
	});
}

void Chart::Proteins::clear()
{
	proteins.clear();
	samples.clear();
	QtCharts::QScatterSeries::clear();
}

void Chart::Proteins::add(ProteinId id, unsigned index, const QPointF &point)
{
	proteins.push_back(id);
	samples.append(index);
	replacement.append(point); // deferred addition to chart for speed reasons
}

void Chart::Proteins::apply()
{
	replace(replacement);
	replacement.clear();
}

void Chart::Proteins::redecorate(bool full, bool hl)
{
	if (!full && (hl == highlighted))
		return;

	highlighted = hl;

	auto c = reinterpret_cast<Chart*>(chart());
	if (!c)
		return;
	auto &s = c->config->proteinStyle;

	if (full)
		setMarkerSize(s.size);

	QPen border(s.border);
	auto col = Qt::darkGray;
	if (highlighted) {
		col = Qt::black;
	}
	border.setColor(col);
	border.setStyle(highlighted ? Qt::PenStyle::SolidLine : s.border);
	setPen(border);

	auto fillColor = brush().color();
	auto alpha = s.alpha.reg;
	if (s.singleMode)
		alpha = (highlighted ? s.alpha.hi : s.alpha.lo);
	fillColor.setAlphaF(alpha);
	setColor(fillColor);
}

Chart::Marker::Marker(Chart *chart, unsigned sampleIndex, ProteinId id)
    : index(nextIndex++),
      sampleIndex(sampleIndex), sampleId(id),
      series(std::make_unique<QtCharts::QScatterSeries>())
{
	auto config = chart->config;
	auto s = series.get();

	auto label = chart->data->peek<Dataset::Proteins>()->proteins[sampleId].name;
	s->setName(label);
	s->setPointLabelsFormat(label);

	s->append(chart->master->pointsVector()[(int)sampleIndex]);
	chart->addSeries(s);

	s->attachAxis(chart->ax);
	s->attachAxis(chart->ay);

	s->setBorderColor(Qt::black);
	s->setColor(chart->data->peek<Dataset::Proteins>()->proteins[sampleId].color);
	s->setMarkerShape(QtCharts::QScatterSeries::MarkerShapeRectangle);
	s->setMarkerSize(config->proteinStyle.size * 1.3333);
	auto f = s->pointLabelsFont(); // increase font size
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.3);
	s->setPointLabelsFont(f);
	s->setPointLabelsVisible(true);

	/* install protein menu */
	auto openMenu = [chart,id] { chart->state->proteinMenu(id)->exec(QCursor::pos()); };
	connect(s, &QtCharts::QScatterSeries::clicked, openMenu);
	auto lm = chart->legend()->markers(s).first();
	connect(lm, &QtCharts::QLegendMarker::clicked, openMenu);

	// follow style changes (note: receiver specified for cleanup on delete!)
	s->connect(chart, &Chart::proteinStyleUpdated, s, [config, s] {
		// we only care about size
		s->setMarkerSize(config->proteinStyle.size * 1.3333);
	});
}
