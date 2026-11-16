#include "Library/DTHLibraryBuild.h"

#include "DTHFaceFitter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "DTHExpressionLog.h"
#include "Library/DTHExpressionLibrary.h"
#include "Library/DTHExpressionSamples.h"
#include "DTHFaceRig.h"
#include "DTHFaceRigProbe.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "DTHRigKeying.h"
#include "Misc/ScopedSlowTask.h"
#include "Solve/DTHExpressionSolver.h"
#include "DTHFaceEvaluator.h"
#include "DTHFaceTarget.h"
#include "DTHShapeFit.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DTHLibraryBuild"

namespace
{
	/** The character's face board (its default animating rig's face controls), as the shape fit uses it. */
	TArray<FDTHShapeFit::FBoardCurve> BoardOf(const USkeletalMesh* Character)
	{
		TArray<FDTHShapeFit::FBoardCurve> Out;
		for (const DTHRigKeying::FBoardSlot& S : DTHRigKeying::BoardMap(DTHRigKeying::DefaultRig(Character)))
		{
			Out.Add({S.Curve, S.Control, S.Axis, S.Sign});
		}
		return Out;
	}

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
		UDTHExpressionLibrary* Library = NewObject<UDTHExpressionLibrary>(Package, *AssetName, RF_Public | RF_Standalone);
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
	Say(Messages, ESeverity::Info, FString::Printf(TEXT("Reading %d expressions sampled in Daz Studio %s on %s."),
		Samples.NumExpressions, *Samples.DazVersion, *Samples.SampledAt));

	const FDTHExpressionSolver Solver(Rig, Samples, bStripGaze);
	if (!Solver.IsValid(&Error))
	{
		Say(Messages, ESeverity::Error, Error);
		return nullptr;
	}

	// With the face shape sampled: fit each expression's shape on the character, every step measured on its real rig.
	FDTHFaceEvaluator Evaluator;
	FDTHFaceTarget Face;
	FDTHShapeFit Shape;
	FDTHShapeFit::FOptions Options;
	TArray<FName> ShapeCurves;
	if (Samples.HasFace())
	{
		TArray<FString> Report;
		if (!Evaluator.Init(Character, Error) || !Face.Build(Samples.Face, Evaluator, Report, Error)
			|| !Shape.Init(Evaluator, BoardOf(Character), Rig, Report, Error, Evaluator.FaceMorphCurves(), FDTHFaceFitter::TurnableBones(*Character)))
		{
			Say(Messages, ESeverity::Error, Error);
			return nullptr;
		}
		for (const FString& Line : Report) Say(Messages, ESeverity::Info, Line);
		if (bStripGaze)
		{
			for (const FName& C : Shape.Curves())
			{
				const FString Name = C.ToString();
				if ((Name.Contains(TEXT("_eyeLook")) && !Name.Contains(TEXT("Parallel"))) || Name.StartsWith(TEXT("bone|eye_"))) Options.Fixed.Add(C);
			}
		}
		ShapeCurves = Shape.Curves();
	}

	// The library is built apart and only copied into the asset when every expression is done: a cancelled or failed build
	// leaves the asset as it was. The samples are read one file at a time, and fitted 100 at a time.
	const TStrongObjectPtr<UDTHExpressionLibrary> Built(NewObject<UDTHExpressionLibrary>(GetTransientPackage()));
	TSet<FName> Used;
	int32 Partial = 0;
	bool bCancelled = false;
	FScopedSlowTask Task(FMath::Max(Samples.NumExpressions, 1), Samples.HasFace()
		? LOCTEXT("FitShapes", "Fitting expression shapes on the character") : LOCTEXT("FitExpressions", "Fitting expressions"));
	Task.MakeDialog(true);

	TArray<FDTHDazExpression> Batch;
	TArray<TArray<TArray<FVector3f>>> BatchShapes;
	auto FitBatch = [&]()
	{
		TArray<FDTHExpressionFit> Fits;
		Fits.SetNum(Batch.Num());
		ParallelFor(Batch.Num(), [&](int32 I)
		{
			if (Batch[I].bLoaded)
			{
				Fits[I] = Solver.Fit(Batch[I].State);
			}
		});
		TArray<FVector3f> Target;
		for (int32 I = 0; I < Batch.Num() && !bCancelled; ++I)
		{
			const FDTHDazExpression& Source = Batch[I];
			Task.EnterProgressFrame(1.0f, FText::FromString(Source.Label));
			if (Task.ShouldCancel())
			{
				bCancelled = true;
				break;
			}
			if (!Source.bLoaded)
			{
				const FString Line = FString::Printf(TEXT("%s / %s: skipped, %s."), *Source.Pack, *Source.Label, Source.Reason.IsEmpty() ? TEXT("Daz did not apply it") : *Source.Reason);
				Built->Skipped.Add(Line);
				Say(Messages, ESeverity::Warning, Line);
				continue;
			}
			FDTHShapeFit::FResult ShapeFit;
			if (Samples.HasFace() && Face.Target(BatchShapes[I], Target)) ShapeFit = Shape.Fit(Target, Options);
			const bool bShape = !ShapeFit.Values.IsEmpty();
			const FDTHExpressionFit& Fit = Fits[I];
			// neutral: no FACS change, or (with the face shape sampled) no face point moving more than 0.2 mm
			if (Fit.TargetSize < 1e-3 || (bShape && ShapeFit.Largest < 0.02))
			{
				const FString Line = FString::Printf(TEXT("%s / %s: skipped, it leaves the Genesis 9 face neutral (a reset preset, or one made for another generation)."), *Source.Pack, *Source.Label);
				Built->Skipped.Add(Line);
				Say(Messages, ESeverity::Warning, Line);
				continue;
			}

			FDTHExpression& E = Built->Expressions.AddDefaulted_GetRef();
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
			if (bShape)
			{
				// the measured shape fit: the face as Unreal shows it against the Daz face, whole face without the eyes
				for (int32 C = 0; C < ShapeCurves.Num(); ++C)
				{
					if (ShapeFit.Values[C] != 0.0f)
					{
						E.Curves.Add(ShapeCurves[C]);
						E.Values.Add(ShapeFit.Values[C]);
						// the dial's limit comes from the board's range; morph curves have none
						if (!Shape.IsMorph(C) && !Shape.IsBone(C)) Largest = FMath::Max(Largest, static_cast<double>(ShapeFit.Values[C]));
						Used.Add(ShapeCurves[C]);
					}
				}
				E.Match = static_cast<float>(FMath::Clamp(ShapeFit.Match, 0.0, 1.0));
				Say(Messages, ESeverity::Info, FString::Printf(TEXT("%s / %s: shape %.1f%% (model %.1f%%), %d evaluations, %d steps, %.2f s."),
					*Source.Pack, *Source.Label, 100.0 * ShapeFit.Match, 100.0 * ShapeFit.ModelMatch, ShapeFit.Evaluations, ShapeFit.Iterations, ShapeFit.Seconds));
			}
			else
			{
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
				E.Match = static_cast<float>(FMath::Clamp(1.0 - Fit.Residual / Fit.TargetSize, 0.0, 1.0));
			}
			E.MaxStrength = Largest > 0.0 ? static_cast<float>(1.0 / Largest) : 1.0f;
			E.Missing = Fit.Missing;
			for (const FString& Sculpt : Source.State.Sculpted)
			{
				E.Missing.Add(FString::Printf(TEXT("%s: the preset's own sculpted shape, not a Genesis 9 FACS shape; DTH's face poses can only approximate it."), *Sculpt));
			}
			if (!E.Missing.IsEmpty())
			{
				++Partial;
				Say(Messages, ESeverity::Info, FString::Printf(TEXT("%s / %s: %d%% reproduced. Missing: %s"),
					*E.Pack, *E.Label, FMath::RoundToInt(E.Match * 100.0f), *FString::Join(E.Missing, TEXT(" "))));
			}
		}
		Batch.Reset();
		BatchShapes.Reset();
	};

	constexpr int32 BatchSize = 100;
	const bool bRead = Samples.ReadExpressions([&](FDTHDazExpression&& Expression, TArray<TArray<FVector3f>>&& Displacement)
	{
		Batch.Add(MoveTemp(Expression));
		BatchShapes.Add(MoveTemp(Displacement));
		if (Batch.Num() == BatchSize) FitBatch();
		return !bCancelled;
	}, Error);
	if (bRead && !bCancelled) FitBatch();
	if (!bRead)
	{
		Say(Messages, ESeverity::Error, Error);
		return nullptr;
	}
	if (bCancelled)
	{
		Say(Messages, ESeverity::Warning, TEXT("Cancelled: the library was left as it was."));
		return nullptr;
	}

	UDTHExpressionLibrary* Library = FindOrCreateLibrary(AssetPath, Error);
	if (!Library)
	{
		Say(Messages, ESeverity::Error, Error);
		return nullptr;
	}
	// Not through the undo buffer: a large library would be copied into it whole.
	Library->Expressions = MoveTemp(Built->Expressions);
	Library->Skipped = MoveTemp(Built->Skipped);
	Library->SamplesFile = SamplesFile;
	Library->SampledAt = Samples.SampledAt;
	Library->bGazeStripped = bStripGaze;
	Library->AllCurves = Used.Array();
	Library->AllCurves.Sort(FNameLexicalLess());

	UPackage* Package = Library->GetPackage();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	// a failed save is reported, not fatal (the default error device stops the editor)
	SaveArgs.Error = GWarn;
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	// an existing library was loaded from this file, and its loader still holds it open: let go of it before writing
	ResetLoadersForSave(Package, *File);
	if (!UPackage::SavePackage(Package, Library, *File, SaveArgs))
	{
		// The fits took a long time: they are not thrown away when the library's file cannot be written (another program
		// holding it, a read-only file). They go into a new library beside it.
		const FString RecoveredName = Package->GetName() + TEXT("_Recovered_") + FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		UPackage* RecoveredPackage = CreatePackage(*RecoveredName);
		UDTHExpressionLibrary* Recovered = NewObject<UDTHExpressionLibrary>(RecoveredPackage, *FPackageName::GetLongPackageAssetName(RecoveredName), RF_Public | RF_Standalone);
		Recovered->Expressions = Library->Expressions;
		Recovered->Skipped = Library->Skipped;
		Recovered->SamplesFile = Library->SamplesFile;
		Recovered->SampledAt = Library->SampledAt;
		Recovered->bGazeStripped = Library->bGazeStripped;
		Recovered->AllCurves = Library->AllCurves;
		FAssetRegistryModule::AssetCreated(Recovered);
		RecoveredPackage->MarkPackageDirty();
		const FString RecoveredFile = FPackageName::LongPackageNameToFilename(RecoveredName, FPackageName::GetAssetPackageExtension());
		if (UPackage::SavePackage(RecoveredPackage, Recovered, *RecoveredFile, SaveArgs))
		{
			Say(Messages, ESeverity::Error, FString::Printf(TEXT("Could not save %s (is the file open in another program?). The built library was saved as %s instead: use that one, or rename it."),
				*Package->GetName(), *RecoveredName));
			Library = Recovered;
			Package = RecoveredPackage;
		}
		else
		{
			Say(Messages, ESeverity::Error, FString::Printf(TEXT("Could not save %s, nor %s beside it."), *Package->GetName(), *RecoveredName));
		}
	}

	Say(Messages, ESeverity::Info, FString::Printf(TEXT("Library %s: %d expressions (%d with shapes DTH's face cannot make, listed above), %d skipped, %d face-board curves."),
		*Package->GetName(), Library->Expressions.Num(), Partial, Library->Skipped.Num(), Library->AllCurves.Num()));
	return Library;
}

#undef LOCTEXT_NAMESPACE
