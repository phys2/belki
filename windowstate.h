#ifndef WINDOWSTATE_H
#define WINDOWSTATE_H

#include "model.h"
#include "compute/colors.h"

#include <QObject>
#include <QColor>
#include <QVector>
#include <QStandardItemModel>

class GuiState;
class ProteinDB;

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
	WindowState(GuiState &global);

	ProteinDB& proteins();

	void setOrder(Order::Type type);

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

	GuiState &global;

	Q_OBJECT

signals:
	void colorsetUpdated();
	void annotationsToggled();
	void annotationsChanged();
	void hierarchyChanged();
	void orderChanged();
	void orderSynchronizingToggled();
	void openGlToggled();
};

#endif
