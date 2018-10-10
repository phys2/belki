#ifndef HEATMAPVIEW_H
#define HEATMAPVIEW_H

#include <QtWidgets/QGraphicsView>

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

	void arrangeScene(QSize geometry = {});

	bool singleColumn = false;
};

#endif // HEATMAPVIEW_H
