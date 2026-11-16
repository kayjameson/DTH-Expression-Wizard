#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class ULevelSequence;
class USkeletalMesh;
class USkeleton;
class UDTHExpressionLibrary;

/** One expression in a mix and how strongly it is dialled in (1 = as authored in Daz; above 1 overdrives). */
struct FDTHMixEntry
{
	int32 Expression = INDEX_NONE;
	float Strength = 1.0f;
};

/**
 * The Mixer's work, without any UI: mixing face values, and writing them into face sequences. A face sequence holds the
 * face curves (CTRL_expressions_*, DTH's fac* morph curves), the face bones turned past DTH's face rig when an
 * expression needs them (jaw, tongue: DTHFaceBones) and the skeleton's root at its reference pose, and is additive
 * (local space, against the reference pose), so it moves no bones and layers over a body animation; DTH's
 * post-process blueprint reads its curves exactly as it reads the face board.
 */
struct FDTHMixer
{
	/** A face-board curve (CTRL_expressions_*), as opposed to one of DTH's morph curves (fac*). */
	static bool IsBoardCurve(FName Curve) { return Curve.ToString().StartsWith(TEXT("CTRL_")); }

	/** Every curve of the library (AllCurves) with its mixed value: sum of strength * value, board curves clamped to [0, 1]. */
	static void Mix(const UDTHExpressionLibrary& Library, TConstArrayView<FDTHMixEntry> Entries, TMap<FName, float>& OutValues);

	/** A new face sequence asset in Folder (saved). Null with OutError if it cannot be made. */
	static UAnimSequence* CreateSequence(const FString& Folder, const FString& Name, USkeleton* Skeleton, FString& OutError);

	/**
	 * Keys Values at Frame (30 fps frames of the sequence), lengthening the sequence if needed. Every curve in Values
	 * is keyed, so the key fully sets the face; other keys are kept. Marks the sequence dirty (not saved).
	 */
	static bool Key(UAnimSequence* Sequence, int32 Frame, const TMap<FName, float>& Values, FString& OutError);

	/**
	 * Keys the mix at Frame onto the character's control rig track in Sequence (binding BindingName; the character's
	 * default rig is added if it has none). The face board's controls carry what the board can show; the layered FK
	 * Control Rig track adds the difference for every curve, so the face is exactly the Mixer's. Only face controls and
	 * the layer's curves are keyed, so body keys from the Pose Wizard on the same frames stay.
	 */
	static bool KeyOntoRig(const UDTHExpressionLibrary& Library, TConstArrayView<FDTHMixEntry> Entries, USkeletalMesh& Character,
		ULevelSequence* Sequence, const FString& BindingName, int32 Frame, bool bStepped, TArray<FString>& OutLines);

	/** The face sequence's frame rate. */
	static FFrameRate FrameRate() { return FFrameRate(30, 1); }

private:
	static void InitializeFaceSequence(UAnimSequence* Sequence, USkeleton* Skeleton, bool bTransact);
	static void SetRootKeys(UAnimSequence* Sequence, bool bTransact);
};
