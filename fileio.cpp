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
	    {OpenDataset, {"Open Dataset", "Peak Volumes Table or ZIP file (*.tsv *.zip)", false, {}}},
	    {OpenDescriptions, {"Open Descriptions", "Two-column table with descriptions (*.tsv)", false, {}}},
	    {OpenClustering, {"Open Annotations or Clustering",
	                      "All supported files (*.tsv *.txt *.json);; "
	                      "Annotation Table / Protein Lists (*.tsv *.txt);; Hierarchical Clustering (*.json)",
	                      false, {}}},
	    {OpenMarkers, {"Open Markers List", "List of markers (*.txt);; All Files (*)", false, {}}},
	    {SaveMarkers, {"Save Markers to File", "List of markers (*.txt)", true, ".txt"}},
	    {SaveAnnotations, {"Save Annotations to File", "Annotation table (*.tsv)", true, ".tsv"}},
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

void FileIO::renderToFile(QWidget *source, const RenderMeta &meta, QString filename)
{
	if (filename.isEmpty())
		filename = chooseFile(SavePlot, source->window());
	if (filename.isEmpty())
		return;

	auto filetype = QFileInfo(filename).suffix().toLower();
	if (filetype.isEmpty()) {
		emit ioError("Please select a filename with suffix (e.g. .svg)!");
		return;
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
		svg.setTitle(meta.title);
		svg.setDescription(meta.description);
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
