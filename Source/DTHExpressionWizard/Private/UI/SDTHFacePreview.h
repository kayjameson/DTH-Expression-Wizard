#pragma once

#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class UDebugSkelMeshComponent;
class USkeletalMesh;

/**
 * The Mixer's live preview: the character in its own preview scene (never the level or the editor camera), playing the
 * current mix's face-board curves, so the preview goes through the same DTH post-process blueprint and face rig as a
 * keyed sequence.
 */
class SDTHFacePreview : public SEditorViewport, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SDTHFacePreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDTHFacePreview() override;

	/** Shows Mesh (null clears it) and frames its face. */
	void SetCharacter(USkeletalMesh* Mesh);
	/** Plays these face-board values. */
	void SetValues(const TMap<FName, float>& Values);
	/** Frames the face from the front. */
	void FrameFace() { SetView(TEXT("front")); }
	/** Frames a view of the face: front, threequarter, eyes or mouth. False for an unknown name or no character. */
	bool SetView(const FString& View);
	/** Frames View, renders the preview and saves it as a PNG. */
	bool Capture(const FString& File, const FString& View, FString& OutError);

	USkeletalMesh* GetCharacter() const;
	/** The preview's component (for scripting checks). */
	UDebugSkelMeshComponent* GetComponent() const { return Component; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SDTHFacePreview"); }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<FEditorViewportClient> Client;
	TObjectPtr<UDebugSkelMeshComponent> Component = nullptr;
	/** Ticks to wait before framing a new character (its bones are posed after the first ticks). */
	int32 FrameAfterTicks = 0;
};
