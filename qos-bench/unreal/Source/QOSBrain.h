#pragma once

#include "CoreMinimal.h"
#include "NNEModelData.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::NNE
{
	class IModelCPU;
	class IModelInstanceCPU;
}

/**
 * Loads a QOS ONNX model straight from disk (no asset import) and evaluates it
 * through UE's Neural Network Engine on the CPU. Input width is read from the
 * model itself, so 3-input and 8-input oracles share one code path.
 */
class OTUKEN_API FQOSBrain
{
public:
	bool LoadFromFile(const FString& OnnxPath, FString& OutError);

	/** Inputs must be normalized to [0,1] and match GetNumInputs(). Returns stress in [0,1], or -1 on failure. */
	float Evaluate(TConstArrayView<float> Inputs) const;

	int32 GetNumInputs() const { return NumInputs; }
	bool IsReady() const { return Instance.IsValid(); }
	const FString& GetRuntimeName() const { return RuntimeName; }

private:
	TStrongObjectPtr<UNNEModelData> ModelData;
	TSharedPtr<UE::NNE::IModelCPU> Model;
	TSharedPtr<UE::NNE::IModelInstanceCPU> Instance;
	FString RuntimeName;
	int32 NumInputs = 0;
};
