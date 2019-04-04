#include "featweightsscene.h"

#include "colormap.h"

#include <QPainter>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

#include <opencv2/imgproc/imgproc.hpp>
#include <tbb/parallel_for_each.h>

#include <QDebug>

FeatweightsScene::FeatweightsScene(Dataset &data)
    : data(data)
{
	display = new QGraphicsPixmapItem;
	markerContour = new QGraphicsPathItem;
	weightBar = new WeightBar;
	// note: scene takes ownership and will clean them up
	addItem(display);
	addItem(markerContour);
	addItem(weightBar);

	qreal offset = .05; // some "feel good" borders
	qreal wb = .1; // offset for weightbar below
	setSceneRect({QPointF{-offset, -offset}, QPointF{1. + offset, 1. + offset + wb}});

	display->setShapeMode(QGraphicsPixmapItem::ShapeMode::BoundingRectShape);
	display->setCursor(Qt::CursorShape::CrossCursor);

	QPen pen(Qt::white);
	pen.setWidth(0);
	markerContour->setPen(pen);

	weightBar->setTransform(QTransform::fromTranslate(0, 1.05).scale(1., 0.05));
}

void FeatweightsScene::setDisplay()
{
	display->setPixmap(image);

	/* normalize display size on screen and also flip X-axis */
	auto br = display->boundingRect();
	auto scaleX = 1./br.width(), scaleY = 1./br.height();
	auto transform = QTransform::fromTranslate(0, 1).scale(scaleX, -scaleY);

	for (auto &i : std::vector<QGraphicsItem*>{display, markerContour}) {
		i->setTransform(transform);
		i->setVisible(true);
	}
}

void FeatweightsScene::computeWeights()
{
	auto d = data.peek();
	auto &feat = d->features;

	std::vector<double> score((unsigned)d->dimensions.size());
	tbb::parallel_for((size_t)0, score.size(), [&] (size_t dim) {
		for (auto& m : markers) {
			score[dim] += feat[(int)m][dim];
		}
	});
	auto total = cv::sum(score)[0];
	if (total < 0.001) {
		weights.assign((unsigned)score.size(), 1./(double)score.size());
	} else {
		std::for_each(score.begin(), score.end(), [s=1./total] (double &v) { v *= s; });
		weights = std::move(score);
	}

	computeImage();
	computeMarkerContour();
	setDisplay();
}

void FeatweightsScene::computeImage()
{
	cv::Size bins = {400, 400}; // TODO: adapt to screen
	cv::Size2d stepSize = {1./(bins.width), 1./(bins.height)};
	translate = [bins,stepSize] (cv::Point_<unsigned> idx) {
		return QPointF(idx.x * stepSize.width, idx.y * stepSize.height);
	};

	matrix = cv::Mat1f(bins, 0);

	auto d = data.peek();
	auto &feat = d->features;
	contours = std::vector<std::vector<unsigned>>((unsigned)feat.size(),
	                                              std::vector<unsigned>((unsigned)bins.width));

	/* go through critera x (0…1) and, for each protein, measure achieved score y
	 * using the features that pass critera. Then increment in matrix accordingly.
	 * Also, store the contour for each protein (in matrix-coordinates)
	 * Outer loop over x instead of proteins so threads do not interfer when writing
	 * to matrix */
	tbb::parallel_for(0, matrix.cols, [&] (int x) {
		for (int p = 0; p < feat.size(); ++p) {
			auto thresh = x * stepSize.width;
			double score = 0;
			for (unsigned dim = 0; dim < weights.size(); dim++) {
				if (feat[p][dim] >= thresh)
					score += weights[dim];
			}
			auto y = std::min((int)(score / stepSize.height), matrix.rows - 1);
			for (int yy = 0; yy <= y; ++yy)
				matrix(yy, x)++;
			contours[p][x] = y;
		}
	});

	cv::Mat matrixL;
	cv::log(matrix, matrixL);

	double scale = 255./std::log(feat.size()); // count in lower-left corner
	//scale = 255; // count in lower-left corner

	cv::Mat1b matrixB(matrix.rows, matrix.cols);
	matrixL.convertTo(matrixB, CV_8U, scale);

	cv::Mat3b colorMatrix;
	cv::applyColorMap(matrixB, colorMatrix, colormap::magma);

	/* finally make a pixmap item out of it */
	image = QPixmap::fromImage({colorMatrix.data, colorMatrix.cols, colorMatrix.rows,
	                            (int)colorMatrix.step, QImage::Format_RGB888});
}

void FeatweightsScene::computeMarkerContour()
{
	if (markers.empty()) {
		markerContour->setPath({});
		return;
	}

	QPolygonF target;
	// add two points for each bin
	for (unsigned x = 0; x < contours[0].size(); ++x) {
		auto y = std::numeric_limits<unsigned>::max();
		for (auto p : markers) {
			y = std::min(y, contours[p][x]);
		}
		target << QPointF(x, y+1) << QPointF(x+1, y+1);
	}

	QPainterPath p;
	p.addPolygon(target);
	markerContour->setPath(p);
}

void FeatweightsScene::reset(bool haveData)
{
	for (auto &i : std::vector<QGraphicsItem*>{display, markerContour, weightBar})
		i->setVisible(false);
	markers.clear();
	//dimensionLabels.clear();

	if (!haveData) {
		return;
	}

	// setup new dimension labels
	auto dim = data.peek()->dimensions; // QStringList COW
	//for (int i = 0; i < dim.size(); ++i)
	    //dimensionLabels.emplace_back(this, (qreal)(i+0.5)/dim.size(), dim[i]);

	computeWeights(); // will call computeImage(), setDisplay()
	weightBar->setVisible(true);
}

void FeatweightsScene::toggleMarker(unsigned sampleIndex, bool present)
{
	if (present)
		markers.insert(sampleIndex);
	else
		markers.erase(sampleIndex);

	computeWeights();
}

void FeatweightsScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	QGraphicsScene::mouseMoveEvent(event);
	if (event->isAccepted())
		return;

	if (!display->scene())
		return; // nothing displayed right now

	auto pos = display->mapFromScene(event->scenePos());

	/* check if cursor lies over matrix */
	// shrink width/height to avoid index out of bounds later
	auto inside = display->boundingRect().adjusted(0,0,-0.01,-0.01).contains(pos);
	if (!inside) {
		if (event->buttons() & Qt::RightButton)
			emit cursorChanged({});
		return;
	}

	// use floored coordinates, as everything in [0,1[ lies over pixel 0
	cv::Point_<unsigned> idx = {(unsigned)pos.x(), (unsigned)pos.y()};

	/* display current value */
	display->setToolTip(QString::number((double)matrix(idx), 'f', 0));

	/* emit cursor change */
	if (!(event->buttons() & Qt::RightButton))
		return;

	QVector<unsigned> luckyOnes;
	for (unsigned i = 0; i < contours.size(); ++i) {
		if (contours[i][idx.x] >= idx.y)
			luckyOnes.push_back(i);
	}
	auto real = translate(idx);
	auto caption = QString("Tr %1 / W %2").arg(real.x()).arg(real.y());
	emit cursorChanged(luckyOnes, caption);
}

void FeatweightsScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	if (event->button() == Qt::RightButton)
		mouseMoveEvent(event);
}

void FeatweightsScene::updateColorset(QVector<QColor> colors)
{
	colorset = colors;
}

FeatweightsScene::WeightBar::WeightBar(QGraphicsItem *parent)
    : QGraphicsItem(parent)
{
	setAcceptHoverEvents(true);
}

void FeatweightsScene::WeightBar::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*)
{
	/* go over all components and perform a drawing op */
	auto loop = [this] (auto func) {
		qreal offset = 0.;
		int index = 0;
		for (auto &w : scene()->weights) {
			func(index, QRectF(offset, 0, w, 1));
			offset += w; ++index;
		}
	};

	/* first fill components */
	auto colors = scene()->colorset;
	loop([&] (auto index, auto rect) {
		auto color = colors[index % colors.size()];
		painter->fillRect(rect, color);
	});
	/* second draw highlight rect */
	QPen pen(Qt::white);
	pen.setWidth(0);
	painter->setPen(pen);
	loop([&] (auto index, auto rect) {
		if (index == highlight)
			painter->drawRect(rect);
	});
	/* third draw text */
	painter->setPen(Qt::black);
	auto font = painter->font();
	font.setBold(true);
	painter->setFont(font);
	// need to hack around scaling so font is not warped, way too large
	qreal scale = 0.015;
	auto sceneScale = sceneTransform().mapRect(boundingRect());
	auto ratio = sceneScale.height()/sceneScale.width();
	painter->scale(ratio*scale, scale);
	auto t = QTransform::fromScale(1./(scale*ratio), 1./scale);
	loop([&] (auto, auto rect) {
		auto text = (rect.width() < 0.01 ? "/"
		                                 : QString::number(rect.width(), 'f', 2).mid(1));
		auto drawRect = t.mapRect(rect).adjusted(-20, 0, 20, 0); // let text overflow
		painter->drawText(drawRect, Qt::AlignCenter, text);
	});
}

void FeatweightsScene::WeightBar::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
	/* determine which component is hovered */
	auto x = event->pos().x();
	qreal offset = 0;
	int index = 0;
	for (auto &w : scene()->weights) {
		offset += w;
		if (x < offset)
			break;
		index++;
	}

	// set tooltip to reflect hovered component
	this->setToolTip(scene()->data.peek()->dimensions[index]);

	// highlight the hovered component
	highlight = index;
	update();
}

void FeatweightsScene::WeightBar::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	highlight = -1;
	update();
}