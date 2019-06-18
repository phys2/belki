#include "featweightsscene.h"
#include "colormap.h"
#include "compute/features.h"

#include <QPainter>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

#include <opencv2/imgproc/imgproc.hpp>
#include <tbb/parallel_for_each.h>

#include <QDebug>

FeatweightsScene::FeatweightsScene(Dataset::Ptr data)
    : data(data)
{
	qRegisterMetaType<FeatweightsScene::Weighting>();

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

	computeWeights();
}

void FeatweightsScene::setDisplay()
{
	display->setPixmap(images[imageIndex]);

	/* normalize display size on screen and also flip X-axis */
	auto br = display->boundingRect();
	auto scaleX = 1./br.width(), scaleY = 1./br.height();
	auto transform = QTransform::fromTranslate(0, 1).scale(scaleX, -scaleY);

	for (auto &i : std::vector<QGraphicsItem*>{display, markerContour})
		i->setTransform(transform);
}

void FeatweightsScene::computeWeights()
{
	auto d = data->peek<Dataset::Base>();
	auto len = (unsigned)d->dimensions.size();
	// use original data if no score threshold was applied
	bool haveClipped = !clippedFeatures.empty();
	const auto &feat = (haveClipped ? clippedFeatures : d->features);
	if (haveClipped)
		d.unlock(); // early unlock, feat does not point into dataset

	weights.clear();

	/* calculate weights if appr. method selected and markers available */
	if (weighting != Weighting::UNWEIGHTED && !markers.empty()) {
		/* compose set of voters by all marker proteins found in current dataset */
		std::vector<const std::vector<double>*> voters;
		for (auto& m : markers) {
			try { voters.push_back(&feat[m]); } catch (...) {}
		}

		/* setup weighters */
		std::map<Weighting, std::function<void(size_t)>> weighters;
		weighters[Weighting::ABSOLUTE] = [&] (size_t dim) {
			for (auto f : voters) {
				weights[dim] += (*f)[dim];
			}
		};
		weighters[Weighting::RELATIVE] = [&] (size_t dim) { // weight against own baseline
			// collect baseline first
			double baseline = std::accumulate(feat.cbegin(), feat.cend(), 0.,
			                                  [dim,n=1./feat.size()] (auto a, auto &p) {
				return a += p[dim] * n;
			});

			for (auto f : voters) {
				auto value = (*f)[dim];
				if (value > baseline)
					weights[dim] += value / baseline;
			}
		};
		weighters[Weighting::OFFSET] = [&] (size_t dim) { // weight against competition's baseline
			for (auto f : voters) {
				// collect per-marker baseline first
				double baseline = 0;
				auto n = 1./(weights.size() - 1);
				for (unsigned i = 0; i < weights.size(); ++i) {
					if (i != dim)
						baseline = std::max(baseline, (*f)[i] * n);
				}
				if (baseline < 0.001)
					baseline = 1.;
				auto value = (*f)[dim];
				if (value > baseline)
					weights[dim] += value / baseline;
			}
		};

		/* apply weighting and normalize */
		weights.resize(len, 0.);
		tbb::parallel_for((size_t)0, weights.size(), weighters[weighting]);
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

	computeImage(feat);

	d.unlock();
	computeMarkerContour();
	setDisplay();
}

void FeatweightsScene::computeImage(const features::vec& feat)
{
	cv::Size bins = {400, 400}; // TODO: adapt to screen
	cv::Size2d stepSize = {1./(bins.width), 1./(bins.height)};
	translate = [bins,stepSize] (cv::Point_<unsigned> idx) {
		return QPointF(idx.x * stepSize.width, idx.y * stepSize.height);
	};

	matrix = cv::Mat1f(bins, 0);
	cv::Mat1f relmatrix(bins, 0);

	contours = std::vector<std::vector<unsigned>>(feat.size(),
	                                              std::vector<unsigned>((unsigned)bins.width));

	/* go through critera x (0…1) and, for each protein, measure achieved score y
	 * using the features that pass critera. Then increment in matrices accordingly.
	 * Also, store the contour for each protein (in matrix-coordinates)
	 * Outer loop over x instead of proteins so threads do not interfer when writing
	 * to matrix */
	tbb::parallel_for(0, matrix.cols, [&] (int x) {
		for (size_t p = 0; p < feat.size(); ++p) {
			auto thresh = x * stepSize.width;
			double score = 0;
			for (unsigned dim = 0; dim < weights.size(); dim++) {
				if (feat[p][dim] >= thresh)
					score += weights[dim];
			}
			auto y = std::min((int)(score / stepSize.height), matrix.rows - 1);
			for (int yy = 0; yy <= y; ++yy) // increase absolute count
				matrix(yy, x)++;
			if (markers.count(p)) {
				for (int yy = 0; yy <= y; ++yy) // increase relative count
					relmatrix(yy, x)++;
			}
			contours[p][x] = y;
		}
	});

	// apply on abs. matrix (use log-scale)
	cv::Mat matrixL;
	cv::log(matrix, matrixL);
	double scale = 1./std::log(feat.size()); // max. count (in lower-left corner)
	images[0] = Colormap::pixmap(Colormap::magma.apply(matrixL, scale));

	// apply on rel. matrix
	cv::Mat matrixR = relmatrix / matrix;
	images[1] = Colormap::pixmap(Colormap::magma.apply(matrixR, 1.));
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

void FeatweightsScene::applyScoreThreshold(double threshold)
{
	if (std::isnan(threshold)) {
		clippedFeatures.clear();
	} else {
		auto d = data->peek<Dataset::Base>();
		clippedFeatures = d->features;
		features::apply_cutoff(clippedFeatures, d->scores, threshold);
		d.unlock();
	}

	computeWeights();
}

void FeatweightsScene::updateMarkers()
{
	auto d = data->peek<Dataset::Base>();
	auto p = data->peek<Dataset::Proteins>();
	std::set<unsigned> newMarkers;
	for (auto &m : p->markers) {
		try {
			newMarkers.insert(d->protIndex.at(m));
		} catch (std::out_of_range&) {}
	}
	p.unlock();
	d.unlock();

	if (newMarkers == markers)
		return;

	markers = std::move(newMarkers);
	computeWeights();
}

void FeatweightsScene::toggleMarkers(const std::vector<ProteinId> &ids, bool present)
{
	for (auto id : ids) {
		try {
			auto index = data->peek<Dataset::Base>()->protIndex.at(id);
			if (present)
				markers.insert(index);
			else
				markers.erase(index);
		} catch (std::out_of_range&) {}
	}

	computeWeights();
}

void FeatweightsScene::toggleImage(bool useAlternate)
{
	imageIndex = (useAlternate ? 1 : 0);
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

void FeatweightsScene::setWeighting(Weighting w)
{
	if (weighting == w)
		return;

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
	// need to hack around scaling so font is not warped, way too large
	qreal scale = 0.02;
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
	this->setToolTip(scene()->data->peek<Dataset::Base>()->dimensions.at(index));

	// highlight the hovered component
	highlight = index;
	update();
}

void FeatweightsScene::WeightBar::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
	highlight = -1;
	update();
}
