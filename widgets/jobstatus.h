#ifndef JOBSTATUS_H
#define JOBSTATUS_H

#include <QWidget>
#include <memory>
#include <map>

class QSvgRenderer;
class QTimer;

class JobWidget : public QWidget
{
	Q_OBJECT
public:
	JobWidget(unsigned jobId, QSvgRenderer *renderer, QWidget *parent=nullptr);

protected:
	void resizeEvent(QResizeEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

	QSvgRenderer *renderer;
	unsigned jobId;
};

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

	const int fps = 25;

	QSvgRenderer *renderer;
	QTimer *animator;

	std::map<unsigned, JobWidget*> jobs;
};

#endif
