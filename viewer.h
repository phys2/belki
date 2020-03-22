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
		if (windowState) {
			windowState->disconnect(this);
			windowState->proteins().disconnect(this);
		}
		windowState = s;
	}

public slots:
	virtual void selectDataset(unsigned id)=0;
	virtual void deselectDataset()=0;
	virtual void addDataset(Dataset::Ptr data)=0;
	virtual void removeDataset(unsigned id)=0;

signals:
	void markerToggled(ProteinId id, bool present);
	void proteinsHighlighted(std::vector<ProteinId> proteins, const QString &title = {});
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

	std::shared_ptr<WindowState> windowState;
};

#endif // VIEWER_H
