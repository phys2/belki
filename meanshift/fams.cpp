/////////////////////////////////////////////////////////////////////////////
// Name:        fams.cpp
// Purpose:     fast adaptive meanshift implementation
// Author:      Ilan Shimshoni
// Modified by: Bogdan Georgescu
// Created:     08/14/2003
// Version:     v0.1
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//Modified by : Maithili  Paranjape
//Date	      : 09/09/04
//Functions modified : PruneModes
//Function added : SaveMymodes
//Version     : v0.2
/////////////////////////////////////////////////////////////////////////////

#include "fams.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iterator>
#include <functional>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

using namespace std;

namespace seg_meanshift {

FAMS::FAMS(Config cfg)
    : config(cfg)
{}

FAMS::~FAMS() {
}

#ifndef UNIX
#define drand48()    (rand() * 1.0 / RAND_MAX)
#endif

void FAMS::resetState() {
	progress = progress_old = 0.;
	cancelled = false;
	modes = std::vector<Mode>(modes.size());
	prunedModes = {};
	prunedIndex = {};
}

// Choose a subset of points on which to perform the mean shift operation
void FAMS::selectStartPoints(double percent, int jump) {
	if (datapoints.empty())
		return;

	size_t selectionSize;
	if (percent > 0.) {
		selectionSize = (size_t)(n_ * percent / 100.0);
	} else  {
		selectionSize = (size_t)ceil(n_ / (jump + 0.0));
	}

	if (selectionSize != startPoints.size()) {
		startPoints.resize(selectionSize);
		modes.resize(selectionSize);
	}

	if (percent > 0.) {
		for (size_t i = 0; i < startPoints.size();  i++)
			startPoints[i] = &datapoints[(int)(drand48() * n_) % n_];
	} else {
		for (size_t i = 0; i < startPoints.size(); i++)
			startPoints[i] = &datapoints[i * jump];
	}
}

void FAMS::importStartPoints(std::vector<Point> &points)
{
	/* add all points as starting points */
	startPoints.resize(points.size());
	for (size_t i = 0; i < points.size(); ++i)
		startPoints[i] = &points[i];
	modes.resize(startPoints.size());
}

void FAMS::ComputePilotPoint::operator()(const tbb::blocked_range<int> &r)
{
	const int thresh = (int)(fams.config.k * std::sqrt((float)fams.n_));
	const int win_j = 10, max_win = 7000;
	const int mwpwj = max_win / win_j;
	unsigned int nn;
	unsigned int wjd = (unsigned int)(win_j * fams.d_);

	int done = 0;
	for (int j = r.begin(); j != r.end(); ++j) {
		int numn = 0;
		int numns[mwpwj];
		memset(numns, 0, sizeof(numns));

		for (unsigned int i = 0; i < fams.n_; i++) {
			nn = fams.DistL1(fams.datapoints[j], fams.datapoints[i]) / wjd; // TODO L2
			if (nn < mwpwj)
				numns[nn]++;
		}

		// determine distance to k-nearest neighbour
		for (nn = 0; nn < mwpwj; nn++) {
			numn += numns[nn];
			if (numn > thresh) {
				break;
			}
		}

		if (numn <= thresh) {
			dbg_noknn++;
		}

		fams.datapoints[j].window = (nn + 1) * wjd;
		fams.datapoints[j].weightdp2 = pow(
					FAMS_FLOAT_SHIFT / fams.datapoints[j].window,
					(fams.d_ + 2) * FAMS_ALPHA);
		if (weights) {
			fams.datapoints[j].weightdp2 *= (*weights)[j];
		}

		dbg_acc += fams.datapoints[j].window;

		if (fams.n_ < 50 || (++done % (fams.n_ / 50)) == 0) {
			bool cont = fams.progressUpdate((float)done/(float)fams.n_ * 20.f,
				false);
			if (!cont) {
				bgLog("ComputePilot aborted.\n");
				return;
			}
			done = 0;
		}
	}
	fams.progressUpdate((float)done/(float)fams.n_ * 20.f, false);
}

// compute the pilot h_i's for the data points
bool FAMS::ComputePilot(vector<double> *weights) {
	bgLog("compute bandwidths...\n");

	ComputePilotPoint comp(*this, weights);
	tbb::parallel_reduce(tbb::blocked_range<int>(0, n_),
						 comp);

	cout << "Avg. window size: " << comp.dbg_acc / n_ << endl;
	bgLog("No kNN found for %2.2f%% of all points\n",
		  (float) comp.dbg_noknn / n_ * 100);

	return !(progress < 0.f); // in case of abort, progress is set to -1
}

// compute real bandwiths for selected points
void FAMS::ComputeRealBandwidths(unsigned int h) {
	const int thresh = (int)(config.k * std::sqrt((float)n_));
	const int    win_j = 10, max_win = 7000;
	unsigned int nn;
	unsigned int wjd;
	wjd =        (unsigned int)(win_j * d_);
	if (h == 0) {
		for (size_t j = 0; j < startPoints.size(); j++) {
			int numn = 0;
			int numns[max_win / win_j];
			memset(numns, 0, sizeof(numns));
			for (unsigned int i = 0; i < n_; i++) {
				nn = DistL1(*startPoints[j], datapoints[i]) / wjd;
				if (nn < max_win / win_j)
					numns[nn]++;
			}
			for (nn = 0; nn < max_win / win_j; nn++) {
				numn += numns[nn];
				if (numn > thresh) {
					break;
				}
			}
			startPoints[j]->window = (nn + 1) * win_j;
		}
	} else{
		for (size_t j = 0; j < startPoints.size(); j++) {
			startPoints[j]->window = h;
		}
	}
}

// perform a FAMS iteration
unsigned int FAMS::DoMSAdaptiveIteration(const std::vector<unsigned short> &old,
										 std::vector<unsigned short> &ret) const
{
	double total_weight = 0;
	std::vector<double> rr(d_, 0.);
	unsigned int crtH = 0;
	double       hmdist = 1e100;
	for (auto &ptp : datapoints) {
		auto [inside, dist] = DistL1(old, ptp, ptp.window); // TODO L2
		if (!inside)
		    continue;

		double x = 1.0 - (dist / ptp.window);
		double w = ptp.weightdp2 * x * x * ptp.factor;
		total_weight += w;
		for (size_t j = 0; j < ptp.data->size(); j++)
		    rr[j] += (*ptp.data)[j] * w;
		if (dist < hmdist) {
			hmdist = dist;
			crtH   = ptp.window;
		}
	}
	if (total_weight == 0) {
		return 0;
	}
	for (unsigned int i = 0; i < d_; i++)
		ret[i] = (unsigned short)(rr[i] / total_weight);

	return crtH;
}

void FAMS::MeanShiftPoint::operator()(const tbb::blocked_range<int> &r)
const
{
	// initialize mean vectors to zero
	std::vector<unsigned short>
			oldMean(fams.d_, 0),
			crtMean(fams.d_, 0);
	unsigned int *crtWindow;

	int done = 0;
	for (int jj = r.begin(); jj != r.end(); ++jj) {

		// update mode's window directly
		crtWindow  = &fams.modes[jj].window;
		// set initial values
		Point *p = fams.startPoints[jj];
		crtMean    = *p->data;
		*crtWindow = p->window;

		for (int iter = 0; oldMean != crtMean && (iter < FAMS_MAXITER);
			 iter++) {
			oldMean = crtMean;
			unsigned newWindow = fams.DoMSAdaptiveIteration(oldMean, crtMean);
			if (!newWindow) {
				// oldMean is final mean -> break loop
				break;
			}
			*crtWindow = newWindow;
		}

		// algorithm converged, store result if we do not already know it
		if (fams.modes[jj].data.empty()) {
			fams.modes[jj].data = crtMean;
		}

		// progress reporting
		if (fams.startPoints.size() < 320 ||
		    (++done % (fams.startPoints.size() / 320)) == 0) {
			bool cont = fams.progressUpdate((float)done/
											(float)fams.startPoints.size()*80.f,
											false);
			if (!cont) {
				bgLog("FinishFAMS aborted.\n");
				return;
			}
			done = 0;
		}
	}
	fams.progressUpdate((float)done/(float)fams.startPoints.size()*80.f, false);
}

// perform FAMS starting from a subset of the data points.
// return true on successful finish (not cancelled by user through update feedback)
bool FAMS::finishFAMS() {
	bgLog(" Start MS iterations\n");

	tbb::parallel_for(tbb::blocked_range<int>(0, startPoints.size()),
	                  MeanShiftPoint(*this));

	bgLog("done.\n");
	return !(progress < 0.f); // in case of abort, progress is set to -1
}

// initialize bandwidths
bool FAMS::prepareFAMS(vector<double> *bandwidths, vector<double> *factors) {
	assert(!datapoints.empty());

	//Compute pilot if necessary
	bgLog(" Run pilot ");
	bool cont = true;
	bool adaptive = (config.bandwidth <= 0. && bandwidths == nullptr);
	if (adaptive) {  // adaptive bandwidths
		bgLog(bandwidths ? "adaptive using weights..." : "adaptive...");
		cont = ComputePilot(bandwidths);
	} else if (bandwidths != nullptr) {  // preset bandwidths
		bgLog("fixed bandwidth (local value)...");
		assert(bandwidths->size() == n_);
		cout << "maxVal_ = " << maxVal_ << endl;
		cout << "minVal_ = " << minVal_ << endl;
		for (unsigned int i = 0; i < n_; i++) {
			double width = bandwidths->at(i) * config.bandwidth;
			unsigned int hWidth = value2ushort<unsigned int>(width);

			datapoints[i].window = hWidth;
			datapoints[i].weightdp2 = pow(
						FAMS_FLOAT_SHIFT / datapoints[i].window,
						(d_ + 2) * FAMS_ALPHA);
		}
	} else {  // fixed bandwidth for all points
		bgLog("fixed bandwidth (global value)...");
		int hWidth = value2ushort<int>(config.bandwidth);
		unsigned int hwd = (unsigned int)(hWidth * d_);
		cout << "Window size: " << hwd << endl;
		for (unsigned int i = 0; i < n_; i++) {
			datapoints[i].window    = hwd;
			datapoints[i].weightdp2 = 1;
		}
	}

	/* Set factors */
	if (factors) {
		bgLog(" *** using factors *** ");
		for (unsigned int i = 0; i < n_; i++)
			datapoints[i].factor = factors->at(i);
	} else {
		for (unsigned int i = 0; i < n_; i++)
			datapoints[i].factor = 1.;
	}

	bgLog("done.\n");
	return cont;
}

bool FAMS::progressUpdate(float percent, bool absolute)
{
	if (cancelled) {
		progress = -1;
		return false;
	}

	if (config.verbosity < 1)
		return true;

	tbb::mutex::scoped_lock l(progressMutex);
	if (absolute)
		progress = percent;
	else
		progress += percent;
	if (progress > progress_old + 0.5f) {
		std::cerr << "\r" << progress << " %          \r";
		std::cerr.flush();
		progress_old = progress;
	}

	return true;
}

}
