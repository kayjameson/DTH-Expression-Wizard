#pragma once

#include "CoreMinimal.h"
#include "DTHFaceRegion.h"

/** One sampled Daz state: final morph values ("Figure|morph") and head bone channels ("Figure|bone|XRotate"). */
struct FDTHDazState
{
	TMap<FString, double> Morphs;
	TMap<FString, double> Bones;
	/** Morphs it turned on that are not Genesis 9 FACS shapes but carry vertex deltas of their own (a sculpted shape). */
	TArray<FString> Sculpted;
	/** ROM frames only: properties keyed on this frame, and the frame it was posed on top of (0: none). */
	TArray<FString> Keyed;
	int32 Base = 0;
};

/** One expression preset as Daz evaluated it on a base Genesis 9. */
struct FDTHDazExpression
{
	/** Content-library folder it came from, e.g. "People/Genesis 9/Expressions/<Vendor>/<Pack>". */
	FString Pack;
	FString Label;
	FString File;
	/** The preset's thumbnail (.duf.png), if it has one. */
	FString Icon;
	bool bLoaded = false;
	/** Why Daz did not apply it, when bLoaded is false. */
	FString Reason;
	FDTHDazState State;
};


/**
 * DTH_ExpressionSamples.json, written by Resources/Daz/DTH_SampleExpressions.dsa.
 *
 * A library can hold hundreds of thousands of expressions, so Load reads only the manifest, the rest state, the ROM and
 * the face region; ReadExpressions streams the expressions one samples file at a time (version 3 writes them 100 to a
 * file). Version 2 files (every expression in the manifest) are read too.
 */
struct FDTHExpressionSamples
{
	FString SampledAt;
	FString DazVersion;
	FDTHDazState Rest;
	/** DTH's face ROM: frame -> state (frame 0 and the Fac frames). */
	TMap<int32, FDTHDazState> RomFrames;
	/** How many expressions ReadExpressions gives. */
	int32 NumExpressions = 0;

	/** The face region (samples version 2 and later): the Genesis 9 head and the Mouth figure. Empty for older samples. */
	TArray<FDTHFaceFigure> Face;

	bool HasFace() const { return !Face.IsEmpty(); }

	bool Load(const FString& File, FString& OutError);

	/**
	 * Calls Visit for every expression in order, with its face displacement: per face figure, how far each region vertex
	 * moved from rest (Daz space, cm; empty when the samples have no face shape). Visit returns false to stop.
	 */
	bool ReadExpressions(TFunctionRef<bool(FDTHDazExpression&& Expression, TArray<TArray<FVector3f>>&& Displacement)> Visit, FString& OutError) const;

private:
	FString File;
	int32 Version = 0;
	/** Face displacements are integers in this unit (cm). */
	double GeoUnit = 1e-4;
	struct FChunk { FString Samples; FString Geometry; };
	TArray<FChunk> Chunks;
};
