// Automation tests for the expression shape fit. Run them from the Session Frontend or through the automation toolset.
// Their inputs are in <Project>/Saved/DTHExpressionWizard/ShapeFitTest.json:
//   samples              the Daz samples file (DTH_ExpressionSamples.json, version 2: with face shapes)
//   character            the skeletal mesh to fit on, e.g. /Game/DTH_Characters/G9Base/SKM_G9Base_Body
//   labels               the expressions to fit (labels as in the samples)
//   characterRest        optional: the character's own rest face from Daz (a face dump, see below), when the character
//                        is not the figure the samples were taken on
//   characterExpressions optional: label -> the same expression's face dump on the character in Daz, for the check
//                        against the character's own Daz face
//   threshold            per-expression pass mark (default 0.94)
//   report               where to write the per-expression report
// A face dump is "<figure name> <vertex count>" followed by one "x y z" line per vertex, for each figure.
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Library/DTHExpressionLibrary.h"
#include "Library/DTHExpressionSamples.h"
#include "Library/DTHLibraryBuild.h"
#include "Library/DTHFaceRig.h"
#include "Library/DTHFaceRigProbe.h"
#include "DTHRigKeying.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Solve/DTHExpressionSolver.h"
#include "Solve/DTHFaceEvaluator.h"
#include "Solve/DTHFaceTarget.h"
#include "Solve/DTHShapeFit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> LoadConfig(FAutomationTestBase& Test)
	{
		const FString File = FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/ShapeFitTest.json");
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			Test.AddError(TEXT("No test inputs: ") + File);
			return nullptr;
		}
		return Root;
	}

	/** Face dump: figure name -> positions (Daz space, cm). */
	bool LoadDump(const FString& File, TMap<FString, TArray<FVector3f>>& Out)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *File)) return false;
		TArray<FVector3f>* Current = nullptr;
		for (const FString& Line : Lines)
		{
			TArray<FString> P;
			Line.ParseIntoArrayWS(P);
			if (P.Num() == 2) Current = &Out.Add(P[0]);
			else if (P.Num() == 3 && Current) Current->Add(FVector3f(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])));
		}
		return !Out.IsEmpty();
	}

	TArray<TArray<FVector3f>> ByFigure(const FDTHExpressionSamples& Samples, const TMap<FString, TArray<FVector3f>>& Dump)
	{
		TArray<TArray<FVector3f>> Out;
		for (const FDTHFaceFigure& F : Samples.Face) Out.Add(Dump.FindRef(F.Name));
		return Out;
	}

	double Match(TConstArrayView<FVector3f> Got, TConstArrayView<FVector3f> Want)
	{
		double E = 0.0, W = 0.0;
		for (int32 I = 0; I < Want.Num(); ++I) { E += FVector3f::DistSquared(Got[I], Want[I]); W += Want[I].SizeSquared(); }
		return W > 0.0 ? 1.0 - FMath::Sqrt(E / W) : 1.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHFaceEvaluatorTest, "DTH.ExpressionWizard.FaceEvaluator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The evaluator's own skinning gives the same face as Unreal's CPU skinning of the whole mesh. */
bool FDTHFaceEvaluatorTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Config = LoadConfig(*this);
	if (!Config) return false;
	USkeletalMesh* Character = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	FDTHFaceEvaluator Evaluator;
	FString Error;
	if (!TestTrue(TEXT("evaluator starts"), Character && Evaluator.Init(Character, Error))) { AddError(Error); return false; }
	TArray<int32> Vertices;
	for (int32 V = 0; V < Evaluator.NumVertices(); V += 7) Vertices.Add(V);
	Evaluator.SetVertices(Vertices);
	const FString P = TEXT("CTRL_expressions_");
	const TArray<TMap<FName, float>> Sets = {
		{},
		{{FName(*(P + TEXT("jawOpen"))), 1.0f}, {FName(*(P + TEXT("tongueOut"))), 1.0f}},
		{{FName(*(P + TEXT("browDownL"))), 0.7f}, {FName(*(P + TEXT("eyeFaceScrunchL"))), 0.5f}, {FName(*(P + TEXT("mouthCornerPullR"))), 0.8f}},
		{{FName(*(P + TEXT("eyeBlinkR"))), 1.0f}, {FName(*(P + TEXT("noseWrinkleL"))), 0.4f}},
	};
	double Worst = 0.0, DQSvsLinear = 0.0;
	for (const TMap<FName, float>& S : Sets)
	{
		TArray<FVector3f> Mine, Engine, DQS;
		Evaluator.bDualQuaternion = false;
		Evaluator.Evaluate(S, Mine);
		Evaluator.EvaluateWithEngine(S, Engine);
		Evaluator.bDualQuaternion = true;
		const double T0 = FPlatformTime::Seconds();
		Evaluator.Evaluate(S, DQS);
		const double Ms = 1000.0 * (FPlatformTime::Seconds() - T0);
		for (int32 I = 0; I < Mine.Num(); ++I)
		{
			Worst = FMath::Max(Worst, static_cast<double>(FVector3f::Dist(Mine[I], Engine[I])));
			DQSvsLinear = FMath::Max(DQSvsLinear, static_cast<double>(FVector3f::Dist(Mine[I], DQS[I])));
		}
		AddInfo(FString::Printf(TEXT("%d curves: evaluated %d vertices in %.2f ms (dual quaternion)"), S.Num(), Mine.Num(), Ms));
	}
	AddInfo(FString::Printf(TEXT("Dual-quaternion skinning differs from linear by up to %.4f cm"), DQSvsLinear));
	AddInfo(FString::Printf(TEXT("Largest difference from Unreal's CPU skinning: %.5f cm"), Worst));
	return TestTrue(TEXT("same face as Unreal's CPU skinning (within 0.001 cm)"), Worst < 0.001);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHShapeFitTest, "DTH.ExpressionWizard.ShapeFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Fits the listed expressions on the character and checks every one against the pass mark. */
bool FDTHShapeFitTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Config = LoadConfig(*this);
	if (!Config) return false;
	FString Error;
	FDTHExpressionSamples Samples;
	if (!Samples.Load(Config->GetStringField(TEXT("samples")), Error) || !Samples.HasFace()) { AddError(Error.IsEmpty() ? TEXT("samples have no face shapes") : Error); return false; }
	USkeletalMesh* Character = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	FDTHFaceRig Rig;
	bool bMeasured = false;
	if (!Character || !FDTHFaceRigProbe::GetOrMeasure(Character, Rig, bMeasured, Error)) { AddError(TEXT("character / face rig: ") + Error); return false; }

	TMap<FString, TArray<FVector3f>> RestDump;
	TArray<TArray<FVector3f>> CharacterRest;
	FString RestFile;
	if (Config->TryGetStringField(TEXT("characterRest"), RestFile))
	{
		if (!LoadDump(RestFile, RestDump)) { AddError(TEXT("cannot read ") + RestFile); return false; }
		CharacterRest = ByFigure(Samples, RestDump);
	}

	FDTHFaceEvaluator Evaluator;
	FDTHFaceTarget Face;
	FDTHShapeFit Shape;
	TArray<FString> Report;
	TArray<FDTHShapeFit::FBoardCurve> Board;
	for (const DTHRigKeying::FBoardSlot& S : DTHRigKeying::BoardMap(DTHRigKeying::DefaultRig(Character))) Board.Add({S.Curve, S.Control, S.Axis, S.Sign});
	const double T0 = FPlatformTime::Seconds();
	if (!Evaluator.Init(Character, Error) || !Face.Build(Samples, Evaluator, Report, Error, CharacterRest.IsEmpty() ? nullptr : &CharacterRest)
		|| !Shape.Init(Evaluator, Board, Rig, Report, Error))
	{
		AddError(Error);
		return false;
	}
	Report.Add(FString::Printf(TEXT("Set-up (pairing, board measurement) %.1f s, %d evaluations."), FPlatformTime::Seconds() - T0, Evaluator.Evaluations));

	double Threshold = 0.94;
	Config->TryGetNumberField(TEXT("threshold"), Threshold);
	const TSharedPtr<FJsonObject>* Own = nullptr;
	Config->TryGetObjectField(TEXT("characterExpressions"), Own);
	TMap<FString, int32> ByLabel;
	for (int32 I = 0; I < Samples.Expressions.Num(); ++I) ByLabel.Add(Samples.Expressions[I].Label, I);

	int32 Passed = 0, Total = 0;
	double Slowest = 0.0;
	const FDTHExpressionSolver LeafSolver(Rig, Samples, false);
	for (const TSharedPtr<FJsonValue>& L : Config->GetArrayField(TEXT("labels")))
	{
		const FString Label = L->AsString();
		const int32* I = ByLabel.Find(Label);
		TArray<FVector3f> Target;
		if (!I || !Face.Target(Samples, *I, Target)) { Report.Add(TEXT("MISSING ") + Label); AddError(TEXT("not in the samples: ") + Label); continue; }
		FDTHShapeFit::FOptions Options;
		const FDTHShapeFit::FResult R = Shape.Fit(Target, Options);
		Slowest = FMath::Max(Slowest, R.Seconds);
		// the pass mark is on the character's own Daz face when it is given, else on the sampled face
		double Score = R.Match;
		FString Extra;
		FString OwnFile;
		if (Own && (*Own)->TryGetStringField(Label, OwnFile) && !RestDump.IsEmpty())
		{
			TMap<FString, TArray<FVector3f>> Dump;
			if (LoadDump(OwnFile, Dump))
			{
				TArray<TArray<FVector3f>> D = ByFigure(Samples, Dump);
				const TArray<TArray<FVector3f>> Rest = ByFigure(Samples, RestDump);
				for (int32 F = 0; F < D.Num(); ++F)
					for (int32 V = 0; V < D[F].Num() && V < Rest[F].Num(); ++V) D[F][V] -= Rest[F][V];
				TArray<FVector3f> OwnTarget, Got;
				if (Face.TargetFromMeshes(Samples, D, OwnTarget))
				{
					Shape.Displacement(R.Values, Got);
					const double OwnMatch = Match(Got, OwnTarget);
					Extra = FString::Printf(TEXT(", against the character's own Daz face %.1f%%"), 100.0 * OwnMatch);
					Score = OwnMatch;
				}
			}
		}
		// where the remaining error is: by the bone that dominates each face point
		{
			TArray<FVector3f> Got;
			Shape.Displacement(R.Values, Got);
			TMap<FName, TPair<double, double>> ByBone;   // error^2, target^2
			double Total2 = 0.0;
			for (int32 P = 0; P < Got.Num(); ++P)
			{
				TPair<double, double>& B = ByBone.FindOrAdd(Evaluator.DominantBone(P));
				const double E2 = FVector3f::DistSquared(Got[P], Target[P]);
				B.Key += E2; B.Value += Target[P].SizeSquared(); Total2 += E2;
			}
			ByBone.ValueSort([](const TPair<double, double>& A, const TPair<double, double>& B) { return A.Key > B.Key; });
			int32 Shown = 0;
			Extra += TEXT(" | error by bone:");
			for (const TPair<FName, TPair<double, double>>& B : ByBone)
			{
				if (Shown++ == 5) break;
				Extra += FString::Printf(TEXT(" %s %.0f%% (match %.0f%%)"), *B.Key.ToString(), 100.0 * B.Value.Key / FMath::Max(Total2, 1e-12),
					100.0 * (1.0 - FMath::Sqrt(B.Value.Key / FMath::Max(B.Value.Value, 1e-12))));
			}
		}
		++Total;
		const bool bPass = Score >= Threshold;
		if (!bPass)
		{
			// what limits it: the board's one-side rule, its 0..1 range, or DTH's poses themselves
			FDTHShapeFit::FOptions Relaxed;
			Relaxed.TimeBudget = 8.0;
			Relaxed.bOneSide = false;
			const double NoSide = Shape.Fit(Target, Relaxed).Match;
			Relaxed.MaxValue = 2.0;
			const double NoLimits = Shape.Fit(Target, Relaxed).Match;
			Relaxed.bOneSide = true;
			Relaxed.MaxValue = 1.0;
			const double Longer = Shape.Fit(Target, Relaxed).Match;
			// other starting points: the slider-space fit of the old library, and nothing at all
			const FDTHExpressionFit Leaf = LeafSolver.Fit(Samples.Expressions[*I].State);
			Relaxed.Start.Init(0.0, Shape.Curves().Num());
			for (int32 K = 0; K < Shape.Curves().Num(); ++K)
			{
				const int32 RC = Rig.FindCurve(Shape.Curves()[K]);
				if (RC != INDEX_NONE && Leaf.Values.IsValidIndex(RC)) Relaxed.Start[K] = Leaf.Values[RC];
			}
			const double FromLeaf = Shape.Fit(Target, Relaxed).Match;
			Relaxed.Start.Init(0.0, Shape.Curves().Num());
			const double FromZero = Shape.Fit(Target, Relaxed).Match;
			Extra += FString::Printf(TEXT(" | limits: 8 s on the board %.1f%%, both slider ends %.1f%%, both ends and values to 2 %.1f%%; from the slider-space fit %.1f%%, from zero %.1f%%"),
				100.0 * Longer, 100.0 * NoSide, 100.0 * NoLimits, 100.0 * FromLeaf, 100.0 * FromZero);
		}
		Passed += bPass ? 1 : 0;
		Report.Add(FString::Printf(TEXT("%s %5.1f%%  %s (sampled face %.1f%%, model %.1f%%, %d evaluations, %d steps, %.2f s of which model %.2f s%s)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), 100.0 * Score, *Label, 100.0 * R.Match, 100.0 * R.ModelMatch, R.Evaluations, R.Iterations, R.Seconds, R.ModelSeconds, *Extra));
	}
	Report.Add(FString::Printf(TEXT("%d of %d at or above %.0f%%. Slowest fit %.2f s."), Passed, Total, 100.0 * Threshold, Slowest));
	FString ReportFile;
	if (!Config->TryGetStringField(TEXT("report"), ReportFile)) ReportFile = FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/ShapeFitReport.txt");
	FFileHelper::SaveStringArrayToFile(Report, *ReportFile);
	for (const FString& Line : Report) AddInfo(Line);
	TestTrue(TEXT("every expression at or above the pass mark"), Passed == Total && Total > 0);
	TestTrue(TEXT("every fit within 2 s"), Slowest <= 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHBuildLibraryTest, "DTH.ExpressionWizard.BuildLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Builds the whole expression library from the test inputs' samples on their character (libraryAsset), as the Build page does. */
bool FDTHBuildLibraryTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Config = LoadConfig(*this);
	if (!Config) return false;
	FString Asset;
	if (!Config->TryGetStringField(TEXT("libraryAsset"), Asset)) { AddError(TEXT("no libraryAsset in the test inputs")); return false; }
	USkeletalMesh* Character = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	TArray<FDTHBuildMessage> Messages;
	const double T0 = FPlatformTime::Seconds();
	const UDTHExpressionLibrary* Library = FDTHLibraryBuild::Build(Config->GetStringField(TEXT("samples")), Asset, Character, false, Messages);
	TArray<FString> Lines;
	for (const FDTHBuildMessage& M : Messages) Lines.Add(M.Text);
	Lines.Add(FString::Printf(TEXT("Built in %.0f s."), FPlatformTime::Seconds() - T0));
	// every expression is held by the face board as it is (no layer corrections), as a face-board key needs
	int32 NeedLayer = 0, Pass = 0;
	if (Library)
	{
		UClass* RigClass = DTHRigKeying::DefaultRig(Character);
		for (const FDTHExpression& E : Library->Expressions)
		{
			TMap<FName, float> Curves;
			for (int32 K = 0; K < E.Curves.Num(); ++K) Curves.Add(E.Curves[K], E.Values[K]);
			TMap<FName, FRigControlValue> Controls, Layer;
			int32 Corrected = 0;
			FString Error;
			if (!DTHRigKeying::FaceBoard(RigClass, Curves, Controls, Layer, Corrected, Error) || Corrected > 0)
			{
				++NeedLayer;
				if (NeedLayer <= 10) Lines.Add(FString::Printf(TEXT("NOT ON THE BOARD: %s (%d curves need a layer correction) %s"), *E.Label, Corrected, *Error));
			}
			Pass += E.Match >= 0.94f ? 1 : 0;
		}
		Lines.Add(FString::Printf(TEXT("%d of %d expressions at or above 94%%; %d need a layer correction (0 expected)."), Pass, Library->Expressions.Num(), NeedLayer));
	}
	TestEqual(TEXT("every expression held by the face board"), NeedLayer, 0);
	FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/BuildLibraryReport.txt")));
	return TestNotNull(TEXT("library built"), Library);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHLibraryOnBoardTest, "DTH.ExpressionWizard.LibraryOnBoard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Every expression of the built library (libraryAsset) is held by the character's face board as it is. */
bool FDTHLibraryOnBoardTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Config = LoadConfig(*this);
	if (!Config) return false;
	const UDTHExpressionLibrary* Library = LoadObject<UDTHExpressionLibrary>(nullptr, *Config->GetStringField(TEXT("libraryAsset")));
	USkeletalMesh* Character = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	if (!Library || !Character) { AddError(TEXT("no library or character")); return false; }
	UClass* RigClass = DTHRigKeying::DefaultRig(Character);
	TArray<FString> Lines;
	TMap<FName, int32> ByCurve;
	int32 NeedLayer = 0;
	for (const FDTHExpression& E : Library->Expressions)
	{
		TMap<FName, float> Curves;
		for (int32 K = 0; K < E.Curves.Num(); ++K) Curves.Add(E.Curves[K], E.Values[K]);
		TMap<FName, FRigControlValue> Controls, Layer;
		int32 Corrected = 0;
		FString Error;
		DTHRigKeying::FaceBoard(RigClass, Curves, Controls, Layer, Corrected, Error);
		if (Corrected == 0) continue;
		++NeedLayer;
		FString Detail;
		for (const TPair<FName, FRigControlValue>& L : Layer)
		{
			const float D = L.Value.Get<float>();
			if (FMath::Abs(D) > 1e-4f)
			{
				Detail += FString::Printf(TEXT(" %s %+.4f"), *L.Key.ToString(), D);
				ByCurve.FindOrAdd(L.Key)++;
			}
		}
		if (NeedLayer <= 15) Lines.Add(FString::Printf(TEXT("%s:%s"), *E.Label, *Detail));
	}
	ByCurve.ValueSort([](int32 A, int32 B) { return A > B; });
	for (const TPair<FName, int32>& C : ByCurve) Lines.Add(FString::Printf(TEXT("  %s in %d expressions"), *C.Key.ToString(), C.Value));
	Lines.Add(FString::Printf(TEXT("%d of %d expressions need a layer correction."), NeedLayer, Library->Expressions.Num()));
	FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/LibraryOnBoardReport.txt")));
	return TestEqual(TEXT("every expression held by the face board"), NeedLayer, 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseFloorTest, "DTH.ExpressionWizard.PoseFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * How exactly each of DTH's face poses reproduces its Daz ROM frame on the character: a curve that drives one pose alone
 * at full weight, against that ROM frame's face dump (romFrames: folder of rom<frame>_geo.txt face dumps of the sampled
 * figure, with rom0_geo.txt the rest). This is the best any fit can do on that pose.
 */
bool FDTHPoseFloorTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Config = LoadConfig(*this);
	if (!Config) return false;
	FString Error, Folder;
	FDTHExpressionSamples Samples;
	if (!Samples.Load(Config->GetStringField(TEXT("samples")), Error) || !Config->TryGetStringField(TEXT("romFrames"), Folder)) { AddError(TEXT("samples / romFrames: ") + Error); return false; }
	USkeletalMesh* Character = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	FDTHFaceRig Rig;
	bool bMeasured = false;
	if (!Character || !FDTHFaceRigProbe::GetOrMeasure(Character, Rig, bMeasured, Error)) { AddError(Error); return false; }
	FDTHFaceEvaluator Evaluator;
	FDTHFaceTarget Face;
	TArray<FString> Report;
	if (!Evaluator.Init(Character, Error) || !Face.Build(Samples, Evaluator, Report, Error)) { AddError(Error); return false; }
	TMap<FString, TArray<FVector3f>> Rest;
	if (!LoadDump(Folder / TEXT("rom0_geo.txt"), Rest)) { AddError(TEXT("no rom0_geo.txt in ") + Folder); return false; }
	const TArray<TArray<FVector3f>> RestByFigure = ByFigure(Samples, Rest);

	// poses driven by exactly one curve at weight 1, and that curve drives nothing else
	TMap<int32, TArray<TPair<int32, double>>> ByCurve;
	for (const FDTHFaceRig::FLink& L : Rig.Linear) ByCurve.FindOrAdd(L.Curve).Add({L.Pose, L.Weight});
	TSet<int32> InPairs;
	for (const FDTHFaceRig::FPair& P : Rig.Pairs) { InPairs.Add(P.A); InPairs.Add(P.B); }
	TArray<FVector3f> Base, Got, Want;
	Evaluator.Evaluate({}, Base);
	TArray<TPair<double, FString>> Rows;
	int32 FirstFrame = 105;
	Config->TryGetNumberField(TEXT("firstFaceFrame"), FirstFrame);
	for (const TPair<int32, TArray<TPair<int32, double>>>& C : ByCurve)
	{
		if (C.Value.Num() != 1 || FMath::Abs(C.Value[0].Value - 1.0) > 1e-3 || InPairs.Contains(C.Key) || Rig.Exponent[C.Key] != 1) continue;
		const int32 Frame = FirstFrame + C.Value[0].Key;
		TMap<FString, TArray<FVector3f>> Dump;
		if (!LoadDump(Folder / FString::Printf(TEXT("rom%d_geo.txt"), Frame), Dump)) continue;
		TArray<TArray<FVector3f>> D = ByFigure(Samples, Dump);
		for (int32 F = 0; F < D.Num(); ++F)
			for (int32 V = 0; V < D[F].Num() && V < RestByFigure[F].Num(); ++V) D[F][V] -= RestByFigure[F][V];
		if (!Face.TargetFromMeshes(Samples, D, Want)) continue;
		Evaluator.Evaluate({{Rig.Curves[C.Key], 1.0f}}, Got);
		for (int32 P = 0; P < Got.Num(); ++P) Got[P] -= Base[P];
		double Largest = 0.0;
		for (const FVector3f& W : Want) Largest = FMath::Max(Largest, static_cast<double>(W.Size()));
		if (Largest < 0.05) continue;
		const double M = Match(Got, Want);
		// where it differs, and how: by dominant bone, with the share of the Daz motion the Unreal pose makes along it
		TMap<FName, TArray<double>> ByBone;   // error^2, want^2, got.want, got^2
		double Total2 = 0.0;
		for (int32 P = 0; P < Got.Num(); ++P)
		{
			TArray<double>& B = ByBone.FindOrAdd(Evaluator.DominantBone(P));
			if (B.IsEmpty()) B.Init(0.0, 4);
			const double E2 = FVector3f::DistSquared(Got[P], Want[P]);
			B[0] += E2; B[1] += Want[P].SizeSquared(); B[2] += FVector3f::DotProduct(Got[P], Want[P]); B[3] += Got[P].SizeSquared(); Total2 += E2;
		}
		ByBone.ValueSort([](const TArray<double>& A, const TArray<double>& B) { return A[0] > B[0]; });
		FString Detail;
		int32 Shown = 0;
		for (const TPair<FName, TArray<double>>& B : ByBone)
		{
			if (Shown++ == 3) break;
			Detail += FString::Printf(TEXT(" %s %.0f%% (Unreal/Daz size %.2f, along %.2f)"), *B.Key.ToString(), 100.0 * B.Value[0] / FMath::Max(Total2, 1e-12),
				FMath::Sqrt(B.Value[3] / FMath::Max(B.Value[1], 1e-12)), B.Value[2] / FMath::Max(FMath::Sqrt(B.Value[1] * B.Value[3]), 1e-12));
		}
		Rows.Add({M, FString::Printf(TEXT("%5.1f%%  %s (frame %d, curve %s, Daz moves up to %.2f cm) |%s"), 100.0 * M, *Rig.Poses[C.Value[0].Key].ToString(), Frame,
			*Rig.Curves[C.Key].ToString(), Largest, *Detail)});
	}
	Rows.Sort([](const TPair<double, FString>& A, const TPair<double, FString>& B) { return A.Key < B.Key; });
	double Sum = 0.0;
	for (const TPair<double, FString>& R : Rows) { Report.Add(R.Value); Sum += R.Key; }
	Report.Add(FString::Printf(TEXT("%d poses, mean %.1f%%."), Rows.Num(), Rows.IsEmpty() ? 0.0 : 100.0 * Sum / Rows.Num()));
	FFileHelper::SaveStringArrayToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/PoseFloorReport.txt")));
	return true;
}

#endif
