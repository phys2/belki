#ifndef DIMRED_H
#define DIMRED_H

#include <QVector>
#include <QPointF>

namespace dimred {

    QVector<QPointF> compute(QString method, QVector<QVector<double>> &features);

}

#endif // DIMRED_H
