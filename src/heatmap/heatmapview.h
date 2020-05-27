#ifndef HEATMAPVIEW_H
#define HEATMAPVIEW_H

#include <QGraphicsView>
#include <memory>

class HeatmapScene;

class HeatmapView : public QGraphicsView
{
public:
	using QGraphicsView::QGraphicsView;
	void switchScene(HeatmapScene *scene);

public slots:
	void setColumnMode(bool single);

protected:
	struct State {
		bool singleColumn = false;
		qreal currentScale = 0.; // current scale factor (pixel size in the scene)
		qreal outerScale = 0.; // scale factor where scene is fully fitted
	};

	// override for internal use (does not work through pointer! scene() is non-virtual)
	HeatmapScene *scene() const;

	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void enterEvent(QEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

	State& currentState();
	void arrangeScene();

	std::map<HeatmapScene*, State> state;
};

#endif // HEATMAPVIEW_H
