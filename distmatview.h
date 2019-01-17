#ifndef DISTMATVIEW_H
#define DISTMATVIEW_H

#include <QGraphicsView>

class DistmatScene;

class DistmatView : public QGraphicsView
{
public:
	using QGraphicsView::QGraphicsView;

protected:
	// override for internal use (does not work through pointer! scene() is non-virtual)
	DistmatScene *scene() const;

	void enterEvent(QEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
};

#endif // DISTMATVIEW_H
