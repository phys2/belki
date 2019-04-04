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

	QPen pen(Qt::green);
	pen.setWidth(0);
	markerContour->setPen(pen);

	weightBar->setTransform(QTransform::fromTranslate(0, 1.05).scale(1., 0.05));
}

void FeatweightsScene::setDisplay()
{
	display->setPixmap(displayImage2 ? image2 : image);

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
	auto len = (unsigned)d->dimensions.size();
	if (!len)
		return;

	weights.clear();

	/* calculate weights if appr. method selected and markers available */
	if (weighting > 0 && !markers.empty()) {
		auto &feat = d->features;

		using W = std::function<void(size_t)>;
		W simpleWeighter = [&] (size_t dim) {
			for (auto& m : markers) {
				weights[dim] += feat[(int)m][dim];
			}
		};
		W relativeWeighter = [&] (size_t dim) { // weight against own baseline
			// collect baseline first
			double baseline = std::accumulate(feat.begin(), feat.end(), 0.,
			                                  [dim,n=1./feat.size()] (auto a, auto &p) {
				return a += p[dim] * n;
			});

			for (auto& m : markers) {
				auto value = feat[(int)m][dim];
				if (value > baseline)
					weights[dim] += value / baseline;
			}
		};
		W bullyWeighter = [&] (size_t dim) { // weight against competition's baseline
			for (auto& m : markers) {
				// collect per-marker baseline first
				double baseline = 0;
				auto n = 1./(weights.size() - 1);
				for (unsigned d = 0; d < weights.size(); ++d) {
					if (d != dim)
						baseline += feat[(int)m][d] * n;
				}
				if (baseline < 0.001)
					baseline = 1.;
				auto value = feat[(int)m][dim];
				if (value > baseline)
					weights[dim] += value / baseline;
			}
		};
		std::vector<W> weighters{simpleWeighter, relativeWeighter, bullyWeighter};

		weights.resize(len, 0.);
		tbb::parallel_for((size_t)0, weights.size(), weighters[weighting - 1]);
		auto total = cv::sum(weights)[0];
		if (total > 0.001) {
			std::for_each(weights.begin(), weights.end(), [s=1./total] (double &v) { v *= s; });
		} else {
			weights.clear(); // undo our useless weights
		}
	}

	/* fallback to unweighted */
	if (weights.empty())
		weights.assign(len, 1./(double)len);

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
	cv::Mat1f matrix2(bins, 0);

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
			if (markers.count(p)) {
				for (int yy = 0; yy <= y; ++yy)
					matrix2(yy, x)++;
			}
			contours[p][x] = y;
		}
	});

	/* creates a heatmap image */
	auto pixifier = [&] (cv::Mat &source, double scale, QPixmap& target) {
		cv::Mat1b matrixB(source.rows, source.cols);
		source.convertTo(matrixB, CV_8U, 255. * scale);

		cv::Mat3b colorMatrix;
		cv::applyColorMap(matrixB, colorMatrix, colormap::magma);

		// finally make a pixmap item out of it
		target = QPixmap::fromImage({colorMatrix.data, colorMatrix.cols, colorMatrix.rows,
		                             (int)colorMatrix.step, QImage::Format_RGB888});
	};

	// first matrix
	cv::Mat matrixL;
	cv::log(matrix, matrixL);
	double scale = 1./std::log(feat.size()); // count in lower-left corner
	pixifier(matrixL, scale, image);

	/* create heatmap image for second matrix */
	cv::Mat matrixR = matrix2 / matrix;
	pixifier(matrixR, 1., image2);
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
	//auto dim = data.peek()->dimensions; // QStringList COW
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

void FeatweightsScene::toggleImage(bool useSecond)
{
	displayImage2 = useSecond;
	if (display->isVisible())
		setDisplay(); // refresh
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

void FeatweightsScene::changeWeighting(int w)
{
	weighting = w;
	computeWeights();
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
