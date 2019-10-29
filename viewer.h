#ifndef VIEWER_H
#define VIEWER_H

#include "proteindb.h"
#include "dataset.h"
#include "windowstate.h"

#include <QMainWindow>
#include <memory>

class QGraphicsScene;
class QGraphicsView;
class QAbstractItemModel;

class Viewer : public QMainWindow
{
	Q_OBJECT

public:
	using QMainWindow::QMainWindow;
	virtual ~Viewer() {}

	virtual void setProteinModel(QAbstractItemModel*) {}
	virtual void setWindowState(std::shared_ptr<WindowState> s)
	{
		disconnect(windowState.get());
		windowState = s;
	}

public slots:
	virtual void selectDataset(unsigned id)=0;
	virtual void addDataset(Dataset::Ptr data)=0;

signals:
	// signals from outside that we might react to
	void inToggleMarkers(const ProteinVec &ids, bool present);

	// signals emitted by us
	void markerToggled(ProteinId id, bool present);
	void cursorChanged(QVector<unsigned> samples, const QString &title = {});
	void exportRequested(QGraphicsView *source, QString description);
	void exportRequested(QGraphicsScene *source, QString description);

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

	std::shared_ptr<WindowState> windowState = std::make_shared<WindowState>();
};

#endif // VIEWER_H
