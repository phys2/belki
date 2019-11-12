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
	void setBorder(Qt::Edge border, qreal value);
	void repopulate();

protected:
	QString titleOf(unsigned int index, const QString &name, bool isMarker) const override;
	QColor colorOf(unsigned int index, const QColor &color, bool isMarker) const override;
	void animHighlight(int index, qreal step) override;

	std::pair<qreal, qreal> range = {0., 0.};

	// score/dist of all proteins on display
	std::unordered_map<unsigned, qreal> scores;
	qreal meanScore = 1.;

	// reference in data features index
	unsigned reference = 1; // most probably not protein if id 0 (very first start)
};

#endif
