#include "colors.h"

#include <opencv2/imgproc/imgproc.hpp>

const QVector<QColor> Palette::tableau20({
	{31, 119, 180}, {174, 199, 232}, {255, 127, 14}, {255, 187, 120},
	{44, 160, 44}, {152, 223, 138}, {214, 39, 40}, {255, 152, 150},
	{148, 103, 189}, {197, 176, 213}, {140, 86, 75}, {196, 156, 148},
	{227, 119, 194}, {247, 182, 210}, {127, 127, 127}, {199, 199, 199},
	{188, 189, 34}, {219, 219, 141}, {23, 190, 207}, {158, 218, 229}
});

const QVector<QColor> Palette::iwanthue20({{221, 69, 50},
                                           {71, 108, 45},
										   {92, 151, 213},
										   {219, 129, 45},
										   {139, 99, 41},
										   {90, 186, 72},
										   {137, 53, 84},
										   {214, 127, 175},
										   {76, 74, 146},
										   {79, 183, 153},
										   {196, 167, 64},
										   {216, 67, 109},
										   {161, 131, 221},
										   {193, 76, 215},
										   {150, 173, 79},
										   {99, 66, 198},
										   {129, 52, 131},
										   {220, 136, 108},
										   {215, 71, 166},
										   {147, 55, 38}});

QColor Colormap::qcolor(const cv::Vec3b &color)
{
	return QColor(color[0], color[1], color[2]);
}

cv::Mat1b Colormap::prepare(const cv::Mat &source, double scale, double minVal)
{
	cv::Mat1b ret(source.rows, source.cols);
	scale *= 255.;
	source.convertTo(ret, CV_8U, scale, -scale*minVal);
	return ret;
}

cv::Mat3b Colormap::apply(const cv::Mat &source, double scale, double minVal)
{
	return apply(prepare(source, scale, minVal));
}

cv::Vec3b Colormap::apply(double value, double min, double max)
{
	auto index = (value - min) / (max - min) * 255;
	if (index < 0 || index > 255)
		return Qt::black;
	return map[(unsigned)index];
}

cv::Mat3b Colormap::apply(const cv::Mat1b &source)
{
	cv::Mat3b ret;
	cv::applyColorMap(source, ret, map);
	return ret;
}

QPixmap Colormap::pixmap(const cv::Mat3b &source)
{
	return QPixmap::fromImage({source.data, source.cols, source.rows,
	                           (int)source.step, QImage::Format_RGB888});
}

/* Magma map from https://github.com/BIDS/colormap/blob/master/colormaps.py */
Colormap Colormap::magma({{
	{0, 0, 4},
	{1, 0, 5},
	{1, 1, 6},
	{1, 1, 8},
	{2, 1, 9},
	{2, 2, 11},
	{2, 2, 13},
	{3, 3, 15},
	{3, 3, 18},
	{4, 4, 20},
	{5, 4, 22},
	{6, 5, 24},
	{6, 5, 26},
	{7, 6, 28},
	{8, 7, 30},
	{9, 7, 32},
	{10, 8, 34},
	{11, 9, 36},
	{12, 9, 38},
	{13, 10, 41},
	{14, 11, 43},
	{16, 11, 45},
	{17, 12, 47},
	{18, 13, 49},
	{19, 13, 52},
	{20, 14, 54},
	{21, 14, 56},
	{22, 15, 59},
	{24, 15, 61},
	{25, 16, 63},
	{26, 16, 66},
	{28, 16, 68},
	{29, 17, 71},
	{30, 17, 73},
	{32, 17, 75},
	{33, 17, 78},
	{34, 17, 80},
	{36, 18, 83},
	{37, 18, 85},
	{39, 18, 88},
	{41, 17, 90},
	{42, 17, 92},
	{44, 17, 95},
	{45, 17, 97},
	{47, 17, 99},
	{49, 17, 101},
	{51, 16, 103},
	{52, 16, 105},
	{54, 16, 107},
	{56, 16, 108},
	{57, 15, 110},
	{59, 15, 112},
	{61, 15, 113},
	{63, 15, 114},
	{64, 15, 116},
	{66, 15, 117},
	{68, 15, 118},
	{69, 16, 119},
	{71, 16, 120},
	{73, 16, 120},
	{74, 16, 121},
	{76, 17, 122},
	{78, 17, 123},
	{79, 18, 123},
	{81, 18, 124},
	{82, 19, 124},
	{84, 19, 125},
	{86, 20, 125},
	{87, 21, 126},
	{89, 21, 126},
	{90, 22, 126},
	{92, 22, 127},
	{93, 23, 127},
	{95, 24, 127},
	{96, 24, 128},
	{98, 25, 128},
	{100, 26, 128},
	{101, 26, 128},
	{103, 27, 128},
	{104, 28, 129},
	{106, 28, 129},
	{107, 29, 129},
	{109, 29, 129},
	{110, 30, 129},
	{112, 31, 129},
	{114, 31, 129},
	{115, 32, 129},
	{117, 33, 129},
	{118, 33, 129},
	{120, 34, 129},
	{121, 34, 130},
	{123, 35, 130},
	{124, 35, 130},
	{126, 36, 130},
	{128, 37, 130},
	{129, 37, 129},
	{131, 38, 129},
	{132, 38, 129},
	{134, 39, 129},
	{136, 39, 129},
	{137, 40, 129},
	{139, 41, 129},
	{140, 41, 129},
	{142, 42, 129},
	{144, 42, 129},
	{145, 43, 129},
	{147, 43, 128},
	{148, 44, 128},
	{150, 44, 128},
	{152, 45, 128},
	{153, 45, 128},
	{155, 46, 127},
	{156, 46, 127},
	{158, 47, 127},
	{160, 47, 127},
	{161, 48, 126},
	{163, 48, 126},
	{165, 49, 126},
	{166, 49, 125},
	{168, 50, 125},
	{170, 51, 125},
	{171, 51, 124},
	{173, 52, 124},
	{174, 52, 123},
	{176, 53, 123},
	{178, 53, 123},
	{179, 54, 122},
	{181, 54, 122},
	{183, 55, 121},
	{184, 55, 121},
	{186, 56, 120},
	{188, 57, 120},
	{189, 57, 119},
	{191, 58, 119},
	{192, 58, 118},
	{194, 59, 117},
	{196, 60, 117},
	{197, 60, 116},
	{199, 61, 115},
	{200, 62, 115},
	{202, 62, 114},
	{204, 63, 113},
	{205, 64, 113},
	{207, 64, 112},
	{208, 65, 111},
	{210, 66, 111},
	{211, 67, 110},
	{213, 68, 109},
	{214, 69, 108},
	{216, 69, 108},
	{217, 70, 107},
	{219, 71, 106},
	{220, 72, 105},
	{222, 73, 104},
	{223, 74, 104},
	{224, 76, 103},
	{226, 77, 102},
	{227, 78, 101},
	{228, 79, 100},
	{229, 80, 100},
	{231, 82, 99},
	{232, 83, 98},
	{233, 84, 98},
	{234, 86, 97},
	{235, 87, 96},
	{236, 88, 96},
	{237, 90, 95},
	{238, 91, 94},
	{239, 93, 94},
	{240, 95, 94},
	{241, 96, 93},
	{242, 98, 93},
	{242, 100, 92},
	{243, 101, 92},
	{244, 103, 92},
	{244, 105, 92},
	{245, 107, 92},
	{246, 108, 92},
	{246, 110, 92},
	{247, 112, 92},
	{247, 114, 92},
	{248, 116, 92},
	{248, 118, 92},
	{249, 120, 93},
	{249, 121, 93},
	{249, 123, 93},
	{250, 125, 94},
	{250, 127, 94},
	{250, 129, 95},
	{251, 131, 95},
	{251, 133, 96},
	{251, 135, 97},
	{252, 137, 97},
	{252, 138, 98},
	{252, 140, 99},
	{252, 142, 100},
	{252, 144, 101},
	{253, 146, 102},
	{253, 148, 103},
	{253, 150, 104},
	{253, 152, 105},
	{253, 154, 106},
	{253, 155, 107},
	{254, 157, 108},
	{254, 159, 109},
	{254, 161, 110},
	{254, 163, 111},
	{254, 165, 113},
	{254, 167, 114},
	{254, 169, 115},
	{254, 170, 116},
	{254, 172, 118},
	{254, 174, 119},
	{254, 176, 120},
	{254, 178, 122},
	{254, 180, 123},
	{254, 182, 124},
	{254, 183, 126},
	{254, 185, 127},
	{254, 187, 129},
	{254, 189, 130},
	{254, 191, 132},
	{254, 193, 133},
	{254, 194, 135},
	{254, 196, 136},
	{254, 198, 138},
	{254, 200, 140},
	{254, 202, 141},
	{254, 204, 143},
	{254, 205, 144},
	{254, 207, 146},
	{254, 209, 148},
	{254, 211, 149},
	{254, 213, 151},
	{254, 215, 153},
	{254, 216, 154},
	{253, 218, 156},
	{253, 220, 158},
	{253, 222, 160},
	{253, 224, 161},
	{253, 226, 163},
	{253, 227, 165},
	{253, 229, 167},
	{253, 231, 169},
	{253, 233, 170},
	{253, 235, 172},
	{252, 236, 174},
	{252, 238, 176},
	{252, 240, 178},
	{252, 242, 180},
	{252, 244, 182},
	{252, 246, 184},
	{252, 247, 185},
	{252, 249, 187},
	{252, 251, 189},
	{252, 253, 191},
}});

/* Viridis map from https://github.com/BIDS/colormap/blob/master/colormaps.py */
Colormap Colormap::viridis({{
	{68, 1, 84},
	{68, 2, 85},
	{68, 3, 87},
	{69, 5, 88},
	{69, 6, 90},
	{69, 8, 91},
	{70, 9, 92},
	{70, 11, 94},
	{70, 12, 95},
	{70, 14, 97},
	{71, 15, 98},
	{71, 17, 99},
	{71, 18, 101},
	{71, 20, 102},
	{71, 21, 103},
	{71, 22, 105},
	{71, 24, 106},
	{72, 25, 107},
	{72, 26, 108},
	{72, 28, 110},
	{72, 29, 111},
	{72, 30, 112},
	{72, 32, 113},
	{72, 33, 114},
	{72, 34, 115},
	{72, 35, 116},
	{71, 37, 117},
	{71, 38, 118},
	{71, 39, 119},
	{71, 40, 120},
	{71, 42, 121},
	{71, 43, 122},
	{71, 44, 123},
	{70, 45, 124},
	{70, 47, 124},
	{70, 48, 125},
	{70, 49, 126},
	{69, 50, 127},
	{69, 52, 127},
	{69, 53, 128},
	{69, 54, 129},
	{68, 55, 129},
	{68, 57, 130},
	{67, 58, 131},
	{67, 59, 131},
	{67, 60, 132},
	{66, 61, 132},
	{66, 62, 133},
	{66, 64, 133},
	{65, 65, 134},
	{65, 66, 134},
	{64, 67, 135},
	{64, 68, 135},
	{63, 69, 135},
	{63, 71, 136},
	{62, 72, 136},
	{62, 73, 137},
	{61, 74, 137},
	{61, 75, 137},
	{61, 76, 137},
	{60, 77, 138},
	{60, 78, 138},
	{59, 80, 138},
	{59, 81, 138},
	{58, 82, 139},
	{58, 83, 139},
	{57, 84, 139},
	{57, 85, 139},
	{56, 86, 139},
	{56, 87, 140},
	{55, 88, 140},
	{55, 89, 140},
	{54, 90, 140},
	{54, 91, 140},
	{53, 92, 140},
	{53, 93, 140},
	{52, 94, 141},
	{52, 95, 141},
	{51, 96, 141},
	{51, 97, 141},
	{50, 98, 141},
	{50, 99, 141},
	{49, 100, 141},
	{49, 101, 141},
	{49, 102, 141},
	{48, 103, 141},
	{48, 104, 141},
	{47, 105, 141},
	{47, 106, 141},
	{46, 107, 142},
	{46, 108, 142},
	{46, 109, 142},
	{45, 110, 142},
	{45, 111, 142},
	{44, 112, 142},
	{44, 113, 142},
	{44, 114, 142},
	{43, 115, 142},
	{43, 116, 142},
	{42, 117, 142},
	{42, 118, 142},
	{42, 119, 142},
	{41, 120, 142},
	{41, 121, 142},
	{40, 122, 142},
	{40, 122, 142},
	{40, 123, 142},
	{39, 124, 142},
	{39, 125, 142},
	{39, 126, 142},
	{38, 127, 142},
	{38, 128, 142},
	{38, 129, 142},
	{37, 130, 142},
	{37, 131, 141},
	{36, 132, 141},
	{36, 133, 141},
	{36, 134, 141},
	{35, 135, 141},
	{35, 136, 141},
	{35, 137, 141},
	{34, 137, 141},
	{34, 138, 141},
	{34, 139, 141},
	{33, 140, 141},
	{33, 141, 140},
	{33, 142, 140},
	{32, 143, 140},
	{32, 144, 140},
	{32, 145, 140},
	{31, 146, 140},
	{31, 147, 139},
	{31, 148, 139},
	{31, 149, 139},
	{31, 150, 139},
	{30, 151, 138},
	{30, 152, 138},
	{30, 153, 138},
	{30, 153, 138},
	{30, 154, 137},
	{30, 155, 137},
	{30, 156, 137},
	{30, 157, 136},
	{30, 158, 136},
	{30, 159, 136},
	{30, 160, 135},
	{31, 161, 135},
	{31, 162, 134},
	{31, 163, 134},
	{32, 164, 133},
	{32, 165, 133},
	{33, 166, 133},
	{33, 167, 132},
	{34, 167, 132},
	{35, 168, 131},
	{35, 169, 130},
	{36, 170, 130},
	{37, 171, 129},
	{38, 172, 129},
	{39, 173, 128},
	{40, 174, 127},
	{41, 175, 127},
	{42, 176, 126},
	{43, 177, 125},
	{44, 177, 125},
	{46, 178, 124},
	{47, 179, 123},
	{48, 180, 122},
	{50, 181, 122},
	{51, 182, 121},
	{53, 183, 120},
	{54, 184, 119},
	{56, 185, 118},
	{57, 185, 118},
	{59, 186, 117},
	{61, 187, 116},
	{62, 188, 115},
	{64, 189, 114},
	{66, 190, 113},
	{68, 190, 112},
	{69, 191, 111},
	{71, 192, 110},
	{73, 193, 109},
	{75, 194, 108},
	{77, 194, 107},
	{79, 195, 105},
	{81, 196, 104},
	{83, 197, 103},
	{85, 198, 102},
	{87, 198, 101},
	{89, 199, 100},
	{91, 200, 98},
	{94, 201, 97},
	{96, 201, 96},
	{98, 202, 95},
	{100, 203, 93},
	{103, 204, 92},
	{105, 204, 91},
	{107, 205, 89},
	{109, 206, 88},
	{112, 206, 86},
	{114, 207, 85},
	{116, 208, 84},
	{119, 208, 82},
	{121, 209, 81},
	{124, 210, 79},
	{126, 210, 78},
	{129, 211, 76},
	{131, 211, 75},
	{134, 212, 73},
	{136, 213, 71},
	{139, 213, 70},
	{141, 214, 68},
	{144, 214, 67},
	{146, 215, 65},
	{149, 215, 63},
	{151, 216, 62},
	{154, 216, 60},
	{157, 217, 58},
	{159, 217, 56},
	{162, 218, 55},
	{165, 218, 53},
	{167, 219, 51},
	{170, 219, 50},
	{173, 220, 48},
	{175, 220, 46},
	{178, 221, 44},
	{181, 221, 43},
	{183, 221, 41},
	{186, 222, 39},
	{189, 222, 38},
	{191, 223, 36},
	{194, 223, 34},
	{197, 223, 33},
	{199, 224, 31},
	{202, 224, 30},
	{205, 224, 29},
	{207, 225, 28},
	{210, 225, 27},
	{212, 225, 26},
	{215, 226, 25},
	{218, 226, 24},
	{220, 226, 24},
	{223, 227, 24},
	{225, 227, 24},
	{228, 227, 24},
	{231, 228, 25},
	{233, 228, 25},
	{236, 228, 26},
	{238, 229, 27},
	{241, 229, 28},
	{243, 229, 30},
	{246, 230, 31},
	{248, 230, 33},
	{250, 230, 34},
	{253, 231, 36},
}});

Colormap Colormap::stoplight({{
	{255, 0, 0},
	{254, 1, 0},
	{253, 2, 0},
	{252, 3, 0},
	{251, 4, 0},
	{250, 5, 0},
	{249, 6, 0},
	{248, 7, 0},
	{247, 8, 0},
	{246, 9, 0},
	{245, 10, 0},
	{244, 11, 0},
	{243, 12, 0},
	{242, 13, 0},
	{241, 14, 0},
	{240, 15, 0},
	{239, 16, 0},
	{238, 17, 0},
	{237, 18, 0},
	{236, 19, 0},
	{235, 20, 0},
	{234, 21, 0},
	{233, 22, 0},
	{232, 23, 0},
	{231, 24, 0},
	{230, 25, 0},
	{229, 26, 0},
	{228, 27, 0},
	{227, 28, 0},
	{226, 29, 0},
	{225, 30, 0},
	{224, 31, 0},
	{223, 32, 0},
	{222, 33, 0},
	{221, 34, 0},
	{220, 35, 0},
	{219, 36, 0},
	{218, 37, 0},
	{217, 38, 0},
	{216, 39, 0},
	{215, 40, 0},
	{214, 41, 0},
	{213, 42, 0},
	{212, 43, 0},
	{211, 44, 0},
	{210, 45, 0},
	{209, 46, 0},
	{208, 47, 0},
	{207, 48, 0},
	{206, 49, 0},
	{205, 50, 0},
	{204, 51, 0},
	{203, 52, 0},
	{202, 53, 0},
	{201, 54, 0},
	{200, 55, 0},
	{199, 56, 0},
	{198, 57, 0},
	{197, 58, 0},
	{196, 59, 0},
	{195, 60, 0},
	{194, 61, 0},
	{193, 62, 0},
	{192, 63, 0},
	{191, 64, 0},
	{190, 65, 0},
	{189, 66, 0},
	{188, 67, 0},
	{187, 68, 0},
	{186, 69, 0},
	{185, 70, 0},
	{184, 71, 0},
	{183, 72, 0},
	{182, 73, 0},
	{181, 74, 0},
	{180, 75, 0},
	{179, 76, 0},
	{178, 77, 0},
	{177, 78, 0},
	{176, 79, 0},
	{175, 80, 0},
	{174, 81, 0},
	{173, 82, 0},
	{172, 83, 0},
	{171, 84, 0},
	{170, 85, 0},
	{169, 86, 0},
	{168, 87, 0},
	{167, 88, 0},
	{166, 89, 0},
	{165, 90, 0},
	{164, 91, 0},
	{163, 92, 0},
	{162, 93, 0},
	{161, 94, 0},
	{160, 95, 0},
	{159, 96, 0},
	{158, 97, 0},
	{157, 98, 0},
	{156, 99, 0},
	{155, 100, 0},
	{154, 101, 0},
	{153, 102, 0},
	{152, 103, 0},
	{151, 104, 0},
	{150, 105, 0},
	{149, 106, 0},
	{148, 107, 0},
	{147, 108, 0},
	{146, 109, 0},
	{145, 110, 0},
	{144, 111, 0},
	{143, 112, 0},
	{142, 113, 0},
	{141, 114, 0},
	{140, 115, 0},
	{139, 116, 0},
	{138, 117, 0},
	{137, 118, 0},
	{136, 119, 0},
	{135, 120, 0},
	{134, 121, 0},
	{133, 122, 0},
	{132, 123, 0},
	{131, 124, 0},
	{130, 125, 0},
	{129, 126, 0},
	{128, 127, 0},
	{127, 128, 0},
	{126, 129, 0},
	{125, 130, 0},
	{124, 131, 0},
	{123, 132, 0},
	{122, 133, 0},
	{121, 134, 0},
	{120, 135, 0},
	{119, 136, 0},
	{118, 137, 0},
	{117, 138, 0},
	{116, 139, 0},
	{115, 140, 0},
	{114, 141, 0},
	{113, 142, 0},
	{112, 143, 0},
	{111, 144, 0},
	{110, 145, 0},
	{109, 146, 0},
	{108, 147, 0},
	{107, 148, 0},
	{106, 149, 0},
	{105, 150, 0},
	{104, 151, 0},
	{103, 152, 0},
	{102, 153, 0},
	{101, 154, 0},
	{100, 155, 0},
	{99, 156, 0},
	{98, 157, 0},
	{97, 158, 0},
	{96, 159, 0},
	{95, 160, 0},
	{94, 161, 0},
	{93, 162, 0},
	{92, 163, 0},
	{91, 164, 0},
	{90, 165, 0},
	{89, 166, 0},
	{88, 167, 0},
	{87, 168, 0},
	{86, 169, 0},
	{85, 170, 0},
	{84, 171, 0},
	{83, 172, 0},
	{82, 173, 0},
	{81, 174, 0},
	{80, 175, 0},
	{79, 176, 0},
	{78, 177, 0},
	{77, 178, 0},
	{76, 179, 0},
	{75, 180, 0},
	{74, 181, 0},
	{73, 182, 0},
	{72, 183, 0},
	{71, 184, 0},
	{70, 185, 0},
	{69, 186, 0},
	{68, 187, 0},
	{67, 188, 0},
	{66, 189, 0},
	{65, 190, 0},
	{64, 191, 0},
	{63, 192, 0},
	{62, 193, 0},
	{61, 194, 0},
	{60, 195, 0},
	{59, 196, 0},
	{58, 197, 0},
	{57, 198, 0},
	{56, 199, 0},
	{55, 200, 0},
	{54, 201, 0},
	{53, 202, 0},
	{52, 203, 0},
	{51, 204, 0},
	{50, 205, 0},
	{49, 206, 0},
	{48, 207, 0},
	{47, 208, 0},
	{46, 209, 0},
	{45, 210, 0},
	{44, 211, 0},
	{43, 212, 0},
	{42, 213, 0},
	{41, 214, 0},
	{40, 215, 0},
	{39, 216, 0},
	{38, 217, 0},
	{37, 218, 0},
	{36, 219, 0},
	{35, 220, 0},
	{34, 221, 0},
	{33, 222, 0},
	{32, 223, 0},
	{31, 224, 0},
	{30, 225, 0},
	{29, 226, 0},
	{28, 227, 0},
	{27, 228, 0},
	{26, 229, 0},
	{25, 230, 0},
	{24, 231, 0},
	{23, 232, 0},
	{22, 233, 0},
	{21, 234, 0},
	{20, 235, 0},
	{19, 236, 0},
	{18, 237, 0},
	{17, 238, 0},
	{16, 239, 0},
	{15, 240, 0},
	{14, 241, 0},
	{13, 242, 0},
	{12, 243, 0},
	{11, 244, 0},
	{10, 245, 0},
	{9, 246, 0},
	{8, 247, 0},
	{7, 248, 0},
	{6, 249, 0},
	{5, 250, 0},
	{4, 251, 0},
	{3, 252, 0},
	{2, 253, 0},
	{1, 254, 0},
	{0, 255, 0},
}});

Colormap Colormap::stoplight_mild({{
	{255, 128, 128},
	{254, 128, 128},
	{254, 129, 128},
	{253, 129, 128},
	{253, 130, 128},
	{252, 130, 128},
	{252, 131, 128},
	{251, 131, 128},
	{251, 132, 128},
	{250, 132, 128},
	{250, 133, 128},
	{249, 133, 128},
	{249, 134, 128},
	{248, 134, 128},
	{248, 135, 128},
	{247, 135, 128},
	{247, 136, 128},
	{246, 136, 128},
	{246, 137, 128},
	{245, 137, 128},
	{245, 138, 128},
	{244, 138, 128},
	{244, 139, 128},
	{243, 139, 128},
	{243, 140, 128},
	{242, 140, 128},
	{242, 141, 128},
	{241, 141, 128},
	{241, 142, 128},
	{240, 142, 128},
	{240, 143, 128},
	{239, 143, 128},
	{239, 144, 128},
	{238, 144, 128},
	{238, 145, 128},
	{237, 145, 128},
	{237, 146, 128},
	{236, 146, 128},
	{236, 147, 128},
	{235, 147, 128},
	{235, 148, 128},
	{234, 148, 128},
	{234, 149, 128},
	{233, 149, 128},
	{233, 150, 128},
	{232, 150, 128},
	{232, 151, 128},
	{231, 151, 128},
	{231, 152, 128},
	{230, 152, 128},
	{230, 153, 128},
	{229, 153, 128},
	{229, 154, 128},
	{228, 154, 128},
	{228, 155, 128},
	{227, 155, 128},
	{227, 156, 128},
	{226, 156, 128},
	{226, 157, 128},
	{225, 157, 128},
	{225, 158, 128},
	{224, 158, 128},
	{224, 159, 128},
	{223, 159, 128},
	{223, 160, 128},
	{222, 160, 128},
	{222, 161, 128},
	{221, 161, 128},
	{221, 162, 128},
	{220, 162, 128},
	{220, 163, 128},
	{219, 163, 128},
	{219, 164, 128},
	{218, 164, 128},
	{218, 165, 128},
	{217, 165, 128},
	{217, 166, 128},
	{216, 166, 128},
	{216, 167, 128},
	{215, 167, 128},
	{215, 168, 128},
	{214, 168, 128},
	{214, 169, 128},
	{213, 169, 128},
	{213, 170, 128},
	{212, 170, 128},
	{212, 171, 128},
	{211, 171, 128},
	{211, 172, 128},
	{210, 172, 128},
	{210, 173, 128},
	{209, 173, 128},
	{209, 174, 128},
	{208, 174, 128},
	{208, 175, 128},
	{207, 175, 128},
	{207, 176, 128},
	{206, 176, 128},
	{206, 177, 128},
	{205, 177, 128},
	{205, 178, 128},
	{204, 178, 128},
	{204, 179, 128},
	{203, 179, 128},
	{203, 180, 128},
	{202, 180, 128},
	{202, 181, 128},
	{201, 181, 128},
	{201, 182, 128},
	{200, 182, 128},
	{200, 183, 128},
	{199, 183, 128},
	{199, 184, 128},
	{198, 184, 128},
	{198, 185, 128},
	{197, 185, 128},
	{197, 186, 128},
	{196, 186, 128},
	{196, 187, 128},
	{195, 187, 128},
	{195, 188, 128},
	{194, 188, 128},
	{194, 189, 128},
	{193, 189, 128},
	{193, 190, 128},
	{192, 190, 128},
	{192, 191, 128},
	{191, 191, 128},
	{191, 192, 128},
	{190, 192, 128},
	{190, 193, 128},
	{189, 193, 128},
	{189, 194, 128},
	{188, 194, 128},
	{188, 195, 128},
	{187, 195, 128},
	{187, 196, 128},
	{186, 196, 128},
	{186, 197, 128},
	{185, 197, 128},
	{185, 198, 128},
	{184, 198, 128},
	{184, 199, 128},
	{183, 199, 128},
	{183, 200, 128},
	{182, 200, 128},
	{182, 201, 128},
	{181, 201, 128},
	{181, 202, 128},
	{180, 202, 128},
	{180, 203, 128},
	{179, 203, 128},
	{179, 204, 128},
	{178, 204, 128},
	{178, 205, 128},
	{177, 205, 128},
	{177, 206, 128},
	{176, 206, 128},
	{176, 207, 128},
	{175, 207, 128},
	{175, 208, 128},
	{174, 208, 128},
	{174, 209, 128},
	{173, 209, 128},
	{173, 210, 128},
	{172, 210, 128},
	{172, 211, 128},
	{171, 211, 128},
	{171, 212, 128},
	{170, 212, 128},
	{170, 213, 128},
	{169, 213, 128},
	{169, 214, 128},
	{168, 214, 128},
	{168, 215, 128},
	{167, 215, 128},
	{167, 216, 128},
	{166, 216, 128},
	{166, 217, 128},
	{165, 217, 128},
	{165, 218, 128},
	{164, 218, 128},
	{164, 219, 128},
	{163, 219, 128},
	{163, 220, 128},
	{162, 220, 128},
	{162, 221, 128},
	{161, 221, 128},
	{161, 222, 128},
	{160, 222, 128},
	{160, 223, 128},
	{159, 223, 128},
	{159, 224, 128},
	{158, 224, 128},
	{158, 225, 128},
	{157, 225, 128},
	{157, 226, 128},
	{156, 226, 128},
	{156, 227, 128},
	{155, 227, 128},
	{155, 228, 128},
	{154, 228, 128},
	{154, 229, 128},
	{153, 229, 128},
	{153, 230, 128},
	{152, 230, 128},
	{152, 231, 128},
	{151, 231, 128},
	{151, 232, 128},
	{150, 232, 128},
	{150, 233, 128},
	{149, 233, 128},
	{149, 234, 128},
	{148, 234, 128},
	{148, 235, 128},
	{147, 235, 128},
	{147, 236, 128},
	{146, 236, 128},
	{146, 237, 128},
	{145, 237, 128},
	{145, 238, 128},
	{144, 238, 128},
	{144, 239, 128},
	{143, 239, 128},
	{143, 240, 128},
	{142, 240, 128},
	{142, 241, 128},
	{141, 241, 128},
	{141, 242, 128},
	{140, 242, 128},
	{140, 243, 128},
	{139, 243, 128},
	{139, 244, 128},
	{138, 244, 128},
	{138, 245, 128},
	{137, 245, 128},
	{137, 246, 128},
	{136, 246, 128},
	{136, 247, 128},
	{135, 247, 128},
	{135, 248, 128},
	{134, 248, 128},
	{134, 249, 128},
	{133, 249, 128},
	{133, 250, 128},
	{132, 250, 128},
	{132, 251, 128},
	{131, 251, 128},
	{131, 252, 128},
	{130, 252, 128},
	{130, 253, 128},
	{129, 253, 128},
	{129, 254, 128},
	{128, 254, 128},
	{128, 255, 128},
	{127, 255, 128},
}});
