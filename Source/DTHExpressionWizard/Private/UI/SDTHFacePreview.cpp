#include "UI/SDTHFacePreview.h"

#include "AdvancedPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Mixer/DTHCurveDriverInstance.h"

namespace
{
	/** Ticks the preview world so the face animation and post-process blueprint run. */
	class FFacePreviewClient : public FEditorViewportClient
	{
	public:
		FFacePreviewClient(FPreviewScene* InScene, const TWeakPtr<SEditorViewport>& InViewport)
			: FEditorViewportClient(nullptr, InScene, InViewport)
		{
			SetRealtime(true);
			EngineShowFlags.SetGrid(false);
			ViewFOV = 30.0f;
		}

		virtual void Tick(float DeltaSeconds) override
		{
			FEditorViewportClient::Tick(DeltaSeconds);
			if (PreviewScene)
			{
				PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
			}
		}
	};
}

void SDTHFacePreview::Construct(const FArguments& InArgs)
{
	Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	Scene->SetFloorVisibility(false);
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SDTHFacePreview::~SDTHFacePreview()
{
	if (Client.IsValid())
	{
		Client->Viewport = nullptr;
	}
}

TSharedRef<FEditorViewportClient> SDTHFacePreview::MakeEditorViewportClient()
{
	Client = MakeShared<FFacePreviewClient>(Scene.Get(), SharedThis(this));
	Client->SetViewLocation(FVector(0.0, 80.0, 150.0));
	Client->SetViewRotation(FRotator(0.0, -90.0, 0.0));
	return Client.ToSharedRef();
}

USkeletalMesh* SDTHFacePreview::GetCharacter() const
{
	return Component ? Component->GetSkeletalMeshAsset() : nullptr;
}

void SDTHFacePreview::SetCharacter(USkeletalMesh* Mesh)
{
	if (Component)
	{
		Scene->RemoveComponent(Component);
		Component = nullptr;
	}
	if (!Mesh || !Mesh->GetSkeleton())
	{
		return;
	}
	Component = NewObject<UDebugSkelMeshComponent>(GetTransientPackage());
	Component->SetSkeletalMesh(Mesh);
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(UDTHCurveDriverInstance::StaticClass());
	Scene->AddComponent(Component, FTransform::Identity);
	FrameAfterTicks = 3;
}

void SDTHFacePreview::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (FrameAfterTicks > 0 && --FrameAfterTicks == 0)
	{
		FrameFace();
	}
}

void SDTHFacePreview::SetValues(const TMap<FName, float>& Values)
{
	if (UDTHCurveDriverInstance* Driver = Component ? Cast<UDTHCurveDriverInstance>(Component->GetAnimInstance()) : nullptr)
	{
		Driver->SetCurves(Values);
	}
}

bool SDTHFacePreview::SetView(const FString& View)
{
	if (!Component || !Client.IsValid())
	{
		return false;
	}
	const FName Head(TEXT("head"));
	// Eye level, from the head bone; DTH characters face +Y on the UE5 Mannequin skeleton.
	const FVector Face = Component->DoesSocketExist(Head) ? Component->GetSocketLocation(Head) + FVector(0.0, 2.0, 5.0)
		: Component->Bounds.Origin + FVector(0.0, 0.0, Component->Bounds.BoxExtent.Z * 0.85);
	FVector Target = Face, Eye;
	if (View.Equals(TEXT("front"), ESearchCase::IgnoreCase)) Eye = Face + FVector(0.0, 70.0, 0.0);
	else if (View.Equals(TEXT("threequarter"), ESearchCase::IgnoreCase)) Eye = Face + FVector(70.0 * FMath::Cos(FMath::DegreesToRadians(55.0)), 70.0 * FMath::Sin(FMath::DegreesToRadians(55.0)), 0.0);
	else if (View.Equals(TEXT("eyes"), ESearchCase::IgnoreCase)) Eye = Face + FVector(0.0, 32.0, 0.0);
	else if (View.Equals(TEXT("mouth"), ESearchCase::IgnoreCase)) { Target = Face + FVector(0.0, 7.0, -7.0); Eye = Target + FVector(0.0, 32.0, 0.0); }
	else return false;
	Client->SetViewLocation(Eye);
	Client->SetViewRotation((Target - Eye).Rotation());
	Client->Invalidate();
	return true;
}

bool SDTHFacePreview::Capture(const FString& File, const FString& View, FString& OutError)
{
	if (!SetView(View))
	{
		OutError = FString::Printf(TEXT("Cannot frame \"%s\" (no character, or not front/threequarter/eyes/mouth)."), *View);
		return false;
	}
	FViewport* Viewport = Client->Viewport;
	if (!Viewport || Viewport->GetSizeXY().X <= 0)
	{
		OutError = TEXT("The preview is not on screen (open the Mixer page).");
		return false;
	}
	Viewport->Draw(false);
	FlushRenderingCommands();
	TArray<FColor> Pixels;
	if (!Viewport->ReadPixels(Pixels))
	{
		OutError = TEXT("Could not read the preview's pixels.");
		return false;
	}
	for (FColor& P : Pixels) P.A = 255;
	const FImageView Image(Pixels.GetData(), Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y);
	if (!FImageUtils::SaveImageByExtension(*File, Image))
	{
		OutError = FString::Printf(TEXT("Could not write %s."), *File);
		return false;
	}
	return true;
}

void SDTHFacePreview::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Component);
}
