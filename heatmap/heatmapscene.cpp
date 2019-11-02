#include "heatmapscene.h"
#include "windowstate.h"
#include "compute/colors.h"
#include "compute/features.h"

#include <QPainter>
#include <QGraphicsItem>
#include <QGraphicsSceneHoverEvent>

HeatmapScene::HeatmapScene(Dataset::Ptr data)
    : data(data),
      state(std::make_shared<WindowState>())
{
	auto d = data->peek<Dataset::Base>();

	/* build up scene with data */
	profiles.resize(d->features.size());
	for (unsigned i = 0; i < profiles.size(); ++i) {
		// setup profile graphics item
		auto h = new Profile(i, d);
		h->setBrush(Qt::transparent);

		// add to scene and our own container
		addItem(h);
		profiles[i] = h;
	}
	// note: order will be done in first rearrange() (when view is available)
	// note: colors will be done in first recolor() by updateColorset()

	// save for later
	layout.columnWidth = profiles[0]->boundingRect().width();
}

void HeatmapScene::setState(std::shared_ptr<WindowState> s)
{
	hibernate();
	state = s;
}

void HeatmapScene::setScale(qreal scale)
{
	pixelScale = scale;
	// markers use pixelScale
	for (auto& [_, m] : markers)
		m.rearrange(profiles[m.sampleIndex]->pos());
}

void HeatmapScene::hibernate()
{
	awake = false;
	if (state)
		state->disconnect(this);
	data->disconnect(this);
}

void HeatmapScene::wakeup()
{
	if (awake)
		return;

	awake = true;
	updateAnnotations();
	updateMarkers();

	/* get updates from state (specify receiver so signal is cleaned up!) */
	auto s = state.get();
	connect(s, &WindowState::annotationsToggled, this, &HeatmapScene::updateAnnotations);
	connect(s, &WindowState::annotationsChanged, this, &HeatmapScene::updateAnnotations);
	connect(s, &WindowState::orderChanged, this, &HeatmapScene::reorder);

	/* get updates from dataset (specify receiver so signal is cleaned up!) */
	connect(data.get(), &Dataset::update, this, [this] (Dataset::Touched touched) {
		if (touched & Dataset::Touch::ORDER)
			reorder();
		if (touched & Dataset::Touch::CLUSTERS)
			recolor();
	});
}

void HeatmapScene::rearrange(QSize newViewport)
{
	viewport = newViewport; // keep information in case we have to stop here

	auto aspect = (viewport.width() / layout.columnWidth) / viewport.height();
	layout.columns = (unsigned)std::floor(std::sqrt(profiles.size() * aspect));

	rearrange(layout.columns);
}

void HeatmapScene::rearrange(unsigned columns)
{
	if (!columns)
		return;

	layout.rows = (unsigned)std::ceil(profiles.size() / (float)columns);

	// reposition profiles
	reorder();

	// set scene rect
	QRectF box({0, 0}, QSizeF{layout.columnWidth * columns, (qreal)layout.rows});
	qreal offset = 10; // some "feel good" borders
	setSceneRect(box.adjusted(-offset, -offset, offset, offset));
}

void HeatmapScene::reorder()
{
	if (!layout.rows) // view is not set-up yet
		return;

	auto d = data->peek<Dataset::Structure>(); // keep while we operate with Order*!
	auto order = d->fetch(state->order);

	/* optimization: disable slow stuff as we move everything around */
	auto indexer = itemIndexMethod();
	setItemIndexMethod(QGraphicsScene::ItemIndexMethod::NoIndex);

	for (unsigned i = 0; i < profiles.size(); ++i) {
		auto p = profiles[order.index[i]];
		p->setPos((i / layout.rows) * layout.columnWidth, i % layout.rows);
	}

	// sync marker positions
	for (auto& [_, m] : markers)
		m.rearrange(profiles[m.sampleIndex]->pos());

	/* optimization: restore index (used for hover events) */
	setItemIndexMethod(indexer);
}

void HeatmapScene::updateMarkers()
{
	auto p = data->peek<Dataset::Proteins>();

	// remove outdated
	erase_if(markers, [&p] (auto it) { return !p->markers.count(it->first); });

	// insert missing
	toggleMarkers({p->markers.begin(), p->markers.end()}, true);
}

void HeatmapScene::toggleMarkers(const std::vector<ProteinId> &ids, bool present)
{
	for (auto id : ids) {
		if (present) {
			try {
				auto index = data->peek<Dataset::Base>()->protIndex.at(id);
				auto pos = profiles[index]->pos();
				markers.try_emplace(id, this, index, pos);
			} catch (...) {}
		} else {
			markers.erase(id);
		}
	}
}

void HeatmapScene::updateAnnotations()
{
	recolor();
}

void HeatmapScene::recolor()
{
	auto clear = [&] () {
		for (auto &p : profiles)
			p->setBrush(Qt::transparent);
		update();
	};

	if (!state->showAnnotations)
		return clear();

	auto d = data->peek<Dataset::Structure>(); // keep while we operate with Annot*!
	auto annotations = d->fetch(state->annotations);
	if (!annotations)
		return clear();

	for (unsigned i = 0; i < profiles.size(); ++i) {
		const auto &assoc = annotations->memberships[i];
		switch (assoc.size()) {
		case 1:
			profiles[i]->setBrush(annotations->groups.at(*assoc.begin()).color);
			break;
		default: // TODO: maybe set to White on multiple memberships
			profiles[i]->setBrush(Qt::transparent);
		}
	}
	update();
}

HeatmapScene::Profile::Profile(unsigned index, View<Dataset::Base> &d)
    : index(index)
{
	auto feat = cv::Mat(d->features[index]);
	auto range = d->featureRange;
	if (d->logSpace) {
		range = features::log_valid(range);
		cv::log(cv::max(feat, range.min), feat);
		range.min = cv::log(range.min); range.max = cv::log(range.max);
	}
	features = Colormap::prepare(feat, range.scale(), range.min);

	if (d->hasScores()) {
		// apply score colormap flipped (low scores are better)
		scores = Colormap::stoplight_mild.apply(cv::Mat(d->scores[index]) * -1.,
		                         d->scoreRange.scale(), -d->scoreRange.max);
	}
	setAcceptHoverEvents(true);
}

QRectF HeatmapScene::Profile::boundingRect() const
{
	auto s = scene()->style;
	return {0, 0, 2. * s.margin + features.rows * s.expansion, 1};
}

void HeatmapScene::Profile::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*)
{
	painter->setPen(Qt::PenStyle::NoPen);
	auto s = scene()->style;
	auto b = brush();

	QColor fg, bg;
	bool mixin = s.mixin && (b.color() != Qt::transparent);
	if (!s.inverted) { // regular case, replace white bg with marker color
		fg = s.fg;
		bg = (mixin ? b.color() : s.bg);
	} else { // inverted case, swap fg/bg, replace white fg with marker color
		bg = s.fg;
		fg = (mixin ? b.color() : s.bg);
	}

	if (highlight) {
		painter->fillRect(boundingRect(), s.cursor);
	}
	if (b.color() != Qt::transparent)
		painter->fillRect(QRectF(0, 0, s.margin, 1), b.color());

	painter->fillRect(QRectF(s.margin, 0, features.rows * s.expansion, 1),
	                  highlight ? s.cursor : bg);

	for (auto i = 0; i < features.rows; ++i) {
		if (!scores.empty())
			fg = Colormap::qcolor(scores(i, 0));
		fg.setAlpha(features(i, 0));
		QRectF r(s.margin + i * s.expansion, 0, s.expansion, 1);
		painter->fillRect(r, fg);
	}
}


void HeatmapScene::Profile::hoverEnterEvent(QGraphicsSceneHoverEvent*)
{
	highlight = true;
	update();

	emit scene()->cursorChanged({index});
}

void HeatmapScene::Profile::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
	highlight = false;
	update();
}

void HeatmapScene::Profile::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
	// display a tooltip to expose the dimension the mouse is over
	auto s = scene()->style;
	auto index = int((event->pos().x() - s.margin) / s.expansion);
	if (index < 0 || index >= features.rows) {
		setToolTip({});
		return;
	}
	setToolTip(scene()->data->peek<Dataset::Base>()->dimensions.at(index));
}

HeatmapScene::Marker::Marker(HeatmapScene *scene, unsigned sampleIndex, const QPointF &pos)
    : sampleIndex(sampleIndex)
{
	auto p = scene->data->peek<Dataset::Proteins>();
	auto &meta = scene->data->peek<Dataset::Base>()->lookup(p, sampleIndex);

	QBrush fill(QColor{0, 0, 0, 127});
	QPen outline(meta.color.darker(300));
	outline.setCosmetic(true);
	backdrop.reset(scene->addRect({}));
	backdrop->setBrush(fill);
	backdrop->setPen(outline);

	line.reset(scene->addLine({}));
	QPen pen(meta.color.darker(150));
	pen.setCosmetic(true);
	line->setPen(pen);

	// do label last, so it will be on top of its backdrop
	label.reset(scene->addSimpleText(meta.name));
	auto font = label->font();
	font.setBold(true);
	label->setFont(font);
	label->setBrush(meta.color);

	rearrange(pos);
}

void HeatmapScene::Marker::rearrange(const QPointF &pos)
{
	auto vCenter = pos.y() + 0.5;
	auto linewidth = .5 * scene()->style.margin;
	auto right = pos.x() + scene()->style.margin;

	auto scale = scene()->pixelScale;
	auto margin = 2.*scale;

	// invert zoom for label
	label->setScale(scale);
	auto labelSize = label->sceneBoundingRect().size();

	// place label
	auto left = right - (labelSize.width() + margin + linewidth);
	label->setPos(left, vCenter - labelSize.height()/2.);
	backdrop->setRect(label->sceneBoundingRect()
	                  .adjusted(-margin, -margin, margin, margin));

	// place line
	line->setLine({{right - linewidth, vCenter}, {right, vCenter}});
}
