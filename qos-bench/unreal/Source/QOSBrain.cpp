#include "QOSBrain.h"

#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeRunSync.h"
#include "NNETypes.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogQOS, Log, All);

bool FQOSBrain::LoadFromFile(const FString& OnnxPath, FString& OutError)
{
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *OnnxPath))
	{
		OutError = FString::Printf(TEXT("cannot read model file: %s"), *OnnxPath);
		return false;
	}

	UNNEModelData* Data = NewObject<UNNEModelData>(GetTransientPackage(), NAME_None, RF_Transient);
	Data->Init(TEXT("onnx"), TConstArrayView64<uint8>(Bytes.GetData(), Bytes.Num()));
	ModelData.Reset(Data);

	static const TCHAR* Candidates[] = { TEXT("NNERuntimeORTCpu"), TEXT("NNERuntimeBasicCpu") };
	TWeakInterfacePtr<INNERuntimeCPU> Runtime;
	for (const TCHAR* Name : Candidates)
	{
		Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(FString(Name));
		if (Runtime.IsValid())
		{
			RuntimeName = Name;
			break;
		}
	}
	if (!Runtime.IsValid())
	{
		OutError = TEXT("no CPU NNE runtime available (enable the NNERuntimeORT plugin)");
		return false;
	}

	Model = Runtime->CreateModelCPU(Data);
	if (!Model.IsValid())
	{
		OutError = FString::Printf(TEXT("%s could not create a model from %s"), *RuntimeName, *OnnxPath);
		return false;
	}

	Instance = Model->CreateModelInstanceCPU();
	if (!Instance.IsValid())
	{
		OutError = TEXT("could not create a model instance");
		return false;
	}

	TConstArrayView<UE::NNE::FTensorDesc> InputDescs = Instance->GetInputTensorDescs();
	if (InputDescs.Num() != 1)
	{
		OutError = FString::Printf(TEXT("expected exactly one input tensor, model has %d"), InputDescs.Num());
		return false;
	}
	TConstArrayView<int32> Shape = InputDescs[0].GetShape().GetData();
	NumInputs = Shape.Num() > 0 ? Shape[Shape.Num() - 1] : 0;
	if (NumInputs <= 0)
	{
		OutError = TEXT("could not infer input width from the model shape");
		return false;
	}

	const uint32 Dims[] = { 1u, static_cast<uint32>(NumInputs) };
	const UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make(Dims);
	if (Instance->SetInputTensorShapes(MakeArrayView(&InputShape, 1)) != UE::NNE::IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
	{
		OutError = TEXT("SetInputTensorShapes failed");
		return false;
	}

	UE_LOG(LogQOS, Log, TEXT("QOS brain loaded: %s | runtime %s | %d inputs"), *OnnxPath, *RuntimeName, NumInputs);
	return true;
}

float FQOSBrain::Evaluate(TConstArrayView<float> Inputs) const
{
	if (!Instance.IsValid() || Inputs.Num() != NumInputs)
	{
		return -1.0f;
	}

	UE::NNE::FTensorBindingCPU InputBinding;
	InputBinding.Data = const_cast<float*>(Inputs.GetData());
	InputBinding.SizeInBytes = Inputs.Num() * sizeof(float);

	float Output = 0.0f;
	UE::NNE::FTensorBindingCPU OutputBinding;
	OutputBinding.Data = &Output;
	OutputBinding.SizeInBytes = sizeof(float);

	if (Instance->RunSync(MakeArrayView(&InputBinding, 1), MakeArrayView(&OutputBinding, 1)) != UE::NNE::IModelInstanceCPU::ERunSyncStatus::Ok)
	{
		return -1.0f;
	}
	return Output;
}
