#ifndef BNMSCHART_H
#define BNMSCHART_H

#include "profilechart.h"

class BnmsChart : public ProfileChart
{
	Q_OBJECT

public:
	BnmsChart(std::shared_ptr<Dataset const> data);

public slots:
	void setReference(ProteinId ref);

protected:
	void animHighlight(int index, qreal step) override;

	// reference in data features index
	unsigned reference = 1; // most probably not protein if id 0 (very first start)
};

#endif
