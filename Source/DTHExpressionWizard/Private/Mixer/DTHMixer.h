#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeleton;
class UDTHExpressionLibrary;

/** One expression in a mix and how strongly it is dialled in (1 = as authored in Daz; above 1 overdrives). */
struct FDTHMixEntry
{
	int32 Expression = INDEX_NONE;
	float Strength = 1.0f;
};

/**
 * The Mixer's work, without any UI: mixing face-board values, and writing them into curve-only animation sequences.
 * A face sequence holds only CTRL_expressions_* curves and the skeleton's root at its reference pose, and is additive
 * (local space, against the reference pose), so it moves no bones and layers over a body animation; DTH's
 * post-process blueprint reads its curves exactly as it reads the face board.
 */
struct FDTHMixer
{
	/** Every curve of the library (AllCurves) with its mixed value: sum of strength * value, clamped to [0, 1]. */
	static void Mix(const UDTHExpressionLibrary& Library, TConstArrayView<FDTHMixEntry> Entries, TMap<FName, float>& OutValues);

	/** A new face sequence asset in Folder (saved). Null with OutError if it cannot be made. */
	static UAnimSequence* CreateSequence(const FString& Folder, const FString& Name, USkeleton* Skeleton, FString& OutError);

	/**
	 * Keys Values at Frame (30 fps frames of the sequence), lengthening the sequence if needed. Every curve in Values
	 * is keyed, so the key fully sets the face; other keys are kept. Marks the sequence dirty (not saved).
	 */
	static bool Key(UAnimSequence* Sequence, int32 Frame, const TMap<FName, float>& Values, FString& OutError);

	/** The face sequence's frame rate. */
	static FFrameRate FrameRate() { return FFrameRate(30, 1); }

private:
	static void InitializeFaceSequence(UAnimSequence* Sequence, USkeleton* Skeleton, bool bTransact);
	static void SetRootKeys(UAnimSequence* Sequence, bool bTransact);
};
