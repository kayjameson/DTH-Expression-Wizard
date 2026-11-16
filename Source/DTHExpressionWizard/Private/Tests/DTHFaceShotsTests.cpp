// The Unreal side of the Daz/Unreal face shots (tools/daz/dth_face_shots.dsa renders the Daz side): each face fitted on
// the character, played through its post-process blueprint off screen (DTH's deformer and materials), and captured from
// the camera the Daz shot uses (the head from DISTANCE cm in front, a little above the head joint, FOV degrees wide).
// Inputs: <Project>/Saved/DTHExpressionWizard/FaceShotsTest.json
//   character          the DTH Genesis 9 skeletal mesh (a base Genesis 9 export, as the samples)
//   expressionSamples  DTH_ExpressionSamples.json (pose shots: DTH.Pose.Face.Shots)
//   out                folder for <name>_ue.png (next to the Daz shots)
//   width, height      the Daz shots' size
//   shots              [{name, kind: "expression", label}]
#include "Dom/JsonObject.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "DTHBodyHarness.h"
#include "DTHFaceBones.h"
#include "DTHFaceFitter.h"
#include "Engine/SkeletalMesh.h"
#include "Library/DTHExpressionSamples.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHFaceShotsTest, "DTH.Face.Shots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHFaceShotsTest::RunTest(const FString& Parameters)
{
	constexpr double Distance = 45.0, Above = 6.0, Fov = 30.0;
	FString Text;
	TSharedPtr<FJsonObject> Config;
	const FString ConfigFile = FPaths::ProjectSavedDir() / TEXT("DTHExpressionWizard/FaceShotsTest.json");
	if (!FFileHelper::LoadFileToString(Text, *ConfigFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config)
	{
		AddError(TEXT("No test inputs: ") + ConfigFile);
		return false;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	if (!Mesh) { AddError(TEXT("No character.")); return false; }
	const FString Out = Config->GetStringField(TEXT("out"));
	int32 Width = 768, Height = 768;
	Config->TryGetNumberField(TEXT("width"), Width);
	Config->TryGetNumberField(TEXT("height"), Height);

	FString Error;
	FDTHExpressionSamples Expressions;
	const bool bExpressions = Expressions.Load(Config->GetStringField(TEXT("expressionSamples")), Error) && Expressions.HasFace();
	TUniquePtr<FDTHFaceFitter> ExprFit;
	TArray<FString> Report;
	if (bExpressions) { ExprFit = MakeUnique<FDTHFaceFitter>(); if (!ExprFit->Init(Mesh, Expressions.Face, Report, Error)) { AddError(Error); return false; } }

	FDTHBodyHarness Harness;
	if (!Harness.Init(Mesh, Error)) { AddError(Error); return false; }
	Harness.SetLightDirection(FRotator(-30.0, -100.0, 0.0));
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	// The eyes' wet layers (moisture, tear) are translucent shells over the iris that the off-screen capture draws opaque:
	// hidden for the shots, so the gaze shows (the fit never uses them).
	if (USkeletalMeshComponent* C = Harness.GetComponent())
	{
		const FSkeletalMeshRenderData* RD = C->GetSkeletalMeshAsset()->GetResourceForRendering();
		for (int32 S = 0; RD && S < RD->LODRenderData[0].RenderSections.Num(); ++S)
		{
			const int32 M = RD->LODRenderData[0].RenderSections[S].MaterialIndex;
			const FString Slot = C->GetSkeletalMeshAsset()->GetMaterials().IsValidIndex(M) ? C->GetSkeletalMeshAsset()->GetMaterials()[M].MaterialSlotName.ToString() : FString();
			if (Slot.Contains(TEXT("Moisture")) || Slot.Contains(TEXT("Tear"))) C->ShowMaterialSection(M, S, false, 0);
		}
	}

	TMap<FName, FTransform> RestPose;
	for (int32 B = 0; B < Ref.GetNum(); ++B) RestPose.Add(Ref.GetBoneName(B), Ref.GetRefBonePose()[B]);

	for (const TSharedPtr<FJsonValue>& V : Config->GetArrayField(TEXT("shots")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		const FString Name = S->GetStringField(TEXT("name")), Kind = S->GetStringField(TEXT("kind")), Label = S->GetStringField(TEXT("label"));
		TArray<TArray<FVector3f>> Face;
		FDTHFaceFitter* Fitter = nullptr;
		FDTHShapeFit::FOptions Options;
		if (Kind == TEXT("expression") && bExpressions)
		{
			Fitter = ExprFit.Get();
			Expressions.ReadExpressions([&](FDTHDazExpression&& E, TArray<TArray<FVector3f>>&& D) { if (E.Label == Label) { Face = MoveTemp(D); return false; } return true; }, Error);
		}
		FDTHFaceFitter::FResult R;
		if (!Fitter || Face.IsEmpty() || !Fitter->Fit(Face, Options, R)) { AddError(Name + TEXT(": no face to fit")); continue; }
		double WorstMm = 0.0;
		FString WorstRegion;
		const FString Regions = Fitter->RegionReport(R, WorstMm, WorstRegion);

		// the face on the body at rest, as the Daz shot has it
		TMap<FName, FTransform> Pose = RestPose;
		for (const TPair<FName, FTransform>& B : DTHFaceBones::Local(Ref, R.Bones)) Pose.Add(B.Key, B.Value);
		FDTHBoneReadback Read;
		Harness.EvalPose(Pose, R.Curves, Read);
		const int32 Head = Read.Find(TEXT("head"));
		const FVector H = Head != INDEX_NONE ? Read.Component[Head].GetLocation() : FVector(0, 0, 160);
		// Daz +Z (the face's front) is Unreal +Y, Daz +Y (up) is Unreal +Z
		const FVector Target = H + FVector(0.0, 0.0, Above);
		const FVector Eye = Target + FVector(0.0, Distance, 0.0);
		// Daz's FOV is across the frame's width (a 36 mm frame); the capture's is horizontal too
		if (!Harness.Capture(Out / Name + TEXT("_ue.png"), Eye, Target, FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov) / 2.0) * Width / FMath::Max(Height, 1))), Width, Height, Error)) { AddError(Name + TEXT(": ") + Error); continue; }
		Report.Add(FString::Printf(TEXT("%s (%s %s): worst %s %.2f mm |%s"), *Name, *Kind, *Label, *WorstRegion, WorstMm, *Regions));
	}
	FFileHelper::SaveStringArrayToFile(Report, *(Out / TEXT("ue_shots_report_expressions.txt")));
	for (const FString& L : Report) AddInfo(L);
	return true;
}

#endif
