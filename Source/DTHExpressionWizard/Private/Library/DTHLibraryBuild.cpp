#include "Library/DTHLibraryBuild.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "DTHExpressionLog.h"
#include "Library/DTHExpressionLibrary.h"
#include "Library/DTHExpressionSamples.h"
#include "Library/DTHFaceRig.h"
#include "Library/DTHFaceRigProbe.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Solve/DTHExpressionSolver.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	void Say(TArray<FDTHBuildMessage>& Messages, FDTHBuildMessage::ESeverity Severity, const FString& Text)
	{
		Messages.Add({Severity, Text});
		if (Severity == FDTHBuildMessage::ESeverity::Error)
		{
			UE_LOG(LogDTHExpression, Error, TEXT("%s"), *Text);
		}
		else if (Severity == FDTHBuildMessage::ESeverity::Warning)
		{
			UE_LOG(LogDTHExpression, Warning, TEXT("%s"), *Text);
		}
		else
		{
			UE_LOG(LogDTHExpression, Log, TEXT("%s"), *Text);
		}
	}

	UDTHExpressionLibrary* FindOrCreateLibrary(const FString& AssetPath, FString& OutError)
	{
		FString PackageName = AssetPath, AssetName;
		if (PackageName.Contains(TEXT(".")))
		{
			PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
		}
		if (!FPackageName::IsValidLongPackageName(PackageName, false))
		{
			OutError = FString::Printf(TEXT("\"%s\" is not a valid asset path (e.g. /Game/DTH_Expressions/DA_DTH_ExpressionLibrary)."), *AssetPath);
			return nullptr;
		}
		AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		if (UDTHExpressionLibrary* Existing = LoadObject<UDTHExpressionLibrary>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackageName);
		UDTHExpressionLibrary* Library = NewObject<UDTHExpressionLibrary>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Library);
		return Library;
	}
}

UDTHExpressionLibrary* FDTHLibraryBuild::Build(const FString& SamplesFile, const FString& AssetPath, USkeletalMesh* Character, bool bStripGaze,
	TArray<FDTHBuildMessage>& Messages)
{
	using ESeverity = FDTHBuildMessage::ESeverity;
	FString Error;

	FDTHFaceRig Rig;
	bool bMeasured = false;
	if (!FDTHFaceRigProbe::GetOrMeasure(Character, Rig, bMeasured, Error))
	{
		Say(Messages, ESeverity::Error, Error);
		return nullptr;
	}
	Say(Messages, ESeverity::Info, FString::Printf(TEXT("%s DTH's face rig on %s: %d face-board curves, %d face poses (%d not driven by any curve), %d curve pairs that act together."),
		bMeasured ? TEXT("Measured") : TEXT("Using the measurement of"), *Rig.Character, Rig.Curves.Num(), Rig.Poses.Num(), Rig.Undriven.Num(), Rig.Pairs.Num()));
	for (const FString& Note : Rig.Notes)
	{
		Say(Messages, ESeverity::Warning, TEXT("Face rig: ") + Note);
	}
	FDTHExpressionSamples Samples;
	if (!Samples.Load(SamplesFile, Error))
	{
		Say(Messages, ESeverity::Error, Error);
		return nullptr;
	}
	Say(Messages, ESeverity::Info, FString::Printf(TEXT("Read %d expressions sampled in Daz Studio %s on %s."),
		Samples.Expressions.Num(), *Samples.DazVersion, *Samples.SampledAt));

	const FDTHExpressionSolver Solver(Rig, Samples, bStripGaze);
	if (!Solver.IsValid(&Error))
	{
		Say(Messages, ESeverity::Error, Error);
		return nullptr;
	}

	TArray<FDTHExpressionFit> Fits;
	Fits.SetNum(Samples.Expressions.Num());
	ParallelFor(Samples.Expressions.Num(), [&](int32 I)
	{
		if (Samples.Expressions[I].bLoaded)
		{
			Fits[I] = Solver.Fit(Samples.Expressions[I].State);
		}
	});

	UDTHExpressionLibrary* Library = FindOrCreateLibrary(AssetPath, Error);
	if (!Library)
	{
		Say(Messages, ESeverity::Error, Error);
		return nullptr;
	}
	Library->Modify();
	Library->Expressions.Reset();
	Library->Skipped.Reset();
	Library->SamplesFile = SamplesFile;
	Library->SampledAt = Samples.SampledAt;
	Library->bGazeStripped = bStripGaze;

	TSet<FName> Used;
	int32 Partial = 0;
	for (int32 I = 0; I < Samples.Expressions.Num(); ++I)
	{
		const FDTHDazExpression& Source = Samples.Expressions[I];
		if (!Source.bLoaded)
		{
			const FString Line = FString::Printf(TEXT("%s / %s: skipped, %s."), *Source.Pack, *Source.Label, Source.Reason.IsEmpty() ? TEXT("Daz did not apply it") : *Source.Reason);
			Library->Skipped.Add(Line);
			Say(Messages, ESeverity::Warning, Line);
			continue;
		}
		const FDTHExpressionFit& Fit = Fits[I];
		if (Fit.TargetSize < 1e-3)
		{
			const FString Line = FString::Printf(TEXT("%s / %s: skipped, it leaves the Genesis 9 face neutral (a reset preset, or one made for another generation)."), *Source.Pack, *Source.Label);
			Library->Skipped.Add(Line);
			Say(Messages, ESeverity::Warning, Line);
			continue;
		}

		FDTHExpression& E = Library->Expressions.AddDefaulted_GetRef();
		E.Pack = Source.Pack;
		E.Label = Source.Label;
		E.SourceFile = Source.File;
		E.IconFile = Source.Icon;
		if (E.IconFile.IsEmpty() || !FPaths::FileExists(E.IconFile))
		{
			// Daz content ships thumbnails as "<preset>.png" or "<preset>.tip.png" as well as "<preset>.duf.png".
			const FString Base = FPaths::ChangeExtension(Source.File, TEXT(""));
			for (const FString& Candidate : {Source.File + TEXT(".png"), Base + TEXT(".png"), Base + TEXT(".tip.png")})
			{
				if (FPaths::FileExists(Candidate)) { E.IconFile = Candidate; break; }
			}
		}
		double Largest = 0.0;
		for (int32 C = 0; C < Fit.Values.Num(); ++C)
		{
			if (Fit.Values[C] > 0.0)
			{
				E.Curves.Add(Rig.Curves[C]);
				E.Values.Add(static_cast<float>(Fit.Values[C]));
				Largest = FMath::Max(Largest, Fit.Values[C]);
				Used.Add(Rig.Curves[C]);
			}
		}
		E.MaxStrength = Largest > 0.0 ? static_cast<float>(1.0 / Largest) : 1.0f;
		E.Match = static_cast<float>(FMath::Clamp(1.0 - Fit.Residual / Fit.TargetSize, 0.0, 1.0));
		E.Missing = Fit.Missing;
		if (!E.Missing.IsEmpty())
		{
			++Partial;
			Say(Messages, ESeverity::Info, FString::Printf(TEXT("%s / %s: %d%% reproduced. Missing: %s"),
				*E.Pack, *E.Label, FMath::RoundToInt(E.Match * 100.0f), *FString::Join(E.Missing, TEXT(" "))));
		}
	}
	Library->AllCurves = Used.Array();
	Library->AllCurves.Sort(FNameLexicalLess());

	UPackage* Package = Library->GetPackage();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Library, *File, SaveArgs))
	{
		Say(Messages, ESeverity::Error, FString::Printf(TEXT("Could not save %s."), *Package->GetName()));
	}

	Say(Messages, ESeverity::Info, FString::Printf(TEXT("Library %s: %d expressions (%d with shapes DTH's face cannot make, listed above), %d skipped, %d face-board curves."),
		*Package->GetName(), Library->Expressions.Num(), Partial, Library->Skipped.Num(), Library->AllCurves.Num()));
	return Library;
}
