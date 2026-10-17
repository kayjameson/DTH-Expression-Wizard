#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "DTHExpressionScripting.generated.h"

/**
 * The Expression Wizard's steps for editor scripting (Python: unreal.DTHExpressionScripting) and automation.
 * Each call does what the wizard's button does. Empty paths mean the ones in the wizard's settings.
 * Results are lines; errors start with "ERROR:", warnings with "WARNING:".
 */
UCLASS()
class UDTHExpressionScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Opens the wizard tab on Page (Settings, Build or Mixer; empty: as it was) and reloads its settings. */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static FString OpenWizard(const FString& Page = TEXT(""));

	/**
	 * Sets the open Mixer's character (a skeletal mesh path; empty keeps it) and replaces its mix with these expressions
	 * (by index) at these strengths, as picking them in the Mixer does.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static FString SetMix(const FString& SkeletalMesh, const TArray<int32>& Expressions, const TArray<float>& Strengths);

	/** Saves the open Mixer's preview, framed as View (front, threequarter, eyes or mouth), as a PNG. */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static FString CapturePreview(const FString& File, const FString& View = TEXT("front"));

	/**
	 * Builds the expression library from the Daz samples file, as the Build page does, measuring DTH's face rig on
	 * Character (a skeletal mesh path) first if needed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static TArray<FString> BuildLibrary(const FString& SamplesFile = TEXT(""), const FString& LibraryAsset = TEXT(""), bool bStripGaze = true,
		const FString& Character = TEXT(""));

	/**
	 * Measures DTH's face rig on Character (a skeletal mesh path; empty: the Settings character) and saves it to the
	 * project's Saved folder, as the library build does when it has no valid measurement. bForce measures even then.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static TArray<FString> MeasureFaceRig(const FString& Character = TEXT(""), bool bForce = false);

	/** Debug: plays Names at Values on Character and returns "name=value" for each of Read (see FDTHFaceRigProbe). */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static TArray<FString> DebugEvaluateFaceRig(const FString& Character, const TArray<FName>& Names, const TArray<float>& Values, const TArray<FName>& Read);

	/** One line per expression: index | pack | label | match % | max strength | curves | missing count. */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static TArray<FString> ListExpressions(const FString& LibraryAsset = TEXT(""), const FString& Filter = TEXT(""));

	/** The mixed face-board values ("curve=value", non-zero only) for expressions (by index) at the given strengths. */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static TArray<FString> MixValues(const TArray<int32>& Expressions, const TArray<float>& Strengths, const FString& LibraryAsset = TEXT(""));

	/**
	 * Keys a mix at Frame into the face sequence at SequencePath, creating it for the skeleton of SkeletalMesh if it does
	 * not exist, and saves it. As the Mixer's Key button.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Expression Wizard")
	static TArray<FString> KeyMix(const FString& SequencePath, const FString& SkeletalMesh, int32 Frame,
		const TArray<int32>& Expressions, const TArray<float>& Strengths, const FString& LibraryAsset = TEXT(""));
};
