#include "IT3PCalculator.h"
#include <math.h>
#include "string.h"


const double dt = 1.0;
struct ThreePhaseResult threePhaseResult = {0};

struct ThreePhaseResult CalculatePhasePairCurrentFromPhaseCurrent(double L1, double L2, double L3) {
	// Two simple algorithms that should cover most real-world scenarios with 1 & 2-phase charging
	if(L2 < dt && fabs(L1 - L3) < dt) {
		// 1-phase charging
		threePhaseResult.L3_L1 = L1;
		threePhaseResult.L3_L2 = 0.0;
		threePhaseResult.L1_L2 = 0.0;
		threePhaseResult.usedAlgorithm = OnePhase;
		return threePhaseResult;
	}
	else {
		// Check if this is 2-phase charging
		double l3candidate = L3Peak(L1, L2);

		if(fabs(l3candidate - L3) < dt) {
			threePhaseResult.L3_L1 = L1;
			threePhaseResult.L3_L2 = L2;
			threePhaseResult.L1_L2 = 0.0;
			threePhaseResult.usedAlgorithm = TwoPhase;
			return threePhaseResult;
		}
	}

	// Real three phase: Search for result
	return SearchForPhasePairCurrentFromPhaseCurrent(L1, L2, L3);
}


struct ThreePhaseResult bestMatch = {0};
struct ThreePhaseResult SearchForPhasePairCurrentFromPhaseCurrent(double L1, double L2, double L3) {

	double bestDiff = 0.0;
	struct ThreePhaseResult threePhaseResultSearch = {0};
	memset(&bestMatch,0, sizeof(bestMatch));

	for(int A = 1; A<=32; A++) {
		for(int B = 1; B<=32; B++) {

			double testL2 = L2Peak((double)A, (double)B);
			//if(isnan(testL2) > 0)
			//	continue;

			if(testL2 > (L2 - dt) && testL2 < (L2 + dt)) {

				// If the calculated L2 current is somewhat within what we're looking for, we run the third for-loop
				for(int C = 0; C<=32; C++) {

					double testL1 = L1Peak((double)A, (double)C);

					double diffSum = (testL2 - L2) + (testL1 - L1);

					if((testL1 > (L1 - dt) && testL1 < (L1 + dt)) && (fabs(diffSum) <= bestDiff || bestMatch.usedAlgorithm == 0)) {

						double testL3 = L3Peak((double)B, (double)C);
						diffSum += (testL3 - L3);

						if((testL3 > (L3 - dt) && testL3 < (L3 + dt)) && (fabs(diffSum) <= bestDiff || bestMatch.usedAlgorithm == 0)) {

							bestDiff = fabs(diffSum);

							/// Since we start at 1 to avoid NaN, round down if 1
							if(A == 1)
								threePhaseResultSearch.L1_L2 = 0.0;
							else
								threePhaseResultSearch.L1_L2 = (double)A;

							if(B == 1)
								threePhaseResultSearch.L3_L2 = 0.0;
							else
								threePhaseResultSearch.L3_L2 = (double)B;

							if(C == 1)
								threePhaseResultSearch.L3_L1 = 0.0;
							else
								threePhaseResultSearch.L3_L1 = (double)C;

							threePhaseResultSearch.usedAlgorithm = Search;
							bestMatch = threePhaseResultSearch;
						}
					}
				}
			}
		}
	}

	return bestMatch;
}

double peakX(int aSign, double r, int shift) {
	volatile double a = aSign / (100.0 * M_PI);
	volatile double b = atan(-7.0 / 6.0 * r - sqrt(3.0) / 3.0);
	volatile double c = shift / 300.0;

	return a * b + c;
	//return aSign / (100.0 * M_PI) * atan(-7.0 / 6.0 * r - sqrt(3.0) / 3.0) + shift / 300.0;
}

double phaseMax(double x, double ampA, int shiftA, double ampB, int shiftB) {
	//if(isnan(x) > 0)
	//	return 0.0;
	volatile double a = (ampA * sin(x * M_PI / 0.01 + shiftA * M_PI / 180.0));
	volatile double b = (ampB * sin(x * M_PI / 0.01 + shiftB * M_PI / 180.0));

	return a + b;
	//return (ampA * sin(x * M_PI / 0.01 + shiftA * M_PI / 180.0)) + (ampB * sin(x * M_PI / 0.01 + shiftB * M_PI / 180.0));
}

double L1Peak(double A, double C) {
	double x = peakX(1, C / A, 2);
	return -phaseMax(x, -A, 0, C, 240);
}

double L2Peak(double A, double B) {
	double x = peakX(-1, B / A, 1);
	return phaseMax(x, A, 0, -B, 120);
}

double L3Peak(double B, double C) {
	double x = peakX(1, B / C, 1);
	return phaseMax(x, B, 120, -C, 240);
}



