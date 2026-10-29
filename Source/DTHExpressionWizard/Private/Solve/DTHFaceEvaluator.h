#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"

class UMorphTarget;
class USkeletalMesh;
class USkeletalMeshComponent;
class UDTHCurveDriverInstance;

/**
 * The character in an off-screen preview world, played through its own post-process blueprint (DTH's face rig and pose
 * assets) with a set of face-board curves, and the resulting positions of chosen render vertices: the face as Unreal
 * shows it, measured exactly. Only the chosen vertices are skinned (morph targets, then linear skinning, as Unreal's CPU
 * skinning does it), so one evaluation costs about as much as the animation update.
 */
class FDTHFaceEvaluator
{
public:
	FDTHFaceEvaluator();
	~FDTHFaceEvaluator();

	bool Init(USkeletalMesh* Character, FString& OutError);

	/** LOD 0 render vertices. */
	int32 NumVertices() const;
	/** The material slot of a render vertex's section. */
	FName MaterialSlot(int32 Vertex) const;

	/** The bone with the largest skin weight on a chosen vertex (index into GetVertices()). */
	FName DominantBone(int32 Index) const;

	/** The render vertices Evaluate measures, in this order. */
	void SetVertices(TArray<int32> Vertices);
	const TArray<int32>& GetVertices() const { return Vertices; }

	/** Component-space positions (cm) of the chosen vertices with these face-board curves (every other curve at 0). */
	void Evaluate(const TMap<FName, float>& Curves, TArray<FVector3f>& OutPositions);

	/** The same positions read from Unreal's own CPU skinning of the whole mesh (slow; for checking Evaluate). */
	void EvaluateWithEngine(const TMap<FName, float>& Curves, TArray<FVector3f>& OutPositions);

	int32 Evaluations = 0;

	/**
	 * Skin with dual quaternions, as DTH's mesh deformer (DG_DTH_DQS) renders the character; false: linear, as Unreal's
	 * default skinning and its CPU skinning do (EvaluateWithEngine compares against that).
	 */
	bool bDualQuaternion = true;

private:
	void Play(const TMap<FName, float>& Curves);

	FPreviewScene Scene;
	USkeletalMeshComponent* Component = nullptr;
	UDTHCurveDriverInstance* Driver = nullptr;
	USkeletalMesh* Mesh = nullptr;

	TArray<int32> Vertices;
	/** Per chosen vertex: reference position and its bone influences (mesh bone, weight). */
	TArray<FVector3f> RefPositions;
	TArray<int32> InfluenceStart;
	TArray<int32> InfluenceBone;
	TArray<float> InfluenceWeight;
	/** Per morph target: its deltas on the chosen vertices (index into Vertices, position delta). */
	TMap<const UMorphTarget*, TArray<TPair<int32, FVector3f>>> MorphDeltas;

	TArray<FMatrix44f> RefToLocals;
	TArray<FQuat4f> BoneReal;
	TArray<FQuat4f> BoneDual;
	TArray<FVector3f> Morphed;
};
