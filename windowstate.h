#ifndef WINDOWSTATE_H
#define WINDOWSTATE_H

#include "model.h"
#include "compute/colors.h"
#include "jobregistry.h"

#include <QObject>
#include <QColor>
#include <QVector>
#include <QStandardItemModel>
#include <memory>

class GuiState;
class ProteinDB;
class DataHub;
class FileIO;
class QMenu;

/* A "dumb" QObject – whoever manipulates also emits the signals
 * Alternatively, we could work with Qt properties (and get/set/notify)
 *
 * TODO: WindowState could become more clever and also hold the Dataset::Ptr
 * that is shared within a window. The select…() methods in MainWindow would
 * become setters here and we would trigger data computation.
 * We would then also not signal new/select dataset signals anymore, but merely
 * a datasetChanged() signal would suffice.
 */
struct WindowState : QObject
{
	Q_OBJECT
public:
	using Ptr = std::shared_ptr<WindowState>;

	WindowState(GuiState &global);

	ProteinDB& proteins();
	DataHub& hub();
	FileIO& io();
	std::unique_ptr<QMenu> proteinMenu(ProteinId id);

	void setOrder(Order::Type type); // emits

	bool showAnnotations = true;
	bool useOpenGl = false;
	// used for feature weights
	QVector<QColor> standardColors = Palette::tableau20;

	Annotations::Meta annotations;
	HrClustering::Meta hierarchy;
	Order order = {Order::NAME};
	Order::Type preferredOrder = Order::NAME;
	bool orderSynchronizing = true; // order follows annot./hier. selection

	QStandardItemModel orderModel;
	std::vector<QPointer<QObject>> jobListeners;

signals:
	void colorsetUpdated();
	void annotationsToggled();
	void annotationsChanged();
	void hierarchyChanged();
	void orderChanged();
	void orderSynchronizingToggled();
	void openGlToggled();

protected:
	GuiState &global;
};

#endif
