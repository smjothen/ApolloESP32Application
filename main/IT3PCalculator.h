#ifndef _IT3PCALCULATOR_H_
#define _IT3PCALCULATOR_H_

#ifdef __cplusplus
extern "C" {
#endif

enum Algorithm
{
	OnePhase,
	TwoPhase,
	Search,
};

struct ThreePhaseResult
{
	// Load on "Phase 8"
	float L3_L1;

	// Load on "Phase 6"
	float L3_L2;

	// Load on "Phase 5"
	float L1_L2;

	enum Algorithm usedAlgorithm;
};

struct ThreePhaseResult CalculatePhasePairCurrentFromPhaseCurrent(double L1, double L2, double L3);
struct ThreePhaseResult SearchForPhasePairCurrentFromPhaseCurrent(double L1, double L2, double L3);
double L1Peak(double A, double C);
double L2Peak(double A, double B);
double L3Peak(double B, double C);

#ifdef __cplusplus
}
#endif

#endif  /*_IT3PCALCULATOR_H_*/
