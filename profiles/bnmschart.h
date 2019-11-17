#ifndef BNMSCHART_H
#define BNMSCHART_H

#include "profilechart.h"
#include "bnmsmodel.h"

class BnmsChart : public ProfileChart
{
	Q_OBJECT

public:
	BnmsChart(std::shared_ptr<Dataset const> data, const std::vector<Components> &comps);

	void clear() override;

public slots:
	void setReference(ProteinId ref);
	void setBorder(Qt::Edge border, double value);
	void setSelectedComponents(const std::vector<size_t> &selection);
	void toggleComponentMode(bool on);
	void toggleZoom(bool toRange, bool force = false);
	void repopulate();

protected:
	QString titleOf(unsigned index, const QString &name, bool isMarker) const override;
	QColor colorOf(unsigned index, const QColor &color, bool isMarker) const override;
	qreal alphaOf(unsigned index) const;
	void animHighlight(int index, qreal step) override;

	bool componentMode = false;
	std::pair<double, double> range = {0., 0.};
	bool zoomToRange = false;

	// score/dist of all proteins on display
	std::unordered_map<size_t, double> scores;
	double meanScore = 1.;

	// reference in data features index
	unsigned reference = 1; // most probably not protein if id 0 (very first start)
	// gaussian components
	std::vector<size_t> compSelection; // selected components of reference
	const std::vector<Components> &allComponents;
};

#endif
