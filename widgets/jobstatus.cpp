#include "jobstatus.h"
#include "jobregistry.h"

#include <QPainter>
#include <QPaintEvent>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolTip>
#include <QHBoxLayout>
#include <QStyle>
#include <QStyleOptionButton>

#include <cmath>

JobStatus::JobStatus(QWidget *parent)
    : QWidget(parent)
{
	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// provide shared renderer and animation timer for all children
	renderer = new QSvgRenderer(QString{":/spinner.svg"}, this);
	renderer->setFramesPerSecond(fps);
	// extra timer as QSvgRenderer::repaintNeeded() signal seems not to respect FPS
	animator = new QTimer(this);
	animator->setInterval(1000 / fps);
	animator->setSingleShot(false);
	connect(animator, &QTimer::timeout, [this] { update(); });
}

void JobStatus::addJob(unsigned id)
{
	auto widget = new JobWidget(id, renderer);
	layout()->addWidget(widget); // sets parent
	jobs.try_emplace(id, widget);
	updateJobs();
}

void JobStatus::updateJob(unsigned id)
{
	try {
		jobs.at(id)->updateJob();
	}  catch (std::out_of_range&) {} // TODO complain, state error on caller side
}

void JobStatus::removeJob(unsigned id)
{
	try {
		delete jobs.at(id);
	}  catch (std::out_of_range&) {} // TODO complain, state error on caller side
	jobs.erase(id);
	updateJobs();
}

void JobStatus::updateJobs()
{
	if (jobs.empty())
		animator->stop();
	else
		animator->start();
}

JobWidget::JobWidget(unsigned jobId, QSvgRenderer *renderer, QWidget *parent)
    : QWidget(parent), renderer(renderer), job(JobRegistry::get()->job(jobId))
{
	// allow expansion in y-direction, and honor the width we set on resizes
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	setMouseTracking(true);
}

void JobWidget::updateJob()
{
	job = JobRegistry::get()->job(job.id);
	update();
}

void JobWidget::resizeEvent(QResizeEvent *event)
{
	auto newHeight = event->size().height();
	// ensure square size
	if (newHeight != event->oldSize().height())
		setMinimumWidth(newHeight);
}

void JobWidget::mouseMoveEvent(QMouseEvent *event)
{
	QToolTip::showText(event->globalPos(), job.name, this);
}

void JobWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	/* draw a tool-button style panel so we look like a button; includes hover effects */
	QStyleOptionButton opt;
	opt.initFrom(this);
	opt.rect = rect();
	style()->drawPrimitive(QStyle::PE_PanelButtonTool, &opt, &painter, this);

	/* draw progress arc */
	if (job.progress && !job.isCancelled) {
		/* factor ranges from 0.5 to 2.0 */
		float factor = 0.5;
		if (job.progress > 1)
			factor = std::min(2 - 0.75f * std::log10(100 - job.progress), 2.f);
		auto rect = QRectF(contentsRect()).adjusted(6.5, 6.5, -6.5, -6.5);
		painter.setRenderHint(QPainter::RenderHint::Antialiasing, true);
		painter.translate(0, contentsRect().height());
		painter.rotate(-90);
		painter.setPen(Qt::NoPen);
		QRadialGradient grad(rect.center(), rect.width()/2.);
		QColor gradBase(int(127 * factor), int(63 * factor), int(7 * factor));
		grad.setColorAt(0, Qt::transparent);
		gradBase.setAlphaF(0.125f * factor);
		grad.setColorAt(0.5, gradBase);
		gradBase.setAlphaF(0.25f * factor);
		grad.setColorAt(0.75, gradBase);
		gradBase.setAlphaF(0.5f * factor);
		grad.setColorAt(1, gradBase);
		painter.setBrush(grad);
		painter.drawPie(rect, 0, 16 * 360);
		painter.setPen({QColor{255, 127, 14}, 2.});
		painter.drawArc(rect, 0, -16 * int(job.progress * 3.6f));
		return;
	}

	/* draw the spinner animation */
	if (job.isCancelled) {
		// mirror, so it runs CCW
		painter.translate(0, contentsRect().height());
		painter.scale(1, -1);
	}
	renderer->render(&painter, contentsRect().adjusted(4, 4, -4, -4));
}
