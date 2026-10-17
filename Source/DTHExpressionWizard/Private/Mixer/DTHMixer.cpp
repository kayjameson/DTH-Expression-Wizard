#include "Mixer/DTHMixer.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
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
		V.Value = FMath::Clamp(V.Value, 0.0f, 1.0f);
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
	for (const TPair<FName, float>& V : Values)
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

#undef LOCTEXT_NAMESPACE
