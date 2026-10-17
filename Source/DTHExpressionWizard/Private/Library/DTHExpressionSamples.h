#pragma once

#include "CoreMinimal.h"

/** One sampled Daz state: final morph values ("Figure|morph") and head bone channels ("Figure|bone|XRotate"). */
struct FDTHDazState
{
	TMap<FString, double> Morphs;
	TMap<FString, double> Bones;
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

/** DTH_ExpressionSamples.json, written by Resources/Daz/DTH_SampleExpressions.dsa. */
struct FDTHExpressionSamples
{
	FString SampledAt;
	FString DazVersion;
	FDTHDazState Rest;
	/** DTH's face ROM: frame -> state (frame 0 and the Fac frames). */
	TMap<int32, FDTHDazState> RomFrames;
	TArray<FDTHDazExpression> Expressions;

	bool Load(const FString& File, FString& OutError);
};
