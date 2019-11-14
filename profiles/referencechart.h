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
	void applyBorder(Qt::Edge border, double value);
	void toggleComponent(unsigned index);
	void repopulate();

protected:
	struct Component {
		::Component parameters;
		bool active = true;
		QtCharts::QAreaSeries *series = {};
	};

	QString titleOf(unsigned int index, const QString &name, bool isMarker) const override;
	QColor colorOf(unsigned int index, const QColor &color, bool isMarker) const override;
	void toggleComponent(Component &c);

	// range, only kept to adequately react to border changes
	std::pair<double, double> range = {0., 0.};

	// reference in data features index
	unsigned reference = 1; // most probably not protein if id 0 (very first start)
	// gaussian components
	std::vector<Component> components;
	const std::vector<Components> &allComponents;
};

#endif
