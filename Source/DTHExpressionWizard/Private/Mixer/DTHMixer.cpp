#include "Mixer/DTHMixer.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DTHRigKeying.h"
#include "DTHFaceBones.h"
#include "Library/DTHExpressionLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "DTHExpressionMixer"

void FDTHMixer::Mix(const UDTHExpressionLibrary& Library, TConstArrayView<FDTHMixEntry> Entries, TMap<FName, float>& OutValues)
{
	OutValues.Reset();
	for (const FName& Curve : Library.AllCurves)
	{
		OutValues.Add(Curve, 0.0f);
	}
	for (const FDTHMixEntry& Entry : Entries)
	{
		if (!Library.Expressions.IsValidIndex(Entry.Expression))
		{
			continue;
		}
		const FDTHExpression& E = Library.Expressions[Entry.Expression];
		for (int32 I = 0; I < E.Curves.Num(); ++I)
		{
			OutValues.FindOrAdd(E.Curves[I]) += Entry.Strength * E.Values[I];
		}
	}
	for (TPair<FName, float>& V : OutValues)
	{
		// face-board curves have the board's 0..1 range; DTH's morph curves (fac*) carry what is past it, and have none
		if (IsBoardCurve(V.Key)) V.Value = FMath::Clamp(V.Value, 0.0f, 1.0f);
	}
}

void FDTHMixer::SetRootKeys(UAnimSequence* Sequence, bool bTransact)
{
	const FReferenceSkeleton& Ref = Sequence->GetSkeleton()->GetReferenceSkeleton();
	if (Ref.GetNum() == 0)
	{
		return;
	}
	const FName Root = Ref.GetBoneName(0);
	const FTransform Pose = Ref.GetRefBonePose()[0];
	const int32 Keys = Sequence->GetDataModel()->GetNumberOfKeys();
	IAnimationDataController& Controller = Sequence->GetController();
	if (!Sequence->GetDataModel()->IsValidBoneTrackName(Root))
	{
		Controller.AddBoneCurve(Root, bTransact);
	}
	TArray<FVector> Positions; Positions.Init(Pose.GetTranslation(), Keys);
	TArray<FQuat> Rotations; Rotations.Init(Pose.GetRotation(), Keys);
	TArray<FVector> Scales; Scales.Init(Pose.GetScale3D(), Keys);
	Controller.SetBoneTrackKeys(Root, Positions, Rotations, Scales, bTransact);
}

void FDTHMixer::InitializeFaceSequence(UAnimSequence* Sequence, USkeleton* Skeleton, bool bTransact)
{
	Sequence->SetSkeleton(Skeleton);
	IAnimationDataController& Controller = Sequence->GetController();
	Controller.InitializeModel();
	Controller.OpenBracket(LOCTEXT("CreateFace", "Create face sequence"), bTransact);
	Controller.SetFrameRate(FrameRate(), bTransact);
	Controller.SetNumberOfFrames(FFrameNumber(1), bTransact);
	// A sequence is only evaluated with bone data, so it carries the root at its reference pose (no motion).
	SetRootKeys(Sequence, bTransact);
	Controller.CloseBracket(bTransact);
	Controller.NotifyPopulated();
	Sequence->AdditiveAnimType = AAT_LocalSpaceBase;
	Sequence->RefPoseType = ABPT_RefPose;
}

UAnimSequence* FDTHMixer::CreateSequence(const FString& Folder, const FString& Name, USkeleton* Skeleton, FString& OutError)
{
	if (!Skeleton)
	{
		OutError = TEXT("No skeleton: pick the character first.");
		return nullptr;
	}
	const FString PackageName = Folder / Name;
	if (!FPackageName::IsValidLongPackageName(PackageName, false) || FPackageName::DoesPackageExist(PackageName))
	{
		OutError = FString::Printf(TEXT("Cannot create %s (not a valid content path, or it already exists)."), *PackageName);
		return nullptr;
	}
	UPackage* Package = CreatePackage(*PackageName);
	UAnimSequence* Sequence = NewObject<UAnimSequence>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
	InitializeFaceSequence(Sequence, Skeleton, false);
	FAssetRegistryModule::AssetCreated(Sequence);
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Sequence, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs);
	return Sequence;
}

bool FDTHMixer::Key(UAnimSequence* Sequence, int32 Frame, const TMap<FName, float>& Values, FString& OutError)
{
	if (!Sequence || !Sequence->GetSkeleton())
	{
		OutError = TEXT("No sequence to key into.");
		return false;
	}
	if (Frame < 0)
	{
		OutError = TEXT("The frame must be 0 or later.");
		return false;
	}
	IAnimationDataController& Controller = Sequence->GetController();
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	Controller.OpenBracket(LOCTEXT("KeyMix", "Key expression mix"));
	if (Model->GetNumberOfFrames() < Frame)
	{
		Controller.SetNumberOfFrames(FFrameNumber(Frame));
		SetRootKeys(Sequence, true);
	}
	const float Time = static_cast<float>(Model->GetFrameRate().AsSeconds(FFrameNumber(Frame)));
	TMap<FName, float> Curves;
	TMap<FName, FQuat> Turns;
	DTHFaceBones::Split(Values, Curves, Turns);
	// Face bones past DTH's face rig: bone tracks (the sequence is additive against the reference pose, so a track is the
	// reference pose turned), keyed at this frame; the track's other frames keep their keys, a new track starts at rest.
	const FReferenceSkeleton& Ref = Sequence->GetSkeleton()->GetReferenceSkeleton();
	const int32 Keys = Model->GetNumberOfKeys();
	for (const TPair<FName, FTransform>& B : DTHFaceBones::Local(Ref, Turns))
	{
		TArray<FTransform> Track;
		if (Model->IsValidBoneTrackName(B.Key)) Model->GetBoneTrackTransforms(B.Key, Track);
		else Controller.AddBoneCurve(B.Key);
		const FTransform& RefPose = Ref.GetRefBonePose()[Ref.FindBoneIndex(B.Key)];
		while (Track.Num() < Keys) Track.Add(RefPose);
		Track.SetNum(Keys);
		if (Track.IsValidIndex(Frame)) Track[Frame] = B.Value;
		TArray<FVector> Positions; TArray<FQuat> Rotations; TArray<FVector> Scales;
		for (const FTransform& T : Track) { Positions.Add(T.GetTranslation()); Rotations.Add(T.GetRotation()); Scales.Add(T.GetScale3D()); }
		Controller.SetBoneTrackKeys(B.Key, Positions, Rotations, Scales);
	}
	for (const TPair<FName, float>& V : Curves)
	{
		const FAnimationCurveIdentifier Id(V.Key, ERawCurveTrackTypes::RCT_Float);
		if (!Model->FindFloatCurve(Id))
		{
			Controller.AddCurve(Id);
		}
		FRichCurveKey Key(Time, V.Value);
		Key.InterpMode = RCIM_Linear;
		Controller.SetCurveKey(Id, Key);
	}
	Controller.CloseBracket();
	Sequence->MarkPackageDirty();
	return true;
}

bool FDTHMixer::KeyOntoRig(const UDTHExpressionLibrary& Library, TConstArrayView<FDTHMixEntry> Entries, USkeletalMesh& Character,
	ULevelSequence* Sequence, const FString& BindingName, int32 Frame, bool bStepped, TArray<FString>& OutLines)
{
	FString Error;
	DTHRigKeying::FTarget Target;
	if (!DTHRigKeying::Resolve(Sequence, BindingName, &Character, Target, Error))
	{
		OutLines.Add(TEXT("ERROR: ") + Error);
		return false;
	}
	TMap<FName, float> Values, Curves;
	Mix(Library, Entries, Values);
	TMap<FName, FQuat> Turns;
	DTHFaceBones::Split(Values, Curves, Turns);
	// face bones past DTH's face rig go on the FK layer as the bones' local transforms
	const TMap<FName, FTransform> Bones = DTHFaceBones::Local(Character.GetRefSkeleton(), Turns);
	TMap<FName, FRigControlValue> Controls, Layer;
	int32 Corrected = 0;
	if (!DTHRigKeying::FaceBoard(Target.RigClass, Curves, Controls, Layer, Corrected, Error)
		|| !DTHRigKeying::Key(Target, Target.Track, Frame, Controls, {}, bStepped, Error)
		|| !DTHRigKeying::Key(Target, Target.Layer, Frame, Layer, Bones, bStepped, Error))
	{
		OutLines.Add(FString::Printf(TEXT("ERROR: frame %d: %s"), Frame, *Error));
		return false;
	}
	OutLines.Add(FString::Printf(TEXT("Frame %d: keyed %d face controls of %s, and %d curves and %d face bones on the FK layer; the layer corrects %d curves the board cannot show."),
		Frame, Controls.Num(), *Target.RigClass->GetName(), Layer.Num(), Bones.Num(), Corrected));
	return true;
}

#undef LOCTEXT_NAMESPACE
