#pragma once

#include "CoreMinimal.h"
#include "QOSBrain.h"
#include "QOSTelemetry.h"
#include "Subsystems/WorldSubsystem.h"
#include "QOSBenchSubsystem.generated.h"

class ACameraActor;
class APointLight;
class AStaticMeshActor;

/**
 * QOS benchmark harness. Activates only when the game is launched with -QOSArm=...
 * and needs no changes to the map: it spawns its own camera, flies a deterministic
 * terrain-following figure-8 around the placed static meshes, injects a wave of
 * GPU stress (shadowed movable lights and extra meshes), samples telemetry, drives
 * quality settings according to the selected arm's policy, logs every frame to CSV
 * and quits when done.
 *
 * Command line:
 *   -QOSArm=baseline|threshold|classical|qos   (required)
 *   -QOSModel=<path.onnx>                      (classical / qos arms)
 *   -QOSRun=<label>            default 1
 *   -QOSDuration=<seconds>     default 60
 *   -QOSWarmup=<seconds>       default 5
 *   -QOSEval=<seconds>         default 0.2   (policy evaluation interval)
 *   -QOSRadius=<cm>            auto from static mesh bounds
 *   -QOSHeight=<cm>            fixed camera height (disables terrain following)
 *   -QOSEye=<cm>               eye height above ground, default 180
 *   -QOSStressLights=<n>       shadowed movable point lights toggled in two waves
 *   -QOSStressMeshes=<n>       copies of the heaviest placed meshes toggled in the same waves
 *   -QOSPool=<MB>              texture streaming pool override (simulates a smaller GPU budget)
 *   -QOSKnobs=a,b,c            quality knobs the tiers move: sp, dynres, nanite, foliage, lod (default dynres,nanite)
 *   -QOSArm=cycle              walks tiers 0-3 every 3 s regardless of telemetry (knob switching-cost test)
 *   -QOSOut=<dir>              default <Project>/Saved/QOSBench
 */
UCLASS()
class OTUKEN_API UQOSBenchSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UQOSBenchSubsystem, STATGROUP_Tickables); }

private:
	enum class EArm : uint8 { Baseline, Threshold, Classical, QOS, Cycle };
	enum class EPhase : uint8 { Idle, Setup, Warmup, Running, Done };

	bool ParseCommandLine();
	bool SetupScene();
	void ComputeLevelBounds();
	float GroundZ(const FVector& At, float Fallback) const;
	void SpawnStress();
	void UpdateStress(float PathTime);
	void UpdateCamera(float PathTime, float DeltaTime);
	void EvaluatePolicy(float DeltaTime);
	int32 TierForScore(float Score) const;
	void ApplyTier(int32 Tier);
	void Exec(const FString& Command) const;
	void OpenCsv();
	void WriteCsvRow(float DeltaTime);
	void Finish();

	// configuration
	EArm Arm = EArm::Baseline;
	FString ArmName;
	FString ModelPath;
	FString RunLabel = TEXT("1");
	FString OutDir;
	float Duration = 60.f;
	float Warmup = 5.f;
	float EvalInterval = 0.2f;
	float OrbitRadius = 0.f;
	float FixedHeight = 0.f;
	float EyeHeight = 180.f;
	int32 StressLights = 0;
	int32 StressMeshes = 0;
	float PoolSizeMB = 0.f;
	TArray<FString> ActiveKnobs;

	// state
	EPhase Phase = EPhase::Idle;
	bool bActive = false;
	float Elapsed = 0.f;
	float EvalTimer = 0.f;
	int64 FrameIndex = 0;
	FVector LevelCenter = FVector::ZeroVector;
	FVector LevelExtent = FVector(3000.f, 3000.f, 500.f);
	FVector PrevCameraPos = FVector::ZeroVector;
	float CameraSpeed = 0.f;

	TObjectPtr<ACameraActor> Camera;
	TArray<TObjectPtr<APointLight>> Lights;
	TArray<TObjectPtr<AStaticMeshActor>> Meshes;
	int32 ActiveLights = 0;
	int32 ActiveMeshes = 0;

	FQOSBrain Brain;
	FQOSTelemetry Telemetry;
	FQOSSample LastSample;
	float RawScore = -1.f;
	float SmoothedScore = 0.f;
	int32 CurrentTier = -1;
	int32 TierChanges = 0;
	bool bTierChangedThisFrame = false;

	TUniquePtr<FArchive> Csv;
	FString CsvPath;
};
