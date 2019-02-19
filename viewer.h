#ifndef VIEWER_H
#define VIEWER_H

#include <QMainWindow>

class Dataset;

class Viewer : public QMainWindow
{
	Q_OBJECT

public:
	using QMainWindow::QMainWindow;

	virtual void init(Dataset *data)=0;

signals:
	// inbound signals (that are wired to internal components)
	void inUpdateColorset(QVector<QColor> colors);
	void inReset(bool);
	void inRecolor();
	void inReorder();
	void inAddMarker(unsigned sampleIndex);
	void inRemoveMarker(unsigned sampleIndex);

	// signals emitted by us
	void cursorChanged(QVector<unsigned> samples, QString title = {});
};

#endif // VIEWER_H
