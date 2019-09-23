#ifndef VIEWER_H
#define VIEWER_H

#include "proteindb.h"
#include "dataset.h"

#include <QMainWindow>

class QGraphicsScene;
class QGraphicsView;

class Viewer : public QMainWindow
{
	Q_OBJECT

public:
	using QMainWindow::QMainWindow;
	virtual ~Viewer() {}

public slots:
	virtual void selectDataset(unsigned id)=0;
	virtual void addDataset(Dataset::Ptr data)=0;

signals:
	// signals from outside that we might react to
	void inAddProtein(ProteinId id, const Protein &protein);
	void inUpdateColorset(QVector<QColor> colors);
	void inTogglePartitions(bool show);
	void inToggleMarkers(const std::vector<ProteinId> ids, bool present);
	void inToggleOpenGL(bool enabled);

	// signals emitted by us
	void markerToggled(ProteinId id, bool present);
	void cursorChanged(QVector<unsigned> samples, const QString &title = {});
	void orderRequested(Dataset::OrderBy order, bool synchronize);
	void exportRequested(QGraphicsView *source, QString description);
	void exportRequested(QGraphicsScene *source, QString description);

	// gui synchronization between views
	// TODO: read from dataset directly on update()
	void changeOrder(Dataset::OrderBy order, bool synchronize);

protected:
	struct DataState {
		Dataset::Ptr data;
	};

	template<typename State>
	using ContentMap = std::map<unsigned, State>;
	template<typename State>
	struct Current {
		operator bool() const { return id; }
		State& operator()() const { return *p; }
		unsigned id = 0;
		State *p = nullptr;
	};
};

#endif // VIEWER_H
