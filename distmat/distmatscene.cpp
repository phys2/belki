#include "distmatscene.h"

#include <QPainter>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

#include <QtDebug>

DistmatScene::DistmatScene(Dataset &data, bool dialogMode)
    : data(data),
      dialogMode(dialogMode),
      clusterbars(this)
{
	display = new QGraphicsPixmapItem;
	display->setShapeMode(QGraphicsPixmapItem::ShapeMode::BoundingRectShape);
	if (!dialogMode)
		display->setCursor(Qt::CursorShape::CrossCursor);
	addItem(display); // scene takes ownership and will clean it up

	qreal offset = (dialogMode ? .01 : .1); // some "feel good" borders
	QRectF rect{QPointF{-offset, -offset}, QPointF{1. + offset, 1. + offset}};
	if (dialogMode) // hack: provide extra space for labels, educated guess
		rect.adjust(-1., 0, 0, 0);
	setSceneRect(rect);
}

void DistmatScene::setViewport(const QRectF &rect, qreal scale)
{
	GraphicsScene::setViewport(rect, scale);

	rearrange();
	updateRenderQuality();
}

void DistmatScene::setDisplay()
{
	display->setPixmap(matrices[currentDirection].image);

	/* normalize display size on screen and also flip Y-axis */
	auto scale = 1./display->boundingRect().width();
	display->setTransform(QTransform::fromTranslate(0, 1).scale(scale, -scale));
	updateRenderQuality();
	display->setVisible(true);
}

void DistmatScene::setDirection(DistmatScene::Direction direction)
{
	if (direction == currentDirection && matrices.count(direction))
		return;

	currentDirection = direction;
	if (dimensionLabels.empty())
		return; // na data yet!

	updateVisibilities();

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
		std::vector<std::vector<double>>
		        features((size_t)d->dimensions.size(), std::vector<double>(d->features.size()));
		for (size_t i = 0; i < d->features.size(); ++i) {
			for (size_t j = 0; j < d->features[i].size(); ++j) {
				features[j][i] = d->features[i][j];
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
	clusterbars.update({}); // clears
	dimensionLabels.clear();
	dimensionSelected.clear();
	markers.clear();

	if (!haveData) {
		return;
	}

	// setup new dimension labels
	auto dim = data.peek()->dimensions; // QStringList COW
	dimensionSelected.resize((size_t)dim.size(), true); // all dims selected by default
	for (int i = 0; i < dim.size(); ++i)
		dimensionLabels.try_emplace((size_t)i, this, (qreal)(i+0.5)/dim.size(), dim.at(i));

	// trigger computation (also set dimension label visibilty)
	setDirection(currentDirection);
}

void DistmatScene::reorder()
{
	// note: although we have nothing to do here for PER_DIMENSION, we keep the
	// state consistent for a future switch to PER_PROTEIN

	if (matrices.count(Direction::PER_PROTEIN)) {
		/* re-do display with current ordering */
		auto d = data.peek();
		matrices[Direction::PER_PROTEIN].computeImage([&d] (int y, int x) {
			return cv::Point(d->order.index[x], d->order.index[y]);
		});
		if (currentDirection == Direction::PER_PROTEIN)
			setDisplay();
	}

	/* reflect new order in clusterbars */
	recolor();

	/* reflect new order in markers */
	for (auto& [i, m] : markers) {
		m.coordinate = computeCoord(m.sampleIndex);
		m.rearrange(viewport.left(), vpScale);
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
		const auto &assoc = cl.memberships[source[(size_t)i]];
		switch (assoc.size()) {
		case 0:
			clusterbar.setPixelColor(i, 0, Qt::transparent);
			break;
		case 1:
			clusterbar.setPixelColor(i, 0, cl.clusters.at(*assoc.begin()).color);
			break;
		default:
			clusterbar.setPixelColor(i, 0, Qt::white);
		}
	}

	clusterbars.update(clusterbar);
	rearrange();
	updateVisibilities();
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
	for (auto &[_, m] : markers)
		m.rearrange(viewport.left(), vpScale);
	for (auto &[_, l] : dimensionLabels)
		l.rearrange(viewport.left(), vpScale);
}

void DistmatScene::updateVisibilities()
{
	for (auto &[i, l] : dimensionLabels)
		l.setVisible(currentDirection == Direction::PER_DIMENSION && dimensionSelected[i]);
	for (auto &[_, m] : markers)
		m.setVisible(currentDirection == Direction::PER_PROTEIN);
	clusterbars.setVisible(showPartitions && currentDirection == Direction::PER_PROTEIN);
}

void DistmatScene::updateRenderQuality()
{
	auto pixelWidth = display->mapToScene({1, 1}).x() / vpScale;
	if (pixelWidth < 2)
		display->setTransformationMode(Qt::TransformationMode::SmoothTransformation);
	else
		display->setTransformationMode(Qt::TransformationMode::FastTransformation);
	display->update();
}

void DistmatScene::toggleMarker(unsigned sampleIndex, bool present)
{
	if (present)
		markers.try_emplace(sampleIndex, this, sampleIndex);
	else
		markers.erase(sampleIndex);
}

void DistmatScene::togglePartitions(bool show)
{
	showPartitions = show;
	updateVisibilities();
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

void DistmatScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	if (dialogMode && display->scene() && currentDirection == Direction::PER_DIMENSION) {
		// see mouseMoveEvent()
		auto y = (unsigned)display->mapFromScene(event->scenePos()).y();
		if (y < dimensionSelected.size()) {
			dimensionSelected[y] = !dimensionSelected[y];
			updateVisibilities();
			emit selectionChanged(dimensionSelected);
			event->accept();
		}
	}

	QGraphicsScene::mouseReleaseEvent(event);
}

void DistmatScene::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
	recolor();
	// TODO: re-initialize markers
}

qreal DistmatScene::computeCoord(unsigned sampleIndex)
{
	auto d = data.peek();
	auto pos = d->order.rankOf[sampleIndex];
	return (qreal)(pos + 0.5) / d->proteins.size();
}

DistmatScene::LegendItem::LegendItem(qreal coord) : coordinate(coord) {}
DistmatScene::LegendItem::LegendItem(DistmatScene *scene, qreal coord, QString title)
    : coordinate(coord)
{
	setup(scene, title, Qt::white);
}

void DistmatScene::LegendItem::setup(DistmatScene *scene, QString title, QColor color)
{
	QColor bgColor{0, 0, 0, 127};
	if (scene->dialogMode) {
		color = Qt::black;
		bgColor = {255, 255, 255, 191};
	}

	line.reset(scene->addLine({}));
	QPen pen(color.darker(150));
	pen.setCosmetic(true);
	line->setPen(pen);

	QBrush fill(bgColor);
	QPen outline(color.dark(300));
	outline.setCosmetic(true);
	backdrop.reset(scene->addRect({}));
	backdrop->setBrush(fill);
	backdrop->setPen(outline);

	// do label last, so it will be on top of its backdrop
	label.reset(scene->addSimpleText(title));
	auto font = label->font();
	font.setBold(true);
	label->setFont(font);
	label->setBrush(color);

	rearrange(scene->viewport.left(), scene->vpScale);
}

DistmatScene::Marker::Marker(DistmatScene *scene, unsigned sampleIndex)
    : LegendItem(scene->computeCoord(sampleIndex)), sampleIndex(sampleIndex)
{
	auto meta = scene->data.peek()->proteins[sampleIndex];
	setup(scene, meta.name, meta.color);
	setVisible(scene->currentDirection == Direction::PER_PROTEIN);
}

void DistmatScene::LegendItem::setVisible(bool visible)
{
	for (auto i : std::initializer_list<QGraphicsItem*>{backdrop.get(), line.get(), label.get()})
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
	if (content.isNull()) {
		valid = false;
		return;
	}

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
	valid = true;
}

void DistmatScene::Clusterbars::setVisible(bool visible)
{
	for (auto& [k, v] : items)
		v->setVisible(visible && valid);
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
