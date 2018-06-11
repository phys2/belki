#ifndef DIMRED_H
#define DIMRED_H

#include <QVector>
#include <QPointF>

namespace dimred {
    struct Method {
		QString id;
		QString description;
	};

    QVector<QPointF> compute(QString method, QVector<QVector<double>> &features);

	std::vector<Method> availableMethods();
}

#endif // DIMRED_H
