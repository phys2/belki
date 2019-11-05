#ifndef BNMSCHART_H
#define BNMSCHART_H

#include "profilechart.h"

class RangeSelectItem;

class BnmsChart : public ProfileChart
{
	Q_OBJECT

public:
	BnmsChart(std::shared_ptr<Dataset const> data);
	~BnmsChart() override;

	void clear() override;

public slots:
	void setReference(ProteinId ref);

protected:
	void repopulate();
	QString titleOf(unsigned int index, const QString &name, bool isMarker) const override;
	void animHighlight(int index, qreal step) override;

	std::unique_ptr<RangeSelectItem> rangeItem;
	std::pair<qreal, qreal> range;

	// score/dist of all proteins on display
	std::unordered_map<unsigned, qreal> scores;
	qreal meanScore = 1.;

	// reference in data features index
	unsigned reference = 1; // most probably not protein if id 0 (very first start)
};

#endif
