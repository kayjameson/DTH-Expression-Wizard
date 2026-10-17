#pragma once

#include "CoreMinimal.h"

class UDTHExpressionLibrary;
class USkeletalMesh;

/** A line of a build's report. */
struct FDTHBuildMessage
{
	enum class ESeverity : uint8 { Info, Warning, Error };
	ESeverity Severity = ESeverity::Info;
	FString Text;
};

/** Builds (or rebuilds in place) the expression library asset from a Daz samples file. */
struct FDTHLibraryBuild
{
	/**
	 * Fits every sampled expression to DTH's face rig as measured on Character (measured now if there is no valid
	 * measurement yet) and writes the library asset at AssetPath (created if missing, saved).
	 * Returns the library, or null on error; Messages gets the report, including every expression's missing shapes.
	 */
	static UDTHExpressionLibrary* Build(const FString& SamplesFile, const FString& AssetPath, USkeletalMesh* Character, bool bStripGaze,
		TArray<FDTHBuildMessage>& Messages);
};
