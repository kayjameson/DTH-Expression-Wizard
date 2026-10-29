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
#include "DTHRigKeying.h"
#include "Misc/ScopedSlowTask.h"
#include "Solve/DTHExpressionSolver.h"
#include "Solve/DTHFaceEvaluator.h"
#include "Solve/DTHFaceTarget.h"
#include "Solve/DTHShapeFit.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

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

	// With the face shape sampled: fit each expression's shape on the character, every step measured on its real rig.
	TArray<FDTHShapeFit::FResult> Shapes;
	TArray<FName> ShapeCurves;
	if (Samples.HasFace())
	{
		FDTHFaceEvaluator Evaluator;
		FDTHFaceTarget Face;
		FDTHShapeFit Shape;
		TArray<FString> Report;
		if (!Evaluator.Init(Character, Error) || !Face.Build(Samples, Evaluator, Report, Error)
			|| !Shape.Init(Evaluator, BoardOf(Character), Rig, Report, Error))
		{
			Say(Messages, ESeverity::Error, Error);
			return nullptr;
		}
		for (const FString& Line : Report) Say(Messages, ESeverity::Info, Line);
		FDTHShapeFit::FOptions Options;
		if (bStripGaze)
		{
			for (const FName& C : Shape.Curves())
			{
				const FString Name = C.ToString();
				if (Name.Contains(TEXT("_eyeLook")) && !Name.Contains(TEXT("Parallel"))) Options.Fixed.Add(C);
			}
		}
		ShapeCurves = Shape.Curves();
		Shapes.SetNum(Samples.Expressions.Num());
		FScopedSlowTask Task(Samples.Expressions.Num(), LOCTEXT("FitShapes", "Fitting expression shapes on the character"));
		Task.MakeDialog(true);
		TArray<FVector3f> Target;
		for (int32 I = 0; I < Samples.Expressions.Num(); ++I)
		{
			Task.EnterProgressFrame(1.0f, FText::FromString(Samples.Expressions[I].Label));
			if (Task.ShouldCancel()) break;
			if (Samples.Expressions[I].bLoaded && Face.Target(Samples, I, Target)) Shapes[I] = Shape.Fit(Target, Options);
		}
	}

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
		// neutral: no FACS change, or (with the face shape sampled) no face point moving more than 0.2 mm
		const bool bShapeSampled = Shapes.IsValidIndex(I) && !Shapes[I].Values.IsEmpty();
		if (Fit.TargetSize < 1e-3 || (bShapeSampled && Shapes[I].Largest < 0.02))
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
		const bool bShape = Shapes.IsValidIndex(I) && !Shapes[I].Values.IsEmpty();
		if (bShape)
		{
			// the measured shape fit: the face as Unreal shows it against the Daz face, whole face without the eyes
			for (int32 C = 0; C < ShapeCurves.Num(); ++C)
			{
				if (Shapes[I].Values[C] > 0.0f)
				{
					E.Curves.Add(ShapeCurves[C]);
					E.Values.Add(Shapes[I].Values[C]);
					Largest = FMath::Max(Largest, static_cast<double>(Shapes[I].Values[C]));
					Used.Add(ShapeCurves[C]);
				}
			}
			E.Match = static_cast<float>(FMath::Clamp(Shapes[I].Match, 0.0, 1.0));
			Say(Messages, ESeverity::Info, FString::Printf(TEXT("%s / %s: shape %.1f%% (model %.1f%%), %d evaluations, %d steps, %.2f s."),
				*Source.Pack, *Source.Label, 100.0 * Shapes[I].Match, 100.0 * Shapes[I].ModelMatch, Shapes[I].Evaluations, Shapes[I].Iterations, Shapes[I].Seconds));
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

#undef LOCTEXT_NAMESPACE
