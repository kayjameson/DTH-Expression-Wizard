#pragma once

#include "CoreMinimal.h"

struct FDTHFaceRig;
struct FDTHExpressionSamples;
struct FDTHDazState;

/** Face-board values that reproduce one Daz expression through DTH's face rig, and what could not be reproduced. */
struct FDTHExpressionFit
{
	/** One value per face-board curve (FDTHFaceRig::Curves), in [0, 1]. */
	TArray<double> Values;
	/** Size of the expression in the fitted leaves, and what is left after the fit (same units). */
	double TargetSize = 0.0;
	double Residual = 0.0;
	/** One line per thing the fit could not reproduce, with the reason. */
	TArray<FString> Missing;
};

/**
 * Fits face-board curves to Daz expression samples.
 *
 * Basis: DTH's ROM frame for each face pose (the ROM's face frames, in the pose asset's order) against frame 0, or
 * against the frame it was posed on top of. Rig: the face rig as measured on the character (FDTHFaceRig).
 * Leaves: the base figure's facs_bs_* / facs_cbs_* values and head bone rotations (degrees / 30), relative to rest.
 * Only the base figure counts: in Unreal one DTH pose drives the mouth, eyes and tear together, so their copies of a
 * leaf cannot differ from the figure's. Fit: values in [0, 1] minimising |basis * rig(values) - target|
 * (Gauss-Newton, each step a bounded least-squares solve by coordinate descent).
 */
class FDTHExpressionSolver
{
public:
	/** bStripGaze: the eye-look curves stay at 0 and eye direction is left out of the target. */
	FDTHExpressionSolver(const FDTHFaceRig& Rig, const FDTHExpressionSamples& Samples, bool bStripGaze);

	/** False (with the reason) if the samples do not hold every ROM frame the rig's poses need. */
	bool IsValid(FString* OutError = nullptr) const;

	FDTHExpressionFit Fit(const FDTHDazState& Expression) const;

	/** Pose weights the rig gives for face-board values (FDTHFaceRig::Evaluate). */
	const FDTHFaceRig& GetRig() const { return Rig; }

private:
	void Leaves(const FDTHDazState& State, const FDTHDazState& Reference, TMap<FString, double>& Out) const;
	int32 LeafIndex(const FString& Key);
	bool IsGazeLeaf(const FString& Key) const;

	const FDTHFaceRig& Rig;
	bool bStripGaze = true;
	FString Error;

	const FDTHDazState* Rest = nullptr;
	TArray<FString> LeafKeys;
	TMap<FString, int32> LeafIndices;
	/** Basis[pose] = sparse leaf deltas of that pose. */
	TArray<TArray<TPair<int32, double>>> Basis;
	/** ROM frame of the first face pose. */
	int32 FirstFrame = INDEX_NONE;
	/** Curves the fit may move (driving at least one pose, gaze stripped if asked). */
	TArray<int32> FreeCurves;
	/** Per leaf: some pose the face rig drives moves it. */
	TArray<bool> bLeafDriven;
};
