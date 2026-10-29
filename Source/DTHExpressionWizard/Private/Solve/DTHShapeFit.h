#pragma once

#include "CoreMinimal.h"

class FDTHFaceEvaluator;
struct FDTHFaceRig;

/**
 * Finds face-board values that reproduce a face shape on the character, measured on the real rig.
 *
 * Only DTH's face board is used, as a face-board key can hold it: board curves only, and each control axis on one side
 * (a slider cannot be at both ends at once). The board's response is measured once (every curve alone, and the pairs
 * that act together: the face rig's own pairs, eyeFaceScrunch and jawOpen with every curve, the tongue curves with each
 * other) and gives the starting values. Then every step is measured: the slopes of the curves in play and of the ones
 * that would reduce the remaining error most, a bounded least-squares step, and a line search, each evaluation exact.
 */
class FDTHShapeFit
{
public:
	/** A face-board curve's control axis and side, from the face board (DTHRigKeying::BoardMap). */
	struct FBoardCurve
	{
		FName Curve;
		FName Control;
		int32 Axis = 0;
		float Sign = 1.0f;
	};

	struct FOptions
	{
		/** Wall-clock budget per shape, seconds. */
		double TimeBudget = 2.0;
		/** Curves kept at 0 (e.g. the eye-look curves when gaze is stripped). */
		TSet<FName> Fixed;
		/** Largest curve value. 1 is the face board's range; more is only for finding what limits a shape. */
		double MaxValue = 1.0;
		/** Each control axis on one side, as a face-board key holds it. Off only for finding what limits a shape. */
		bool bOneSide = true;
		/** Optional start values (one per curve), measured from directly instead of the model start; for checking the fit. */
		TArray<double> Start;
	};

	struct FResult
	{
		/** One value per curve (Curves()). */
		TArray<float> Values;
		/** 1 - |residual| / |shape| over the face points, measured. */
		double Match = 0.0;
		double ModelMatch = 0.0;
		int32 Evaluations = 0;
		int32 Iterations = 0;
		/** The largest movement of any face point in the shape (cm). */
		double Largest = 0.0;
		/** Time in the model start, and in the measured steps. */
		double ModelSeconds = 0.0;
		double Seconds = 0.0;
	};

	/** Measures the board on Evaluator's character (its chosen vertices). Rig gives the curve exponents and known pairs. */
	bool Init(FDTHFaceEvaluator& Evaluator, TConstArrayView<FBoardCurve> Board, const FDTHFaceRig& Rig, TArray<FString>& OutReport, FString& OutError);

	/** Fits Target (displacement of each chosen vertex from rest, Unreal axes, cm). */
	FResult Fit(TConstArrayView<FVector3f> Target, const FOptions& Options);

	const TArray<FName>& Curves() const { return CurveNames; }

	/** The face's displacement from rest on the real rig for these values (one per curve). */
	void Displacement(TConstArrayView<float> Values, TArray<FVector3f>& Out)
	{
		TArray<double> C;
		for (const float V : Values) C.Add(V);
		Measure(C, Out);
	}

private:
	struct FPairTerm { int32 A = 0; int32 B = 0; };

	double F(int32 Curve, double X) const { return Exponent[Curve] == 2 ? X * X : X; }
	double DF(int32 Curve, double X) const { return Exponent[Curve] == 2 ? 2.0 * X : 1.0; }
	void Features(TConstArrayView<double> C, TArray<double>& Out) const;
	void Measure(TConstArrayView<double> C, TArray<FVector3f>& OutDisplacement);
	/** Keeps each two-sided control axis on one side (the side with the larger value). */
	void OneSide(TArray<double>& C) const;
	void ModelFit(const TArray<double>& BT, double TT, const TArray<bool>& Free, TArray<double>& C) const;

	FDTHFaceEvaluator* Evaluator = nullptr;
	TArray<FName> CurveNames;
	TArray<int32> Exponent;
	/** Per curve: the curves on the other side of its control axis. */
	TArray<TArray<int32>> Opposite;
	/** Per curve: the curves one board slider drives with it (itself included); they always take the same value. */
	TArray<TArray<int32>> Tied;
	/** Sets each tied group to its mean. */
	void Tie(TArray<double>& C) const;
	TArray<FVector3f> Rest;
	int32 NumPoints = 0;
	/** Measured basis: one column per feature (curves alone, then the pair interactions), 3 * NumPoints rows. */
	TArray<TArray<float>> Basis;
	TArray<FPairTerm> Pairs;
	/** Gram matrix of the basis (features x features). */
	TArray<double> Gram;
	/** The current fit's options (see FOptions). */
	double MaxValue = 1.0;
	bool bOneSideRule = true;
};
