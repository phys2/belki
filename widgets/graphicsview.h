#ifndef DISTMATVIEW_H
#define DISTMATVIEW_H

#include <QGraphicsView>

class GraphicsScene;

class GraphicsView : public QGraphicsView
{
	Q_OBJECT

public:
	using QGraphicsView::QGraphicsView;

protected:
	// override for internal use (does not work through pointer! scene() is non-virtual)
	GraphicsScene *scene() const;

	void wheelEvent(QWheelEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

	std::pair<QTransform, QSize> lastViewport;
};

#endif // DISTMATVIEW_H