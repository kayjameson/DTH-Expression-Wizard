#include "DTHExpressionScripting.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Docking/TabManager.h"
#include "DTHExpressionWizardModule.h"
#include "Library/DTHExpressionLibrary.h"
#include "Library/DTHFaceRig.h"
#include "Library/DTHFaceRigProbe.h"
#include "Library/DTHLibraryBuild.h"
#include "Misc/PackageName.h"
#include "Mixer/DTHMixer.h"
#include "Settings/DTHExpressionSettings.h"
#include "UI/SDTHExpressionWizard.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	UDTHExpressionLibrary* LoadLibrary(const FString& Path, TArray<FString>& Lines)
	{
		const FString Use = Path.IsEmpty() ? GetDefault<UDTHExpressionSettings>()->LibraryAsset : Path;
		UDTHExpressionLibrary* Library = LoadObject<UDTHExpressionLibrary>(nullptr, *Use, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Library)
		{
			Lines.Add(FString::Printf(TEXT("ERROR: No expression library at %s (build it first)."), *Use));
		}
		return Library;
	}

	TArray<FDTHMixEntry> Entries(const TArray<int32>& Expressions, const TArray<float>& Strengths)
	{
		TArray<FDTHMixEntry> Out;
		for (int32 I = 0; I < Expressions.Num(); ++I)
		{
			Out.Add({Expressions[I], Strengths.IsValidIndex(I) ? Strengths[I] : 1.0f});
		}
		return Out;
	}
}

FString UDTHExpressionScripting::OpenWizard(const FString& Page)
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDTHExpressionWizardModule::WizardTabName);
	const TSharedPtr<SDTHExpressionWizard> Wizard = SDTHExpressionWizard::Get();
	if (!Wizard.IsValid())
	{
		return TEXT("ERROR: The wizard tab did not open.");
	}
	Wizard->Reload();
	if (!Page.IsEmpty() && !Wizard->ShowPage(Page))
	{
		return FString::Printf(TEXT("ERROR: No page \"%s\" (Settings, Build or Mixer)."), *Page);
	}
	return TEXT("Wizard open.");
}

FString UDTHExpressionScripting::SetMix(const FString& SkeletalMesh, const TArray<int32>& Expressions, const TArray<float>& Strengths)
{
	const TSharedPtr<SDTHExpressionWizard> Wizard = SDTHExpressionWizard::Get();
	if (!Wizard.IsValid())
	{
		return TEXT("ERROR: The wizard is not open (OpenWizard first).");
	}
	if (!SkeletalMesh.IsEmpty())
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMesh, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Mesh)
		{
			return FString::Printf(TEXT("ERROR: No skeletal mesh at %s."), *SkeletalMesh);
		}
		Wizard->SelectCharacter(Mesh);
	}
	TArray<TPair<int32, float>> Mix;
	for (int32 I = 0; I < Expressions.Num(); ++I)
	{
		Mix.Emplace(Expressions[I], Strengths.IsValidIndex(I) ? Strengths[I] : 1.0f);
	}
	return Wizard->ReplaceMix(Mix);
}

FString UDTHExpressionScripting::CapturePreview(const FString& File, const FString& View)
{
	const TSharedPtr<SDTHExpressionWizard> Wizard = SDTHExpressionWizard::Get();
	return Wizard.IsValid() ? Wizard->CapturePreview(File, View) : TEXT("ERROR: The wizard is not open (OpenWizard first).");
}

TArray<FString> UDTHExpressionScripting::BuildLibrary(const FString& SamplesFile, const FString& LibraryAsset, bool bStripGaze, const FString& Character)
{
	const UDTHExpressionSettings* Settings = GetDefault<UDTHExpressionSettings>();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(Character.IsEmpty() ? Settings->PreviewMesh.TryLoad() : FSoftObjectPath(Character).TryLoad());
	TArray<FDTHBuildMessage> Messages;
	FDTHLibraryBuild::Build(SamplesFile.IsEmpty() ? Settings->SamplesFile.FilePath : SamplesFile,
		LibraryAsset.IsEmpty() ? Settings->LibraryAsset : LibraryAsset, Mesh, bStripGaze, Messages);
	TArray<FString> Lines;
	for (const FDTHBuildMessage& M : Messages)
	{
		const TCHAR* Prefix = M.Severity == FDTHBuildMessage::ESeverity::Error ? TEXT("ERROR: ")
			: M.Severity == FDTHBuildMessage::ESeverity::Warning ? TEXT("WARNING: ") : TEXT("");
		Lines.Add(Prefix + M.Text);
	}
	return Lines;
}

TArray<FString> UDTHExpressionScripting::MeasureFaceRig(const FString& Character, bool bForce)
{
	TArray<FString> Lines;
	const UDTHExpressionSettings* Settings = GetDefault<UDTHExpressionSettings>();
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(Character.IsEmpty() ? Settings->PreviewMesh.TryLoad() : FSoftObjectPath(Character).TryLoad());
	FDTHFaceRig Rig;
	FString Error;
	bool bMeasured = true;
	const bool bOk = bForce ? FDTHFaceRigProbe::Measure(Mesh, Rig, Error) && Rig.Save(FDTHFaceRigProbe::RigFile(Mesh))
		: FDTHFaceRigProbe::GetOrMeasure(Mesh, Rig, bMeasured, Error);
	if (!bOk)
	{
		Lines.Add(TEXT("ERROR: ") + (Error.IsEmpty() ? FString(TEXT("Could not save the measurement.")) : Error));
		return Lines;
	}
	Lines.Add(FString::Printf(TEXT("%s %s: %d curves, %d poses (%d not driven), %d links, %d pair terms."), bMeasured ? TEXT("Measured") : TEXT("Kept"),
		*FDTHFaceRigProbe::RigFile(Mesh), Rig.Curves.Num(), Rig.Poses.Num(), Rig.Undriven.Num(), Rig.Linear.Num(), Rig.Pairs.Num()));
	for (const FString& Note : Rig.Notes) Lines.Add(TEXT("WARNING: ") + Note);
	return Lines;
}

TArray<FString> UDTHExpressionScripting::DebugEvaluateFaceRig(const FString& Character, const TArray<FName>& Names, const TArray<float>& Values, const TArray<FName>& Read)
{
	TArray<FString> Lines;
	TMap<FName, float> Curves;
	for (int32 I = 0; I < Names.Num(); ++I) Curves.Add(Names[I], Values.IsValidIndex(I) ? Values[I] : 1.0f);
	TArray<float> Out;
	FString Error;
	if (!FDTHFaceRigProbe::DebugEvaluate(Cast<USkeletalMesh>(FSoftObjectPath(Character).TryLoad()), Curves, Read, Out, Error))
	{
		Lines.Add(TEXT("ERROR: ") + Error);
		return Lines;
	}
	for (int32 I = 0; I < Read.Num(); ++I) Lines.Add(FString::Printf(TEXT("%s=%.5f"), *Read[I].ToString(), Out[I]));
	return Lines;
}

TArray<FString> UDTHExpressionScripting::ListExpressions(const FString& LibraryAsset, const FString& Filter)
{
	TArray<FString> Lines;
	const UDTHExpressionLibrary* Library = LoadLibrary(LibraryAsset, Lines);
	if (!Library)
	{
		return Lines;
	}
	for (int32 I = 0; I < Library->Expressions.Num(); ++I)
	{
		const FDTHExpression& E = Library->Expressions[I];
		if (!Filter.IsEmpty() && !E.Label.Contains(Filter) && !E.Pack.Contains(Filter))
		{
			continue;
		}
		Lines.Add(FString::Printf(TEXT("%d | %s | %s | %d%% | %.2f | %d | %d"), I, *E.Pack, *E.Label,
			FMath::RoundToInt(E.Match * 100.0f), E.MaxStrength, E.Curves.Num(), E.Missing.Num()));
	}
	return Lines;
}

TArray<FString> UDTHExpressionScripting::MixValues(const TArray<int32>& Expressions, const TArray<float>& Strengths, const FString& LibraryAsset)
{
	TArray<FString> Lines;
	const UDTHExpressionLibrary* Library = LoadLibrary(LibraryAsset, Lines);
	if (!Library)
	{
		return Lines;
	}
	TMap<FName, float> Values;
	FDTHMixer::Mix(*Library, Entries(Expressions, Strengths), Values);
	for (const TPair<FName, float>& V : Values)
	{
		if (V.Value > 0.0f) Lines.Add(FString::Printf(TEXT("%s=%.5f"), *V.Key.ToString(), V.Value));
	}
	return Lines;
}

TArray<FString> UDTHExpressionScripting::KeyMix(const FString& SequencePath, const FString& SkeletalMesh, int32 Frame,
	const TArray<int32>& Expressions, const TArray<float>& Strengths, const FString& LibraryAsset)
{
	TArray<FString> Lines;
	const UDTHExpressionLibrary* Library = LoadLibrary(LibraryAsset, Lines);
	if (!Library)
	{
		return Lines;
	}
	FString PackageName = SequencePath.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(SequencePath) : SequencePath;
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName)), nullptr, LOAD_NoWarn | LOAD_Quiet);
	FString Error;
	if (!Sequence)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMesh, nullptr, LOAD_NoWarn | LOAD_Quiet);
		Sequence = FDTHMixer::CreateSequence(FPackageName::GetLongPackagePath(PackageName), FPackageName::GetLongPackageAssetName(PackageName),
			Mesh ? Mesh->GetSkeleton() : nullptr, Error);
		if (!Sequence)
		{
			Lines.Add(TEXT("ERROR: ") + Error);
			return Lines;
		}
		Lines.Add(FString::Printf(TEXT("Created %s."), *PackageName));
	}
	TMap<FName, float> Values;
	FDTHMixer::Mix(*Library, Entries(Expressions, Strengths), Values);
	if (!FDTHMixer::Key(Sequence, Frame, Values, Error))
	{
		Lines.Add(TEXT("ERROR: ") + Error);
		return Lines;
	}
	UPackage* Package = Sequence->GetPackage();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Sequence, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), SaveArgs);
	Lines.Add(FString::Printf(TEXT("Keyed %d curves at frame %d of %s."), Values.Num(), Frame, *PackageName));
	return Lines;
}
