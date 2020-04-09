#ifndef DIMRED_H
#define DIMRED_H

#include <QVector>
#include <QMap>
#include <QPointF>
#include <vector>

namespace dimred {
    struct Method {
		QString name;
		QString id; // first id of resulting method(s)
		QString description;
	};

	QMap<QString, QVector<QPointF>>
	compute(QString method, const std::vector<std::vector<double> > &features);

	const std::vector<Method> &availableMethods();
}

#endif // DIMRED_H
