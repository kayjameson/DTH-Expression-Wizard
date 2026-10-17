#include "Mixer/DTHCurveDriverInstance.h"

void FDTHCurveDriverProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	Values.Reset();
	for (const TPair<FName, float>& V : CastChecked<UDTHCurveDriverInstance>(InAnimInstance)->GetCurves())
	{
		Values.Emplace(V.Key, V.Value);
	}
}

bool FDTHCurveDriverProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	for (const TPair<FName, float>& V : Values)
	{
		Output.Curve.Set(V.Key, V.Value);
	}
	return true;
}
