#include "heatmapscene.h"

#include <QPainter>
#include <QGraphicsItem>

#include <QtDebug>

HeatmapScene::HeatmapScene(Dataset &data) : data(data)
{

}

void HeatmapScene::setScale(qreal scale)
{
	pixelScale = scale;
	// markers use pixelScale
	for (auto& [i, m] : markers)
		m.rearrange(profiles[i]->pos());
}

void HeatmapScene::reset(bool haveData)
{
	profiles.clear();
	markers.clear();
	clear(); // removes & deletes all items (ie. profiles)

	if (!haveData) {
		return;
	}

	auto d = data.peek();

	/* build up scene with new data */
	profiles.resize((unsigned)d->features.size());
	for (unsigned i = 0; i < profiles.size(); ++i) {
		// setup profile graphics item
		auto h = new Profile(i, d->features[(int)i]);
		h->setBrush(Qt::transparent);

		// add to scene and our own container
		addItem(h);
		profiles[i] = h;
	}

	// empty data shouldn't happen but right now can when a file cannot be read completely,
	// in the future this should result in IOError already earlier
	if (profiles.empty())
		return;

	// save for later
	layout.columnWidth = profiles[0]->boundingRect().width();

	// arrange screen in case we already got a view up
	if (viewport.isValid())
		rearrange(viewport);
}

void HeatmapScene::rearrange(QSize newViewport)
{
	viewport = newViewport; // keep information in case we have to stop here
	if (profiles.empty())
		return;

	auto aspect = (viewport.width() / layout.columnWidth) / viewport.height();
	layout.columns = (unsigned)std::floor(std::sqrt(profiles.size() * aspect));

	rearrange(layout.columns);
}

void HeatmapScene::rearrange(unsigned columns)
{
	if (!columns || profiles.empty())
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

	auto d = data.peek();

	for (unsigned i = 0; i < profiles.size(); ++i) {
		auto p = profiles[d->order.index[i]];
		p->setPos((i / layout.rows) * layout.columnWidth, i % layout.rows);
	}

	// sync marker positions
	for (auto& [i, m] : markers)
		m.rearrange(profiles[i]->pos());
}

void HeatmapScene::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
	recolor();
	// TODO: re-initialize markers
}

void HeatmapScene::toggleMarker(unsigned sampleIndex, bool present)
{
	if (present) {
		auto pos = profiles[sampleIndex]->pos();
		markers.try_emplace(sampleIndex, this, sampleIndex, pos);
	} else {
		markers.erase(sampleIndex);
	}
}

void HeatmapScene::togglePartitions(bool show)
{
	showPartitions = show;
	recolor();
}

void HeatmapScene::recolor()
{
	auto d = data.peek();
	if (!showPartitions || d->clustering.empty()) {
		for (auto &p : profiles)
			p->setBrush(Qt::transparent);
		update();
		return;
	}

	for (unsigned i = 0; i < profiles.size(); ++i) {
		const auto &assoc = d->clustering.memberships[i];
		switch (assoc.size()) {
		case 1:
			profiles[i]->setBrush(d->clustering.clusters[*assoc.begin()].color);
			break;
		default: // TODO: maybe set to White on multiple memberships
			profiles[i]->setBrush(Qt::transparent);
		}
	}
	update();
}

HeatmapScene::Profile::Profile(unsigned index, const std::vector<double> &features, QGraphicsItem *parent)
    : QAbstractGraphicsShapeItem(parent),
      index(index), features(features)
{
	setAcceptHoverEvents(true);
}

QRectF HeatmapScene::Profile::boundingRect() const
{
	auto s = scene()->style;
	return {0, 0, 2. * s.margin + (qreal)features.size() * s.expansion, 1};
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

	painter->fillRect(QRectF(s.margin, 0, (qreal)features.size() * s.expansion, 1),
	                  highlight ? s.cursor : bg);

	for (unsigned i = 0; i < features.size(); ++i) {
		fg.setAlphaF(features[i]);
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

HeatmapScene::Marker::Marker(HeatmapScene *scene, unsigned sampleIndex, const QPointF &pos)
{
	auto title = scene->data.peek()->proteins[sampleIndex].name;
	auto color = scene->colorset[(int)qHash(title) % scene->colorset.size()];

	QBrush fill(QColor{0, 0, 0, 127});
	QPen outline(color.dark(300));
	outline.setCosmetic(true);
	backdrop.reset(scene->addRect({}));
	backdrop->setBrush(fill);
	backdrop->setPen(outline);

	line.reset(scene->addLine({}));
	QPen pen(color.darker(150));
	pen.setCosmetic(true);
	line->setPen(pen);

	// do label last, so it will be on top of its backdrop
	label.reset(scene->addSimpleText(title));
	auto font = label->font();
	font.setBold(true);
	label->setFont(font);
	label->setBrush(color);

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