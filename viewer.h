#ifndef VIEWER_H
#define VIEWER_H

#include "dataset.h"

#include <QMainWindow>

class QGraphicsScene;
class QGraphicsView;

class Viewer : public QMainWindow
{
	Q_OBJECT

public:
	using QMainWindow::QMainWindow;

	virtual void init(Dataset *data)=0;

signals:
	// inbound signals (that are wired to internal components)
	void inUpdateColorset(QVector<QColor> colors);
	void inReset(bool haveData);
	void inRepartition(bool withOrder);
	void inReorder();
	void inToggleMarker(unsigned sampleIndex, bool present);
	void inTogglePartitions(bool show);

	// signals emitted by us
	void markerToggled(unsigned sampleIndex, bool present);
	void cursorChanged(QVector<unsigned> samples, QString title = {});
	void orderRequested(Dataset::OrderBy order, bool synchronize);
	void exportRequested(QGraphicsView *source, QString description);
	void exportRequested(QGraphicsScene *source, QString description);

	// gui synchronization between views
	void changeOrder(Dataset::OrderBy order, bool synchronize);
};

#endif // VIEWER_H
