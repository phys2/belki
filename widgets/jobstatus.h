#ifndef JOBSTATUS_H
#define JOBSTATUS_H

#include <QWidget>
#include <memory>
#include <vector>

class QSvgRenderer;
class QTimer;

class JobStatus : public QWidget
{
	Q_OBJECT
public:
	JobStatus(QWidget *parent=nullptr);

public slots:
	void addJob(unsigned id);
	void updateJob(unsigned id);
	void removeJob(unsigned id);

protected:
	void updateJobs();

	void mouseMoveEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

	QSvgRenderer *renderer;
	QTimer *animator;

	std::vector<unsigned> jobs;
};

#endif
