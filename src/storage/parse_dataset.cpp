#include "storage.h"
#include "proteindb.h"
#include "../compute/features.h"

#include <QTextStream>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

Features::Ptr Storage::readSource(QTextStream in, const QString &featureColName)
{
	// TODO: the featureColName argument is a hack. We probably want some "Config" struct instead
	bool normalize = featureColName.isEmpty() || featureColName == "Dist";

	auto header = in.readLine().split("\t");

	/* simple source files have first header field blank (first column is still proteins) */
	if (!header.empty() && header.first().isEmpty()) {
		in.seek(0);
		return readSimpleSource(in, normalize);
	}

	if (header.contains("") || header.removeDuplicates()) {
		emit message({"Could not parse file!", "Duplicate or empty columns in header!"});
		return {};
	}
	if (header.size() == 0 || header.first() != "Protein") {
		emit message({"Could not parse file!", "The first column must contain protein names."});
		return {};
	}
	int nameCol = header.indexOf("Pair");
	int featureCol = header.indexOf(featureColName.isEmpty() ? "Dist" : featureColName);
	int scoreCol = header.indexOf("Score");
	if (nameCol == -1 || featureCol == -1 || scoreCol == -1) {
		emit message({"Could not parse file!", "Not all necessary columns found."});
		return {};
	}

	/* read file into Features object */
	auto ret = std::make_unique<Features>();
	std::map<QString, unsigned> dimensions;
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		if (line.size() < header.size()) {
			emit message({"Could not parse complete file!",
			              QString{"Stopped at '%1', incomplete row!"}.arg(line[0])});
			break; // avoid message flood
		}

		/* setup metadata */
		auto protid = proteins.add(line[0]);

		/* determine protein index */
		size_t row; // the protein id we are altering
		auto index = ret->protIndex.find(protid);
		if (index == ret->protIndex.end()) {
			ret->protIds.push_back(protid);
			auto len = ret->protIds.size();
			ret->features.resize(len, std::vector<double>(dimensions.size()));
			ret->scores.resize(len, std::vector<double>(dimensions.size()));
			row = len - 1;
			ret->protIndex[protid] = row;
		} else {
			row = index->second;
		}

		/* determine dimension index */
		size_t col; // the dimension we are altering
		auto dIndex = dimensions.find(line[nameCol]);
		if (dIndex == dimensions.end()) {
			ret->dimensions.append(line[nameCol]);
			auto len = (size_t)ret->dimensions.size();
			for (auto &i : ret->features)
				i.resize(len);
			for (auto &i : ret->scores)
				i.resize(len);
			col = len - 1;
			dimensions[line[nameCol]] = col;
		} else {
			col = dIndex->second;
		}

		/* read coefficients */
		bool success = true;
		bool ok;
		double feat, score;
		feat = line[featureCol].toDouble(&ok);
		success = success && ok;
		score = line[scoreCol].toDouble(&ok);
		if (!success) {
			auto name = proteins.peek()->proteins[protid].name;
			QString err{"Stopped at protein '%1', malformed row!"};
			emit message({"Could not parse complete file!", err.arg(name)});
			break; // avoid message flood
		}

		/* fill-in features and scores */
		ret->features[row][col] = feat;
		ret->scores[row][col] = std::max(score, 0.); // TODO temporary clipping
	}

	if (ret->features.empty() || ret->dimensions.empty()) {
		emit message({"Could not read any valid data rows from file!"});
		return {};
	}

	finalizeRead(*ret, normalize);
	return ret;
}

Features::Ptr Storage::readSimpleSource(QTextStream &in, bool normalize)
{
	auto header = in.readLine().split("\t");
	header.pop_front(); // first column (also expected to be empty)
	// allow empty fields at the end caused by Excel export
	while (header.last().isEmpty())
		header.removeLast();

	// ensure header consistency
	if (header.empty() || header.contains("") || header.removeDuplicates()) {
		emit message({"Could not parse file!", "Duplicate or empty columns in header!"});
		return {};
	}

	/* read file into Features object */
	auto ret = std::make_unique<Features>();
	ret->dimensions = trimCrap(header);
	auto len = ret->dimensions.size();
	std::set<QString> seen; // names of read proteins
	while (!in.atEnd()) {
		auto line = in.readLine().split("\t");
		if (line.empty() || line[0].isEmpty())
			break; // early EOF

		if (line.size() < len + 1) {
			emit message({"Could not parse complete file!",
			              QString{"Stopped at '%1', incomplete row!"}.arg(line[0])});
			break; // avoid message flood
		}

		/* setup metadata */
		auto protid = proteins.add(line[0]);
		auto name = proteins.peek()->proteins[protid].name;

		/* check duplicates */
		if (seen.count(name)) {
			emit message({"Could not parse complete file!",
			              QString{"Stopped at multiple occurance of protein '%1'!"}.arg(name)});
			return {};
		}
		seen.insert(name);

		/* read coefficients */
		bool success = true;
		std::vector<double> coeffs((size_t)len);
		for (int i = 0; i < len; ++i) {
			bool ok;
			coeffs[(size_t)i] = line[i+1].toDouble(&ok);
			success = success && ok;
		}
		if (!success) {
			QString err{"Stopped at protein '%1', malformed row!"};
			emit message({"Could not parse complete file!", err.arg(name)});
			break; // avoid message flood
		}

		/* append */
		ret->protIndex[protid] = ret->protIds.size();
		ret->protIds.push_back(protid);
		ret->features.push_back(std::move(coeffs));
	}

	if (ret->features.empty()) {
		emit message({"Could not read any valid data rows from file!"});
		return {};
	}

	finalizeRead(*ret, normalize);
	return ret;
}

void Storage::finalizeRead(Features &data, bool normalize)
{
	/* setup ranges */
	// TODO future work, be resilient to outliers
	// auto range = features::range_of(data.features, 0.99f);
	auto range = features::range_of(data.features);
	// normalize, if needed
	if (normalize && (range.min < 0 || range.max > 1)) {
		QString format{"Values outside expected range (instead [%1, %2])."};
		emit message({format.arg(range.min).arg(range.max),
		              "Cutting off negative values and normalizing to [0, 1].",
		              GuiMessage::INFO});

		// cut off negative values
		range.min = 0.;

		// normalize
		features::normalize(data.features, range);
	}
	data.featureRange = (normalize ? Features::Range{0., 1.} : range);
	data.logSpace = (data.featureRange.min >= 0 && data.featureRange.max > 10000);
	if (data.hasScores())
		data.scoreRange = features::range_of(data.scores);
}

QStringList Storage::trimCrap(QStringList values)
{
	if (values.empty())
		return values;

	/* remove custom shit in our data */
	QString match("[A-Z]{2}20\\d{6}.*?\\([A-Z]{2}(?:-[A-Z]{2})?\\)_(.*?)_\\(?(?:band|o|u)(?:\\+(?:band|o|u))+\\)?_.*?$");
	values.replaceInStrings(QRegularExpression(match), "\\1");

	/* remove common prefix & suffix */
	QString reference = values.front();
	int front = reference.size(), back = reference.size();
	for (auto it = ++values.cbegin(); it != values.cend(); ++it) {
		front = std::min(front, it->size());
		back = std::min(back, it->size());
		for (int i = 0; i < front; ++i) {
			if (it->at(i) != reference[i]) {
				front = i;
				break;
			}
		}
		for (int i = 0; i < back; ++i) {
			if (it->at(it->size()-1 - i) != reference[reference.size()-1 - i]) {
				back = i;
				break;
			}
		}
	}
	match = QString("^.{%1}(.*?).{%2}$").arg(front).arg(back);
	values.replaceInStrings(QRegularExpression(match), "\\1");

	return values;
}
