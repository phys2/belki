#ifndef REFERENCECHART_H
#define REFERENCECHART_H

#include "profilechart.h"
#include "bnmsmodel.h"

namespace QtCharts {
class QAreaSeries;
}

class ReferenceChart : public ProfileChart
{
	Q_OBJECT

public:
	ReferenceChart(std::shared_ptr<Dataset const> data, const std::vector<Components> &comps);

	void clear() override;
	void finalize() override;

public slots:
	void setReference(ProteinId ref);
	void toggleComponent(unsigned index);
	void repopulate();

protected:
	QString titleOf(unsigned int index, const QString &name, bool isMarker) const override;
	QColor colorOf(unsigned int index, const QColor &color, bool isMarker) const override;

	struct Component {
		::Component parameters;
		bool active = true;
		QtCharts::QAreaSeries *series = {};
	};

	// reference in data features index
	unsigned reference = 1; // most probably not protein if id 0 (very first start)
	// gaussian components
	std::vector<Component> components;
	const std::vector<Components> &allComponents;
};

#endif
