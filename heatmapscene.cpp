#include "heatmapscene.h"

#include <QPainter>
#include <QGraphicsItem>

#include <QtDebug>

HeatmapScene::HeatmapScene(Dataset &data) : data(data)
{

}

void HeatmapScene::reset(bool haveData)
{
	clear(); // removes & deletes all items
	if (!haveData) {
		profiles.clear();
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

	// TODO: reorder using hier. order
}

void HeatmapScene::rearrange(QSize viewport)
{
	if (profiles.empty())
		return;

	auto columnWidth = profiles[0]->boundingRect().width();
	auto aspect = (viewport.width() / columnWidth) / viewport.height();
	unsigned columns = (unsigned)std::floor(std::sqrt(profiles.size() * aspect));

	rearrange(columns);
}

void HeatmapScene::rearrange(unsigned columns)
{
	if (!columns || profiles.empty())
		return;

	unsigned rows = (unsigned)std::ceil(profiles.size() / (float)columns);
	auto columnWidth = profiles[0]->boundingRect().width();
	QSizeF box(columnWidth * columns, rows);

	setSceneRect({{0, 0}, box});

	for (unsigned i = 0; i < profiles.size(); ++i) {
		profiles[i]->setPos((i / rows) * columnWidth, i % rows);
	}
}

void HeatmapScene::recolor()
{
	auto d = data.peek();
	for (unsigned i = 0; i < profiles.size(); ++i) {
		const auto &assoc = d->proteins[i].memberOf;
		if (assoc.size() != 1) {
			profiles[i]->setBrush(Qt::transparent);
			continue;
		}
		profiles[i]->setBrush(d->clustering[*assoc.begin()].color);
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
	return {0, 0, 2.*s.margin + (qreal)features.size() * s.expansion, 1};
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

	for (int i = 0; i < features.size(); ++i) {
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
