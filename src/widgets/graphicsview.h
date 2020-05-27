#ifndef DISTMATVIEW_H
#define DISTMATVIEW_H

#include <QGraphicsView>
#include <memory>

class GraphicsScene;

class GraphicsView : public QGraphicsView
{
	Q_OBJECT

	Q_PROPERTY(bool scrollingEnabled MEMBER scrollingEnabled)

public:
	using QGraphicsView::QGraphicsView;
	void switchScene(GraphicsScene *scene);

protected:
	// override for internal use (does not work through pointer! scene() is non-virtual)
	GraphicsScene *scene() const;

	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

	bool scrollingEnabled = true;

	// per-scene state
	std::map<GraphicsScene*, std::pair<QTransform, QSize>> lastViewport;
};

#endif // DISTMATVIEW_H
