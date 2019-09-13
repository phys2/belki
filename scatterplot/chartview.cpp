#include "chartview.h"
#include "chart.h"

ChartView::ChartView(QWidget *parent)
    : QtCharts::QChartView(parent)
{
	setRubberBand(QtCharts::QChartView::RectangleRubberBand); // TODO: issue #5
}

void ChartView::switchChart(Chart *chart)
{
	chart->setConfig(&config);
	setChart(chart);
}

void ChartView::releaseChart()
{
	setChart(new QtCharts::QChart()); // release ownership
}

Chart *ChartView::chart()
{
	return qobject_cast<Chart*>(QChartView::chart());
}

void ChartView::toggleSingleMode()
{
	config.proteinStyle.singleMode = !config.proteinStyle.singleMode;
	emit chart()->proteinStyleUpdated();
}

void ChartView::scaleProteins(qreal factor)
{
	config.proteinStyle.size *= factor;
	emit chart()->proteinStyleUpdated();
}

void ChartView::switchProteinBorders()
{
	const QVector<Qt::PenStyle> rot{
		Qt::PenStyle::SolidLine, Qt::PenStyle::DotLine, Qt::PenStyle::NoPen};
	config.proteinStyle.border = rot[(rot.indexOf(config.proteinStyle.border) + 1) % rot.size()];
	emit chart()->proteinStyleUpdated();
}

void ChartView::adjustProteinAlpha(qreal adjustment)
{
	if (config.proteinStyle.singleMode)
		return; // avoid hidden changes
	auto &a = config.proteinStyle.alpha.reg;
	a = std::min(1., std::max(0., a + adjustment));
	emit chart()->proteinStyleUpdated();
}

void ChartView::scaleCursor(qreal factor)
{
	config.cursorRadius *= factor;
	chart()->refreshCursor();
}

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
	if (!rubberState) {
		chart()->moveCursor(event->pos());
	}

	QChartView::mouseMoveEvent(event);
}


void ChartView::mousePressEvent(QMouseEvent *event)
{
	QChartView::mousePressEvent(event);
	if (event->isAccepted()) { // sadly always…
		rubberState = true;
		rubberPerformed = false;
		/* terrible hack to find out if the plot changed between click press and
		   release, which means, probably the rubber was active, and we should
		   ignore the next release event */
		auto conn = std::make_shared<QMetaObject::Connection>();
		*conn = connect(chart(), &Chart::areaChanged, [this, conn] {
			rubberPerformed = true;
			disconnect(*conn);
		});
	}
}

void ChartView::mouseReleaseEvent(QMouseEvent *event)
{
	QChartView::mouseReleaseEvent(event);
	if (event->isAccepted()) { // sadly always…
		rubberState = false;
	}

	if (rubberPerformed)
		return;

	if (event->button() == Qt::LeftButton) {
		chart()->toggleCursorLock();
	}
}

void ChartView::enterEvent(QEvent *)
{
	// steal focus for the interactive cursor with keyboard events
	setFocus(Qt::MouseFocusReason);
}

void ChartView::leaveEvent(QEvent *)
{
	chart()->moveCursor();
}

void ChartView::keyReleaseEvent(QKeyEvent *event)
{
	QChartView::keyReleaseEvent(event);
	if (event->isAccepted())
		return;

	if (event->key() == Qt::Key_Space)
		chart()->toggleCursorLock();

	if (event->key() == Qt::Key_Z)
		chart()->undoZoom(event->modifiers() & Qt::ShiftModifier);

	if (event->key() == Qt::Key_S)
		toggleSingleMode();

	auto adjustAlpha = [this] (bool decr) {
		adjustProteinAlpha(decr ? -.05 : .05);
	};
	auto adjustScale = [this] (bool decr) {
		scaleProteins(decr ? 0.8 : 1.25);
	};
	auto adjustCursor = [this] (bool decr) {
		scaleCursor(decr ? 0.8 : 1.25);
	};

	if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Minus) {
		if (event->modifiers() & Qt::AltModifier)
			adjustAlpha(event->key() == Qt::Key_Minus);
		else if (event->modifiers() & Qt::ControlModifier)
			adjustScale(event->key() == Qt::Key_Minus);
		else
			adjustCursor(event->key() == Qt::Key_Minus);
	}

	if (event->key() == Qt::Key_B)
		switchProteinBorders();
}

void ChartView::wheelEvent(QWheelEvent *event)
{
	QChartView::wheelEvent(event);
	if (event->isAccepted())
		return;

	auto factor = [&] (qreal strength) { return 1. + 0.001*strength*event->delta(); };

	if (event->modifiers() & Qt::ControlModifier)
		scaleCursor(factor(2.));
	else
		chart()->zoomAt(mapToScene(event->pos()), factor(1.));
}
