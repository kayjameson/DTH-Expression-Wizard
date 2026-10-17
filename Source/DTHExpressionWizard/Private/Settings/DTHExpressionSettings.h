#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "DTHExpressionSettings.generated.h"

class UDTHExpressionLibrary;

/**
 * Where the Expression Wizard reads and writes. Shown under Project Settings > Plugins > DTH Expression Wizard and on
 * the wizard's Settings page. Stored per user, per project.
 */
UCLASS(config = EditorPerProjectUserSettings, BlueprintType, meta = (DisplayName = "DTH Expression Wizard"))
class UDTHExpressionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDTHExpressionSettings();

	/** DTH_ExpressionSamples.json, written by the Daz Studio sampler script (Settings page: "Daz script"). */
	UPROPERTY(config, EditAnywhere, Category = "Library", meta = (DisplayName = "Daz Samples File", FilePathFilter = "json"))
	FFilePath SamplesFile;

	/** The expression library asset the Build page writes and the Mixer reads. */
	UPROPERTY(config, EditAnywhere, Category = "Library", meta = (DisplayName = "Expression Library"))
	FString LibraryAsset;

	/** Leave the eye direction to the animator: expressions do not turn the eyes (lids still follow the expression). */
	UPROPERTY(config, EditAnywhere, Category = "Library", meta = (DisplayName = "Strip Gaze"))
	bool bStripGaze = true;

	/** New keyed sequences are created in this folder. */
	UPROPERTY(config, EditAnywhere, Category = "Mixer", meta = (ContentDir, DisplayName = "Sequence Folder"))
	FDirectoryPath SequenceFolder;

	/**
	 * A DTH Genesis 9 character (skeletal mesh with DTH's post-process blueprint): the library build measures DTH's face
	 * rig on it, and the Mixer previews it and keys sequences for its skeleton.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Library", meta = (DisplayName = "Character", AllowedClasses = "/Script/Engine.SkeletalMesh"))
	FSoftObjectPath PreviewMesh;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
