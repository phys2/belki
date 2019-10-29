#ifndef WINDOWSTATE_H
#define WINDOWSTATE_H

#include "model.h"
#include "compute/colors.h"

#include <QObject>
#include <QColor>
#include <QVector>
#include <QStandardItemModel>

/* A "dumb" QObject – whoever manipulates also emits the signals
 * Alternatively, we could work with Qt properties (and get/set/notify)
 */
struct WindowState : QObject
{
	WindowState();

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
