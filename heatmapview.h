#ifndef HEATMAPVIEW_H
#define HEATMAPVIEW_H

#include <QGraphicsView>

class HeatmapScene;

class HeatmapView : public QGraphicsView
{
public:
	using QGraphicsView::QGraphicsView;

protected:
	// override for internal use (does not work through pointer! scene() is non-virtual)
	HeatmapScene *scene() const;

	void enterEvent(QEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

	void arrangeScene();

	bool singleColumn = false;
	qreal currentScale = 1.; // current scale factor (pixel size in the scene)
	qreal outerScale = 1.; // scale factor where scene is fully fitted
};

#endif // HEATMAPVIEW_H
