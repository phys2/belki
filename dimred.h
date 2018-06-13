#ifndef DIMRED_H
#define DIMRED_H

#include <QtCore/QVector>
#include <QtCore/QMap>
#include <QtCore/QPointF>

namespace dimred {
    struct Method {
		QString name;
		QString id; // first id of resulting method(s)
		QString description;
	};

	QMap<QString, QVector<QPointF>> compute(QString method, QVector<QVector<double>> &features);

	std::vector<Method> availableMethods();
}

#endif // DIMRED_H
