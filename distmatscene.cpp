#include "distmatscene.h"

#include <QPainter>
#include <QGraphicsPixmapItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

#include <opencv2/imgproc/imgproc.hpp>
#include <tbb/parallel_for_each.h>

#include <QtDebug>

std::array<cv::Vec3b, 256> colormap(); // see end of file

DistmatScene::DistmatScene(Dataset &data) : data(data)
{
	display = new QGraphicsPixmapItem;
	display->setShapeMode(QGraphicsPixmapItem::ShapeMode::BoundingRectShape);
	display->setTransformationMode(Qt::TransformationMode::SmoothTransformation);
	display->setCursor(Qt::CursorShape::CrossCursor);
	addItem(display);

	for (auto i : {Qt::TopEdge, Qt::LeftEdge, Qt::BottomEdge, Qt::RightEdge}) {
		auto c = new QGraphicsPixmapItem;
		c->setShapeMode(QGraphicsPixmapItem::ShapeMode::BoundingRectShape);
		c->setTransformationMode(Qt::TransformationMode::FastTransformation);
		addItem(c);
		clusterbars[i] = c;
	}

	// some "feel good" borders
	qreal offset = .1;
	setSceneRect({QPointF{-offset, -offset}, QPointF{1. + offset, 1. + offset}});
}

void DistmatScene::setViewport(const QRectF &rect, qreal scale)
{
	   viewport = rect;
	   vpScale = scale;
	   rearrange();
}

std::map<DistmatScene::Measure, std::function<double (const std::vector<double> &, const std::vector<double> &)> > DistmatScene::measures()
{
	auto distL2 = [] (const auto &a, const auto &b) {
		return cv::norm(a, b, cv::NORM_L2);
	};
	auto distCrossCorr = [] (const auto &a, const auto &b) {
		double corr1 = 0., corr2 = 0., crosscorr = 0.;
		for (unsigned i = 0; i < a.size(); ++i) {
			auto v1 = a[i], v2 = b[i];
			corr1 += v1*v1;
			corr2 += v2*v2;
			crosscorr += v1*v2;
		}
		return crosscorr / (sqrt(corr1) * sqrt(corr2));
	};
	auto distPearson = [&] (const auto &a, const auto &b) {
		std::vector<double> aa, bb;
		cv::subtract(a, cv::mean(a), aa);
		cv::subtract(b, cv::mean(b), bb);
		return distCrossCorr(aa, bb);
	};

	return {
		{Measure::NORM_L2, distL2},
		{Measure::CROSSCORREL, distCrossCorr},
		{Measure::PEARSON, distPearson},
	};
}

void DistmatScene::reset(bool haveData)
{
	display->setVisible(false);
	for (auto &c : clusterbars)
		c.second->setVisible(false);
	if (!haveData) {
		return;
	}

	auto d = data.peek();
	auto sidelen = d->features.size();
	distmat = cv::Mat1f(sidelen, sidelen);

	/* amass all the combinations we need for filling a symmetric matrix */
	std::vector<cv::Point_<size_t>> coords;
	for (size_t y = 0; y < (unsigned)sidelen; ++y) {
		for (size_t x = 0; x <= y; ++x)
			coords.push_back({x, y});
	}

	/* get the work done in parallel */
	auto m = measures()[measure];
	tbb::parallel_for((size_t)0, coords.size(), [&] (size_t i) {
		auto c = coords[i];
		const auto &a = d->features[c.x], &b = d->features[c.y];
		distmat(c) = distmat(c.x, c.y) = (float)m(a, b);
	});

	reorder();
}

/* re-create img and display from mat, using current protein order */
void DistmatScene::reorder()
{
	auto d = data.peek();

	/* determine shift & scale to fit into uchar */
	double minVal, maxVal;
	switch (measure) {
	case Measure::PEARSON:
		minVal = -1.;
		maxVal = 1.;
		break;
	case Measure::CROSSCORREL:
		minVal = 0.;
		maxVal = 1.;
		break;
	default:
		cv::minMaxLoc(distmat, &minVal, &maxVal);
	}
	double scale = 255./(maxVal - minVal);

	auto translate = [&d] (int y, int x) {
		return cv::Point(d->proteinOrder[x], d->proteinOrder[y]);
	};

	/* convert to Mat1b and reorder at the same time */
	cv::Mat1b distmatB(distmat.rows, distmat.cols);
	std::vector<cv::Point_<size_t>> coords;
	tbb::parallel_for(0, distmat.rows, [&] (int y) {
		for (int x = 0; x <= y; ++x)
			distmatB(y, x) = distmatB(x, y)
			        = (uchar)((distmat(translate(y, x)) - minVal)*scale);
	});

	cv::applyColorMap(distmatB, distimg, colormap());

	/* finally make a pixmap item out of it */
	display->setPixmap(
	            QPixmap::fromImage({distimg.data, distimg.cols, distimg.rows,
	                                (int)distimg.step, QImage::Format_RGB888}));

	/* normalize display size on screen and also flip Y-axis */
	scale = 1./display->boundingRect().width();
	display->setTransform(QTransform::fromTranslate(0, 1).scale(scale, -scale));
	display->setVisible(true);

	/* reflect new order in clusterbars */
	recolor();

	/* reflect new order in markers (hack) */
	auto mCopy = markers;
	for (auto& [i, m] : mCopy) {
		removeMarker(i);
		addMarker(i);
	}
}

/* draw colored bars around matrix that indicate cluster membership */
void DistmatScene::recolor()
{
	auto d = data.peek();
	if (d->clustering.empty()) {
		// no clustering, disappear
		for (auto &c : clusterbars)
			c.second->setVisible(false);
		return;
	}

	const auto &source = d->proteinOrder;
	QImage clusterbar(source.size(), 1, QImage::Format_ARGB32);
	for (unsigned i = 0; i < source.size(); ++i) {
		const auto &assoc = d->proteins[source[i]].memberOf;
		switch (assoc.size()) {
		case 0:
			clusterbar.setPixelColor(i, 0, Qt::transparent);
			break;
		case 1:
			clusterbar.setPixelColor(i, 0, d->clustering[*assoc.begin()].color);
			break;
		default:
			clusterbar.setPixelColor(i, 0, Qt::white);
		}
	}

	std::map<Qt::Edge, QTransform> transforms = {
	    {Qt::TopEdge, QTransform::fromScale(1./source.size(), -.025)},
	    {Qt::LeftEdge, QTransform::fromTranslate(0, 1).scale(.025, -1./source.size()).rotate(90)},
	    {Qt::BottomEdge, QTransform::fromScale(1./source.size(), .025)},
	    {Qt::RightEdge, QTransform::fromTranslate(0, 1).scale(-.025, -1./source.size()).rotate(90)}
	};
	for (auto& [i, c] : clusterbars) {
		c->setPixmap(QPixmap::fromImage(clusterbar));
		c->setTransform(transforms[i]);
		c->setVisible(true);
	}

	rearrange();
}

void DistmatScene::addMarker(unsigned sampleIndex)
{
	if (markers.count(sampleIndex))
		return;

	// reverse-search in protein order
	auto d = data.peek();
	auto it = std::find(d->proteinOrder.begin(), d->proteinOrder.end(), sampleIndex);
	// note: all proteins are always in the order! we do not check right now
	auto pos = it - d->proteinOrder.begin();
	auto coordY = 1. - ((qreal)pos / distmat.rows);

	markers[sampleIndex] = new Marker(sampleIndex, coordY, this);
}

void DistmatScene::removeMarker(unsigned sampleIndex)
{
	if (!markers.count(sampleIndex))
		return;

	delete markers[sampleIndex];
	markers.erase(sampleIndex);
}

void DistmatScene::rearrange()
{
	QPointF margin{15.*vpScale, 15.*vpScale};
	auto topleft = viewport.topLeft() + margin;
	auto botright = viewport.bottomRight() - margin;

	/* shift colorbars into view */
	for (auto& [i, c] : clusterbars) {
		auto pos = c->pos();
		switch (i) {
		case (Qt::TopEdge):
			pos.setY(std::max(-10.*vpScale, topleft.y()));
			break;
		case (Qt::BottomEdge):
			pos.setY(std::min(1. + 10.*vpScale, botright.y()));
			break;
		case (Qt::LeftEdge):
			pos.setX(std::max(-10.*vpScale, topleft.x()));
			break;
		case (Qt::RightEdge):
			pos.setX(std::min(1. + 10.*vpScale, botright.x()));
			break;
		}
		c->setPos(pos);
	}
}

void DistmatScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	if (!display->scene())
		return; // nothing displayed right now

	auto pos = display->mapFromScene(event->scenePos());

	/* check if cursor lies over matrix */
	// shrink width/height to avoid index out of bounds later
	if (!display->boundingRect().adjusted(0,0,-0.01,-0.01).contains(pos)) {
		emit cursorChanged({});
		return;
	}

	auto d = data.peek();

	// use floored coordinates, as everything in [0,1[ lies over pixel 0
	emit cursorChanged({d->proteinOrder[(unsigned)pos.x()],
	                    d->proteinOrder[(unsigned)pos.y()]});
}

DistmatScene::Marker::Marker(unsigned sampleIndex, qreal coordY, DistmatScene *scene)
{
	auto scale = scene->vpScale;
	auto title = scene->data.peek()->proteins[sampleIndex].name;
	label = new QGraphicsSimpleTextItem(title);
	scene->addItem(label);
	label->setBrush(Qt::white);
	label->setScale(1. * scale);
	auto dimensions = label->sceneBoundingRect().size();
	label->setPos(-(dimensions.width() + 12.*scale),
	             coordY - dimensions.height()/2.);
	line = new QGraphicsLineItem({{-10.*scale, coordY}, {0., coordY}});
	QPen pen(Qt::white);
	pen.setCosmetic(true);
	line->setPen(pen);
	scene->addItem(line);
}

std::array<cv::Vec3b, 256> colormap() {
	/* Magma map from https://github.com/BIDS/colormap/blob/master/colormaps.py */
	return {{
			{0, 0, 4},
			{1, 0, 5},
			{1, 1, 6},
			{1, 1, 8},
			{2, 1, 9},
			{2, 2, 11},
			{2, 2, 13},
			{3, 3, 15},
			{3, 3, 18},
			{4, 4, 20},
			{5, 4, 22},
			{6, 5, 24},
			{6, 5, 26},
			{7, 6, 28},
			{8, 7, 30},
			{9, 7, 32},
			{10, 8, 34},
			{11, 9, 36},
			{12, 9, 38},
			{13, 10, 41},
			{14, 11, 43},
			{16, 11, 45},
			{17, 12, 47},
			{18, 13, 49},
			{19, 13, 52},
			{20, 14, 54},
			{21, 14, 56},
			{22, 15, 59},
			{24, 15, 61},
			{25, 16, 63},
			{26, 16, 66},
			{28, 16, 68},
			{29, 17, 71},
			{30, 17, 73},
			{32, 17, 75},
			{33, 17, 78},
			{34, 17, 80},
			{36, 18, 83},
			{37, 18, 85},
			{39, 18, 88},
			{41, 17, 90},
			{42, 17, 92},
			{44, 17, 95},
			{45, 17, 97},
			{47, 17, 99},
			{49, 17, 101},
			{51, 16, 103},
			{52, 16, 105},
			{54, 16, 107},
			{56, 16, 108},
			{57, 15, 110},
			{59, 15, 112},
			{61, 15, 113},
			{63, 15, 114},
			{64, 15, 116},
			{66, 15, 117},
			{68, 15, 118},
			{69, 16, 119},
			{71, 16, 120},
			{73, 16, 120},
			{74, 16, 121},
			{76, 17, 122},
			{78, 17, 123},
			{79, 18, 123},
			{81, 18, 124},
			{82, 19, 124},
			{84, 19, 125},
			{86, 20, 125},
			{87, 21, 126},
			{89, 21, 126},
			{90, 22, 126},
			{92, 22, 127},
			{93, 23, 127},
			{95, 24, 127},
			{96, 24, 128},
			{98, 25, 128},
			{100, 26, 128},
			{101, 26, 128},
			{103, 27, 128},
			{104, 28, 129},
			{106, 28, 129},
			{107, 29, 129},
			{109, 29, 129},
			{110, 30, 129},
			{112, 31, 129},
			{114, 31, 129},
			{115, 32, 129},
			{117, 33, 129},
			{118, 33, 129},
			{120, 34, 129},
			{121, 34, 130},
			{123, 35, 130},
			{124, 35, 130},
			{126, 36, 130},
			{128, 37, 130},
			{129, 37, 129},
			{131, 38, 129},
			{132, 38, 129},
			{134, 39, 129},
			{136, 39, 129},
			{137, 40, 129},
			{139, 41, 129},
			{140, 41, 129},
			{142, 42, 129},
			{144, 42, 129},
			{145, 43, 129},
			{147, 43, 128},
			{148, 44, 128},
			{150, 44, 128},
			{152, 45, 128},
			{153, 45, 128},
			{155, 46, 127},
			{156, 46, 127},
			{158, 47, 127},
			{160, 47, 127},
			{161, 48, 126},
			{163, 48, 126},
			{165, 49, 126},
			{166, 49, 125},
			{168, 50, 125},
			{170, 51, 125},
			{171, 51, 124},
			{173, 52, 124},
			{174, 52, 123},
			{176, 53, 123},
			{178, 53, 123},
			{179, 54, 122},
			{181, 54, 122},
			{183, 55, 121},
			{184, 55, 121},
			{186, 56, 120},
			{188, 57, 120},
			{189, 57, 119},
			{191, 58, 119},
			{192, 58, 118},
			{194, 59, 117},
			{196, 60, 117},
			{197, 60, 116},
			{199, 61, 115},
			{200, 62, 115},
			{202, 62, 114},
			{204, 63, 113},
			{205, 64, 113},
			{207, 64, 112},
			{208, 65, 111},
			{210, 66, 111},
			{211, 67, 110},
			{213, 68, 109},
			{214, 69, 108},
			{216, 69, 108},
			{217, 70, 107},
			{219, 71, 106},
			{220, 72, 105},
			{222, 73, 104},
			{223, 74, 104},
			{224, 76, 103},
			{226, 77, 102},
			{227, 78, 101},
			{228, 79, 100},
			{229, 80, 100},
			{231, 82, 99},
			{232, 83, 98},
			{233, 84, 98},
			{234, 86, 97},
			{235, 87, 96},
			{236, 88, 96},
			{237, 90, 95},
			{238, 91, 94},
			{239, 93, 94},
			{240, 95, 94},
			{241, 96, 93},
			{242, 98, 93},
			{242, 100, 92},
			{243, 101, 92},
			{244, 103, 92},
			{244, 105, 92},
			{245, 107, 92},
			{246, 108, 92},
			{246, 110, 92},
			{247, 112, 92},
			{247, 114, 92},
			{248, 116, 92},
			{248, 118, 92},
			{249, 120, 93},
			{249, 121, 93},
			{249, 123, 93},
			{250, 125, 94},
			{250, 127, 94},
			{250, 129, 95},
			{251, 131, 95},
			{251, 133, 96},
			{251, 135, 97},
			{252, 137, 97},
			{252, 138, 98},
			{252, 140, 99},
			{252, 142, 100},
			{252, 144, 101},
			{253, 146, 102},
			{253, 148, 103},
			{253, 150, 104},
			{253, 152, 105},
			{253, 154, 106},
			{253, 155, 107},
			{254, 157, 108},
			{254, 159, 109},
			{254, 161, 110},
			{254, 163, 111},
			{254, 165, 113},
			{254, 167, 114},
			{254, 169, 115},
			{254, 170, 116},
			{254, 172, 118},
			{254, 174, 119},
			{254, 176, 120},
			{254, 178, 122},
			{254, 180, 123},
			{254, 182, 124},
			{254, 183, 126},
			{254, 185, 127},
			{254, 187, 129},
			{254, 189, 130},
			{254, 191, 132},
			{254, 193, 133},
			{254, 194, 135},
			{254, 196, 136},
			{254, 198, 138},
			{254, 200, 140},
			{254, 202, 141},
			{254, 204, 143},
			{254, 205, 144},
			{254, 207, 146},
			{254, 209, 148},
			{254, 211, 149},
			{254, 213, 151},
			{254, 215, 153},
			{254, 216, 154},
			{253, 218, 156},
			{253, 220, 158},
			{253, 222, 160},
			{253, 224, 161},
			{253, 226, 163},
			{253, 227, 165},
			{253, 229, 167},
			{253, 231, 169},
			{253, 233, 170},
			{253, 235, 172},
			{252, 236, 174},
			{252, 238, 176},
			{252, 240, 178},
			{252, 242, 180},
			{252, 244, 182},
			{252, 246, 184},
			{252, 247, 185},
			{252, 249, 187},
			{252, 251, 189},
			{252, 253, 191},
		}};
};
