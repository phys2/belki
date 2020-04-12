#ifndef VIEWER_H
#define VIEWER_H

#include "proteindb.h"
#include "dataset.h"
#include "windowstate.h"

#include <QObject>
#include <memory>

class QWidget;
class QGraphicsScene;
class QGraphicsView;
class QAbstractItemModel;

class Viewer : public QObject
{
	Q_OBJECT

public:
	/* Note: if no explicit parent is specified, the widget is used as parent;
	 * so the lifetime of the widget dictates the lifetime of the viewer */
	Viewer(QWidget *widget = nullptr, QObject *parent = nullptr);
	virtual ~Viewer() {}

	// may be overloaded to return descendant type
	virtual QWidget *getWidget() { return widget; }

	virtual void setProteinModel(QAbstractItemModel*) {}
	virtual void setWindowState(std::shared_ptr<WindowState> s);

public slots:
	virtual void selectDataset(unsigned id)=0;
	virtual void deselectDataset();
	virtual void addDataset(Dataset::Ptr data)=0;
	virtual void removeDataset(unsigned id);

signals:
	void proteinsHighlighted(std::vector<ProteinId> proteins, const QString &title = {});
	void exportRequested(QGraphicsView *source, QString description);
	void exportRequested(QGraphicsScene *source, QString description);

protected:
	struct DataState {
		using Ptr = std::unique_ptr<DataState>;

		DataState(Dataset::Ptr data) : data(data) {}
		virtual ~DataState() = default;
		Dataset::Ptr data;
	};
	using ContentMap = std::map<unsigned, DataState::Ptr>;

	virtual bool updateIsEnabled();
	bool haveData(); // have a selected dataset → current datastate
	bool selectData(unsigned id); // select dataset/state
	template<typename State>
	State& selectedAs() { return dynamic_cast<State&>(*(dataIt->second)); }
	template<typename State>
	State& addData(Dataset::Ptr data) {
		auto it = addData(data->id(), std::make_unique<State>(data));
		return dynamic_cast<State&>(*it->second);
	}
	ContentMap::iterator addData(unsigned id, DataState::Ptr elem);

	std::shared_ptr<WindowState> windowState;
	QWidget *widget;

private:
	ContentMap dataStates;
	ContentMap::iterator dataIt = dataStates.end(); // Note: always update when altering map
};

#endif // VIEWER_H
