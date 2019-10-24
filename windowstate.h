#ifndef WINDOWSTATE_H
#define WINDOWSTATE_H

#include "model.h"
#include "compute/colors.h"

#include <QObject>
#include <QColor>
#include <QVector>

/* a "dumb" QObject – whoever manipulates also emits the signals */
struct WindowState : QObject
{
	bool showAnnotations = true;
	bool useOpenGl = false;
	// used for feature weights
	QVector<QColor> standardColors = Palette::tableau20;

	Q_OBJECT

signals:
	void colorsetUpdated();
	void annotationsToggled();
	void openGlToggled();
};

#endif
