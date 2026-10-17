#pragma once

#include "Engine/DataAsset.h"
#include "DTHExpressionLibrary.generated.h"

/** One expression: the face-board values that reproduce it through DTH's face rig, and where it came from. */
USTRUCT(BlueprintType)
struct FDTHExpression
{
	GENERATED_BODY()

	/** Content-library folder of the Daz preset, e.g. "People/Genesis 9/Expressions/<Vendor>/<Pack>". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	FString Pack;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	FString Label;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	FString SourceFile;

	/** The preset's thumbnail on disk, if it has one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	FString IconFile;

	/** Face-board curves (CTRL_expressions_*) and their values at full strength. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	TArray<FName> Curves;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	TArray<float> Values;

	/** Highest dial setting that keeps every curve within its range (1 / largest value): overdrive limit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	float MaxStrength = 1.0f;

	/** How much of the Daz expression the fit reproduces, 0..1 (1 - residual / size, in the fitted leaves). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	float Match = 1.0f;

	/** What could not be reproduced, one line each with the reason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Expression")
	TArray<FString> Missing;
};

/** Expressions built from a Daz sampling run, for the Mixer. Written by the wizard's Build page. */
UCLASS(BlueprintType)
class UDTHExpressionLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Library")
	TArray<FDTHExpression> Expressions;

	/** Every face-board curve any expression uses; keying writes all of them, so a key fully sets the face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Library")
	TArray<FName> AllCurves;

	/** Presets the sampler found but could not use, with the reason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Library")
	TArray<FString> Skipped;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Library")
	FString SamplesFile;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Library")
	FString SampledAt;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Library")
	bool bGazeStripped = true;
};
