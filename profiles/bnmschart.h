#ifndef BNMSCHART_H
#define BNMSCHART_H

#include "profilechart.h"

class BnmsChart : public ProfileChart
{
	Q_OBJECT

public:
	BnmsChart(std::shared_ptr<Dataset const> data);

	void clear() override;

public slots:
	void setReference(ProteinId ref);
	void setBorder(Qt::Edge border, double value);
	void toggleZoom(bool toRange, bool force = false);
	void repopulate();

protected:
	QString titleOf(unsigned index, const QString &name, bool isMarker) const override;
	QColor colorOf(unsigned index, const QColor &color, bool isMarker) const override;
	qreal alphaOf(unsigned index) const;
	void animHighlight(int index, qreal step) override;

	std::pair<double, double> range = {0., 0.};
	bool zoomToRange = false;

	// score/dist of all proteins on display
	std::unordered_map<unsigned, double> scores;
	double meanScore = 1.;

	// reference in data features index
	unsigned reference = 1; // most probably not protein if id 0 (very first start)
};

#endif
