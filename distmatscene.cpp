#include "distmatscene.h"

#include <QPainter>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

#include <QtDebug>

DistmatScene::DistmatScene(Dataset &data)
    : data(data), clusterbars(this)
{
	display = new QGraphicsPixmapItem;
	display->setShapeMode(QGraphicsPixmapItem::ShapeMode::BoundingRectShape);
	display->setCursor(Qt::CursorShape::CrossCursor);
	addItem(display);

	qreal offset = .1; // some "feel good" borders
	setSceneRect({QPointF{-offset, -offset}, QPointF{1. + offset, 1. + offset}});
}

void DistmatScene::setViewport(const QRectF &rect, qreal scale)
{
	viewport = rect;
	vpScale = scale;
	rearrange();
}

void DistmatScene::setDisplay()
{
	std::map<Direction, Qt::TransformationMode> quality = {
	    {Direction::PER_PROTEIN, Qt::TransformationMode::SmoothTransformation},
	    {Direction::PER_DIMENSION, Qt::TransformationMode::FastTransformation}
	};
	display->setTransformationMode(quality[currentDirection]);

	display->setPixmap(matrices[currentDirection].image);

	/* normalize display size on screen and also flip Y-axis */
	auto scale = 1./display->boundingRect().width();
	display->setTransform(QTransform::fromTranslate(0, 1).scale(scale, -scale));
	display->setVisible(true);
}

void DistmatScene::setDirection(DistmatScene::Direction direction)
{
	if (direction == currentDirection && matrices.count(direction))
		return;

	currentDirection = direction;
	/* change visibility of all annotations accordingly
	 * note: we do not use individual signal handlers right now because these
	 * items are not QObjects and lambda slots would never be cleaned up.
	 */
	for (auto l : dimensionLabels)
		l->setVisible(direction == Direction::PER_DIMENSION);
	for (auto &[_, m] : markers)
		m->setVisible(direction == Direction::PER_PROTEIN);
	clusterbars.setVisible(direction == Direction::PER_PROTEIN
	                       && !data.peek()->clustering.empty());

	/* set display if available */
	if (matrices.count(direction)) {
		setDisplay();
		return;
	}

	/* otherwise compute it */
	matrices[direction] = Distmat();
	auto &m = matrices[direction];

	switch (direction) {
	case Direction::PER_PROTEIN:
		m.computeMatrix(data.peek()->features);
		reorder(); // calls setDisplay()
		break;
	case Direction::PER_DIMENSION:
		auto d = data.peek();
		// re-arrange data to obtain per-dimension feature vectors
		QVector<std::vector<double> >
		        features(d->dimensions.size(), std::vector<double>((size_t)d->features.size()));
		for (int i = 0; i < d->features.size(); ++i) {
			for (size_t j = 0; j < d->features[i].size(); ++j) {
				features[j][(size_t)i] = d->features[i][j];
			}
		}
		m.computeMatrix(features);
		m.computeImage([] (int y, int x) { return cv::Point(x, y); });
		setDisplay();
	}
}

void DistmatScene::reset(bool haveData)
{
	matrices.clear();
	display->setVisible(false);
	clusterbars.setVisible(false);
	for (auto l : dimensionLabels)
		delete l;
	dimensionLabels.clear();
	for (auto &[_, m] : markers)
		delete m;
	markers.clear();

	if (!haveData) {
		return;
	}

	// setup new dimension labels
	auto dim = data.peek()->dimensions; // QStringList COW
	for (int i = 0; i < dim.size(); ++i)
		dimensionLabels.push_back(new LegendItem(this, (qreal)(i+0.5)/dim.size(), dim[i]));

	// trigger computation (also set dimension label visibilty)
	setDirection(currentDirection);
}

void DistmatScene::reorder()
{
	// note: although we have nothing to do here for PER_DIMENSION, we keep the
	// state consistent for a future switch to PER_PROTEIN

	if (matrices.count(currentDirection)) {
		/* re-do display with current ordering */
		auto d = data.peek();
		matrices[currentDirection].computeImage([&d] (int y, int x) {
			return cv::Point(d->order.index[x], d->order.index[y]);
		});
		if (currentDirection == Direction::PER_PROTEIN)
			setDisplay();
	}

	/* reflect new order in clusterbars */
	recolor();

	/* reflect new order in markers (hack) */
	auto mCopy = markers;
	for (auto& [i, m] : mCopy) {
		removeMarker(i);
		addMarker(i);
	}
}

void DistmatScene::recolor()
{
	auto d = data.peek();
	auto &cl = d->clustering;
	if (cl.empty()) {
		// no clustering, disappear
		clusterbars.setVisible(false);
		return;
	}

	/* setup a colored bar that indicates cluster membership */
	const auto &source = d->order.index;
	QImage clusterbar(source.size(), 1, QImage::Format_ARGB32);
	for (int i = 0; i < (int)source.size(); ++i) {
		const auto &assoc = cl.memberships[source[i]];
		switch (assoc.size()) {
		case 0:
			clusterbar.setPixelColor(i, 0, Qt::transparent);
			break;
		case 1:
			clusterbar.setPixelColor(i, 0, cl.clusters[*assoc.begin()].color);
			break;
		default:
			clusterbar.setPixelColor(i, 0, Qt::white);
		}
	}

	clusterbars.update(clusterbar);
	clusterbars.setVisible(currentDirection == Direction::PER_PROTEIN);

	rearrange();
}

void DistmatScene::rearrange()
{
	/* rescale & shift clusterbars */
	QPointF margin{15.*vpScale, 15.*vpScale};
	auto topleft = viewport.topLeft() + margin;
	auto botright = viewport.bottomRight() - margin;
	qreal outerMargin = 10.*vpScale; // 10 pixels
	clusterbars.rearrange({topleft, botright}, outerMargin);

	/* rescale & shift labels */
	for (auto& [i, m] : markers)
		m->rearrange(viewport.left(), vpScale);
	for (auto l : dimensionLabels)
		l->rearrange(viewport.left(), vpScale);
}

void DistmatScene::addMarker(unsigned sampleIndex)
{
	if (markers.count(sampleIndex))
		return;

	// reverse-search in protein order
	auto d = data.peek();
	auto pos = d->order.rankOf[sampleIndex];
	auto coord = (qreal)(pos + 0.5) / d->proteins.size();

	markers[sampleIndex] = new Marker(this, sampleIndex, coord);
}

void DistmatScene::removeMarker(unsigned sampleIndex)
{
	if (!markers.count(sampleIndex))
		return;

	delete markers[sampleIndex];
	markers.erase(sampleIndex);
}

void DistmatScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	if (!display->scene())
		return; // nothing displayed right now

	auto pos = display->mapFromScene(event->scenePos());

	/* check if cursor lies over matrix */
	// shrink width/height to avoid index out of bounds later
	auto inside = display->boundingRect().adjusted(0,0,-0.01,-0.01).contains(pos);
	if (!inside) {
		if (currentDirection == Direction::PER_PROTEIN)
			emit cursorChanged({});
		return;
	}

	// use floored coordinates, as everything in [0,1[ lies over pixel 0
	cv::Point_<unsigned> idx = {(unsigned)pos.x(), (unsigned)pos.y()};
	if (currentDirection == Direction::PER_PROTEIN) {
		// need to back-translate
		auto d = data.peek();
		idx = {d->order.index[(size_t)pos.x()],
		       d->order.index[(size_t)pos.y()]};
	}

	/* display current value */
	auto &m = matrices[currentDirection].matrix;
	display->setToolTip(QString::number((double)m(idx), 'f', 2));

	if (currentDirection == Direction::PER_DIMENSION)
		return;

	/* emit cursor change */
	auto d = data.peek();
	emit cursorChanged({idx.x, idx.y});
}

void DistmatScene::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
	recolor();
	// TODO: re-initialize markers
}

DistmatScene::LegendItem::LegendItem(qreal coord) : coordinate(coord) {}
DistmatScene::LegendItem::LegendItem(DistmatScene *scene, qreal coord, QString title)
    : coordinate(coord)
{
	setup(scene, title, Qt::white);
}

void DistmatScene::LegendItem::setup(DistmatScene *scene, QString title, QColor color)
{
	label = scene->addSimpleText(title);
	label->setBrush(color);
	auto font = label->font();
	font.setBold(true);
	label->setFont(font);

	QBrush fill(QColor{0, 0, 0, 127});
	QPen outline(color.dark(300));
	outline.setCosmetic(true);
	backdrop = scene->addRect({});
	backdrop->setBrush(fill);
	backdrop->setPen(outline);

	line = scene->addLine({});
	QPen pen(color.darker(150));
	pen.setCosmetic(true);
	line->setPen(pen);

	rearrange(scene->viewport.left(), scene->vpScale);
}

DistmatScene::Marker::Marker(DistmatScene *scene, unsigned sampleIndex, qreal coord)
    : LegendItem(coord), sampleIndex(sampleIndex)
{
	auto title = scene->data.peek()->proteins[sampleIndex].name;
	auto color = scene->colorset[(int)qHash(title) % scene->colorset.size()];
	setup(scene, title, color);
	setVisible(scene->currentDirection == Direction::PER_PROTEIN);
}

void DistmatScene::LegendItem::setVisible(bool visible)
{
	for (auto i : std::initializer_list<QGraphicsItem*>{backdrop, line, label})
		i->setVisible(visible);
}

void DistmatScene::LegendItem::rearrange(qreal right, qreal scale)
{
	auto vCenter = 1. - coordinate; // flip
	auto linewidth = 15.*scale;
	auto margin = 2.*scale;

	// invert zoom for label
	label->setScale(scale);
	auto labelSize = label->sceneBoundingRect().size();

	// move label into view
	auto left = std::max(right + margin,
	                     -(labelSize.width() + margin + linewidth));
	label->setPos(left, vCenter - labelSize.height()/2.);
	backdrop->setRect(label->sceneBoundingRect()
	                  .adjusted(-margin, -margin, margin, margin));

	// keep line in place, but adjust length
	line->setLine({{-linewidth, vCenter}, {0., vCenter}});
}

DistmatScene::Clusterbars::Clusterbars(DistmatScene *scene)
{
	for (auto i : {Qt::TopEdge, Qt::LeftEdge, Qt::BottomEdge, Qt::RightEdge}) {
		auto c = new QGraphicsPixmapItem;
		c->setShapeMode(QGraphicsPixmapItem::ShapeMode::BoundingRectShape);
		c->setTransformationMode(Qt::TransformationMode::FastTransformation);
		scene->addItem(c);
		items[i] = c;
	}
}

void DistmatScene::Clusterbars::update(QImage content)
{
	/* scale and orient bars to fit around the [0, 0 – 1, 1] matrix item */
	auto length = content.width();
	std::map<Qt::Edge, QTransform> transforms = {
	    {Qt::TopEdge, QTransform::fromScale(1./length, -.025)},
	    {Qt::LeftEdge, QTransform::fromTranslate(0, 1).scale(.025, -1./length).rotate(90)},
	    {Qt::BottomEdge, QTransform::fromScale(1./length, .025)},
	    {Qt::RightEdge, QTransform::fromTranslate(0, 1).scale(-.025, -1./length).rotate(90)}
	};

	auto p = QPixmap::fromImage(content);
	for (auto& [k, v] : items) {
		v->setPixmap(p);
		v->setTransform(transforms[k]);
	}
}

void DistmatScene::Clusterbars::setVisible(bool visible)
{
	for (auto& [k, v] : items)
		v->setVisible(visible);
}

void DistmatScene::Clusterbars::rearrange(QRectF target, qreal margin)
{
	/* shift colorbars into view */
	for (auto& [k, v] : items) {
		auto pos = v->pos();
		switch (k) {
		case (Qt::TopEdge):
			pos.setY(std::max(-margin, target.top()));
			break;
		case (Qt::BottomEdge):
			pos.setY(std::min(1. + margin, target.bottom()));
			break;
		case (Qt::LeftEdge):
			pos.setX(std::max(-margin, target.left()));
			break;
		case (Qt::RightEdge):
			pos.setX(std::min(1. + margin, target.right()));
			break;
		}
		v->setPos(pos);
	}
}
