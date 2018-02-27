#include "fileio.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QFileDialog>
#include <QtCore/QMap>
#include <QtSvg/QSvgGenerator>
//#include <QtPrintSupport/QPrinter> // for PDF support
#include <QtGui/QPainter>

#include <QtDebug>

FileIO::FileIO(QMainWindow *parent) :
    QObject(parent), parent(parent)
{}

QString FileIO::chooseFile(FileIO::Role purpose, QWidget *p)
{
	const QMap<Role, RoleDef> map = {
	    {OpenDataset, {"Open Dataset", "Peak Volumnes Table (*.tsv)", false, {}}},
	    {OpenClustering, {"Open Annotations or Clustering", "Annotation Table (*.tsv *.txt);; Hierarchical Clustering (*.json)", false, {}}},
	    {OpenMarkers, {"Open Markers List", "List of markers (*.txt);; All Files (*)", false, {}}},
	    {SaveMarkers, {"Save Markers to File", "List of markers (*.txt)", true, ".txt"}},
	    //with pdf//{SavePlot, {"Save Plot to File", "Scalable Vector Graphics (*.svg);; Portable Document Format (*.pdf);; Portable Network Graphics (*.png)", true, {}}},
	    {SavePlot, {"Save Plot to File", "Scalable Vector Graphics (*.svg);; Portable Network Graphics (*.png)", true, {}}},
	};

	if (!p)
		p = parent;

	auto params = map[purpose];
	if (params.isWrite) {
		auto filename = QFileDialog::getSaveFileName(p, params.title, {}, params.filter);
		if (!params.writeSuffix.isEmpty() && QFileInfo(filename).suffix().isEmpty())
			filename.append(params.writeSuffix);
		return filename;
	}

	return QFileDialog::getOpenFileName(p, params.title, {}, params.filter);
}

void FileIO::renderToFile(QWidget *source, const QString &title, const QString &description)
{
	auto filename = chooseFile(SavePlot, source->window());
	auto filetype = QFileInfo(filename).suffix();
	if (filetype.isEmpty()) {
		emit ioError("Please select a filename with suffix (e.g. .svg)!");
	}

	auto renderer = [source] (QPaintDevice *target) {
		QPainter p;
		p.begin(target);
		source->render(&p);
		p.end();
	};

	if (filetype == "svg") {
		QSvgGenerator svg;
		svg.setFileName(filename);
		svg.setSize(source->size());
		svg.setViewBox(source->rect());
		svg.setTitle(title);
		svg.setDescription(description);
		renderer(&svg);
	}
	/*if (filetype == "pdf") { // TODO: this produces only a bitmap, so we disabled it for now
		// maybe use QPicture trick. also need to adapt page size
		QPrinter pdf;
		pdf.setOutputFormat(QPrinter::PdfFormat);
		pdf.setOutputFileName(filename);
		renderer(&pdf);
	}*/
	if (filetype == "png") {
		const qreal scale = 2.; // render in higher resolution
		QPixmap pixmap(source->size()*scale);
		pixmap.setDevicePixelRatio(scale);
		renderer(&pixmap);
		pixmap.save(filename);
	}
}
