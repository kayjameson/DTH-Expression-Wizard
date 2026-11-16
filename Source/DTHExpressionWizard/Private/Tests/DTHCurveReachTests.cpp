// Diagnostic: which curves reach past the face board. Feeds sets of curves through the character's post-process blueprint
// and reports how far the face moves, as a multiple of a reference set's movement. Inputs from ShapeFitTest.json
// (character); writes Saved/DTHExpressionWizard/CurveReachReport.txt.
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "DTHFaceEvaluator.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHCurveReachTest, "DTH.ExpressionWizard.CurveReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHCurveReachTest::RunTest(const FString& Parameters)
{
	const FString ConfigFile = FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/ShapeFitTest.json");
	FString Text;
	TSharedPtr<FJsonObject> Config;
	if (!FFileHelper::LoadFileToString(Text, *ConfigFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config) { AddError(TEXT("no config")); return false; }
	USkeletalMesh* Character = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	FDTHFaceEvaluator Evaluator;
	FString Error;
	if (!Character || !Evaluator.Init(Character, Error)) { AddError(Error); return false; }
	TArray<int32> All;
	for (int32 V = 0; V < Evaluator.NumVertices(); ++V) All.Add(V);
	Evaluator.SetVertices(All);
	TArray<FVector3f> Rest;
	Evaluator.Evaluate({}, Rest);

	using FSet = TMap<FName, float>;
	auto C = [](const TCHAR* N) { return FName(FString(TEXT("CTRL_expressions_")) + N); };
	auto N = [](const TCHAR* S) { return FName(S); };
	struct FCase { FString Label; FSet Reference; FSet Test; };
	TArray<FCase> Cases = {
		{TEXT("browDownL 1.5"), {{C(TEXT("browDownL")), 1.0f}}, {{C(TEXT("browDownL")), 1.5f}}},
		{TEXT("browDownL 1 + pasFacBrowDown_l 0.5"), {{C(TEXT("browDownL")), 1.0f}}, {{C(TEXT("browDownL")), 1.0f}, {N(TEXT("pasFacBrowDown_l")), 0.5f}}},
		{TEXT("pasFacBrowDown_l 1 alone"), {{C(TEXT("browDownL")), 1.0f}}, {{N(TEXT("pasFacBrowDown_l")), 1.0f}}},
		{TEXT("browDownL 1 + facBrowDown_l 0.5"), {{C(TEXT("browDownL")), 1.0f}}, {{C(TEXT("browDownL")), 1.0f}, {N(TEXT("facBrowDown_l")), 0.5f}}},
		{TEXT("facBrowDown_l 1 alone"), {{C(TEXT("browDownL")), 1.0f}}, {{N(TEXT("facBrowDown_l")), 1.0f}}},
		{TEXT("jawOpen 1 + pasFacJawOpen 0.5"), {{C(TEXT("jawOpen")), 1.0f}}, {{C(TEXT("jawOpen")), 1.0f}, {N(TEXT("pasFacJawOpen")), 0.5f}}},
		{TEXT("jawOpen 1 + facJawOpen 0.5"), {{C(TEXT("jawOpen")), 1.0f}}, {{C(TEXT("jawOpen")), 1.0f}, {N(TEXT("facJawOpen")), 0.5f}}},
		{TEXT("pasFacMouthMiddleUpperUp 1 (undriven) vs facMouthMiddleUpperUp 1"), {{N(TEXT("facMouthMiddleUpperUp")), 1.0f}}, {{N(TEXT("pasFacMouthMiddleUpperUp")), 1.0f}}},
		{TEXT("pasFacMouthMiddleUpperUp 1.5 vs 1"), {{N(TEXT("pasFacMouthMiddleUpperUp")), 1.0f}}, {{N(TEXT("pasFacMouthMiddleUpperUp")), 1.5f}}},
		{TEXT("tongueTipUp 1 + pasFacTongueTipUp 0.5"), {{C(TEXT("tongueTipUp")), 1.0f}}, {{C(TEXT("tongueTipUp")), 1.0f}, {N(TEXT("pasFacTongueTipUp")), 0.5f}}},
		{TEXT("browRaiseInL 1 + facBrowInnerUp_l 0.5"), {{C(TEXT("browRaiseInL")), 1.0f}}, {{C(TEXT("browRaiseInL")), 1.0f}, {N(TEXT("facBrowInnerUp_l")), 0.5f}}},
	};
	TArray<FString> Report;
	for (const FCase& K : Cases)
	{
		TArray<FVector3f> A, B;
		Evaluator.Evaluate(K.Reference, A);
		Evaluator.Evaluate(K.Test, B);
		double AA = 0.0, AB = 0.0, BB = 0.0, MaxA = 0.0, MaxB = 0.0;
		for (int32 I = 0; I < Rest.Num(); ++I)
		{
			const FVector3f DA = A[I] - Rest[I], DB = B[I] - Rest[I];
			AA += FVector3f::DotProduct(DA, DA); AB += FVector3f::DotProduct(DA, DB); BB += FVector3f::DotProduct(DB, DB);
			MaxA = FMath::Max(MaxA, (double)DA.Size()); MaxB = FMath::Max(MaxB, (double)DB.Size());
		}
		const double Along = AA > 0.0 ? AB / AA : 0.0;
		const double Off = FMath::Sqrt(FMath::Max(BB - Along * Along * AA, 0.0) / FMath::Max(AA, 1e-12));
		Report.Add(FString::Printf(TEXT("%s: test moves %.3f cm at most (reference %.3f); along the reference x%.3f, off it %.3f"), *K.Label, MaxB, MaxA, Along, Off));
	}
	FFileHelper::SaveStringArrayToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/CurveReachReport.txt")));
	for (const FString& L : Report) AddInfo(L);
	return true;
}

#endif
