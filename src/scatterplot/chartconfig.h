#ifndef CHARTCONFIG_H
#define CHARTCONFIG_H

#include <QColor>

/*
 * GUI configuration of chart maintained by ChartView and read by Chart.
 */
struct ChartConfig {
	struct ProteinStyle {
		bool singleMode = false; // mode to highlight single clusters
		qreal size = 15.;
		struct {
			qreal reg = .65;
			qreal hi = .9;
			qreal lo = .1;
		} alpha;
		struct {
			QColor unlabeled = Qt::lightGray;
			QColor mixed = Qt::darkGray;
		} color;
		Qt::PenStyle border = Qt::PenStyle::DotLine;
	};

	ProteinStyle proteinStyle;
	qreal cursorRadius = 50; // size of cursor circle in coordinate space
};

#endif
