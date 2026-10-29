#pragma once

#include "CoreMinimal.h"

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

/** A figure's part of the face the fit reproduces: its vertices (indices into the figure's base mesh), the facets made of
 * them (indices into Vertices; the 4th is -1 for a triangle) and their rest positions (Daz space, cm). */
struct FDTHFaceFigure
{
	FString Name;
	TArray<int32> Vertices;
	TArray<FIntVector4> Facets;
	TArray<FVector3f> Rest;
};

/** DTH_ExpressionSamples.json, written by Resources/Daz/DTH_SampleExpressions.dsa. */
struct FDTHExpressionSamples
{
	FString SampledAt;
	FString DazVersion;
	FDTHDazState Rest;
	/** DTH's face ROM: frame -> state (frame 0 and the Fac frames). */
	TMap<int32, FDTHDazState> RomFrames;
	TArray<FDTHDazExpression> Expressions;

	/** The face region (samples version 2): the Genesis 9 head and the Mouth figure. Empty for older samples. */
	TArray<FDTHFaceFigure> Face;
	/** Per expression, per face figure: how far each region vertex moved from rest (Daz space, cm). Empty when not sampled. */
	TArray<TArray<TArray<FVector3f>>> FaceDisplacement;

	bool HasFace() const { return !Face.IsEmpty(); }

	bool Load(const FString& File, FString& OutError);
};
