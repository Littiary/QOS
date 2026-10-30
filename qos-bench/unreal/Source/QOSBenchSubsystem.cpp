#include "QOSBenchSubsystem.h"

#include "Camera/CameraActor.h"
#include "CollisionQueryParams.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"

DEFINE_LOG_CATEGORY_STATIC(LogQOSBench, Log, All);

namespace
{
	// Quality tiers applied by every arm. Only continuous, hitch-free knobs are
	// used (the kind dynamic resolution systems touch every frame): scalability
	// groups are pinned once at setup and never changed mid-run, because switching
	// them recompiles shaders and would turn every decision into a 100+ ms hitch.
	// Tier 0 is the untouched high-quality baseline; higher tiers trade fidelity
	// for frame time. Identical for all arms so that only the policy differs.
	// Quality knobs a tier can move. Each is selectable from the command line
	// (-QOSKnobs=dynres,nanite) so the switching cost of every knob
	// can be measured in isolation with the "cycle" arm.
	struct FKnob
	{
		const TCHAR* Name;
		const TCHAR* Commands[4];
	};
	const FKnob Knobs[] = {
		{ TEXT("sp"),      { TEXT("r.ScreenPercentage 100"), TEXT("r.ScreenPercentage 90"), TEXT("r.ScreenPercentage 80"), TEXT("r.ScreenPercentage 65") } },
		{ TEXT("dynres"),  { TEXT("r.DynamicRes.TestScreenPercentage 100"), TEXT("r.DynamicRes.TestScreenPercentage 90"), TEXT("r.DynamicRes.TestScreenPercentage 80"), TEXT("r.DynamicRes.TestScreenPercentage 65") } },
		{ TEXT("nanite"),  { TEXT("r.Nanite.MaxPixelsPerEdge 1"), TEXT("r.Nanite.MaxPixelsPerEdge 1.5"), TEXT("r.Nanite.MaxPixelsPerEdge 2"), TEXT("r.Nanite.MaxPixelsPerEdge 3") } },
		{ TEXT("foliage"), { TEXT("foliage.LODDistanceScale 1"), TEXT("foliage.LODDistanceScale 0.8"), TEXT("foliage.LODDistanceScale 0.6"), TEXT("foliage.LODDistanceScale 0.4") } },
		{ TEXT("lod"),     { TEXT("r.StaticMeshLODDistanceScale 1"), TEXT("r.StaticMeshLODDistanceScale 1.1"), TEXT("r.StaticMeshLODDistanceScale 1.3"), TEXT("r.StaticMeshLODDistanceScale 1.6") } },
	};
	const TCHAR* const DefaultKnobs = TEXT("dynres,nanite");

	// Pinned once for every arm: the heavy baseline the tiers scale down from.
	const TCHAR* const BaseQuality[] = {
		TEXT("sg.ViewDistanceQuality 4"), TEXT("sg.ShadowQuality 4"), TEXT("sg.GlobalIlluminationQuality 4"), TEXT("sg.ReflectionQuality 4"),
		TEXT("sg.FoliageQuality 4"), TEXT("sg.EffectsQuality 4"), TEXT("sg.TextureQuality 4"), TEXT("sg.ShadingQuality 4") };

	// Score bands (rising) with hysteresis on the way down.
	constexpr float TierUp[3] = { 0.35f, 0.55f, 0.75f };
	constexpr float Hysteresis = 0.10f;
	constexpr float EmaAlpha = 0.2f;
	constexpr int32 StressWaves = 2;  // load rises and falls this many times per run

	bool HasCommandLineArm()
	{
		FString Dummy;
		return FParse::Value(FCommandLine::Get(), TEXT("QOSArm="), Dummy);
	}

	// sunflower spiral: evenly spread points inside a disc
	FVector2D SpiralPoint(int32 Index, int32 Count, float Radius)
	{
		const float R = Radius * FMath::Sqrt((Index + 0.5f) / FMath::Max(Count, 1));
		const float A = Index * 2.399963f;  // golden angle
		return FVector2D(R * FMath::Cos(A), R * FMath::Sin(A));
	}
}

bool UQOSBenchSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && HasCommandLineArm();
}

bool UQOSBenchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UQOSBenchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!ParseCommandLine())
	{
		// launched for a benchmark but cannot run one: do not leave a zombie game process behind
		UE_LOG(LogQOSBench, Error, TEXT("QOS bench could not start, exiting"));
		FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("QOSBench"));
		return;
	}
	bActive = true;
	Phase = EPhase::Setup;
	UE_LOG(LogQOSBench, Display, TEXT("QOS bench armed: arm=%s run=%s duration=%.0fs warmup=%.0fs eval=%.2fs lights=%d meshes=%d pool=%.0fMB knobs=%s"),
	       *ArmName, *RunLabel, Duration, Warmup, EvalInterval, StressLights, StressMeshes, PoolSizeMB, *FString::Join(ActiveKnobs, TEXT(",")));
}

void UQOSBenchSubsystem::Deinitialize()
{
	if (Csv.IsValid())
	{
		Csv->Close();
		Csv.Reset();
	}
	Super::Deinitialize();
}

bool UQOSBenchSubsystem::ParseCommandLine()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("QOSArm="), ArmName);
	ArmName = ArmName.ToLower();
	if (ArmName == TEXT("baseline")) Arm = EArm::Baseline;
	else if (ArmName == TEXT("threshold")) Arm = EArm::Threshold;
	else if (ArmName == TEXT("classical")) Arm = EArm::Classical;
	else if (ArmName == TEXT("qos")) Arm = EArm::QOS;
	else if (ArmName == TEXT("cycle")) Arm = EArm::Cycle;
	else
	{
		UE_LOG(LogQOSBench, Error, TEXT("unknown -QOSArm=%s (baseline|threshold|classical|qos)"), *ArmName);
		return false;
	}

	FParse::Value(Cmd, TEXT("QOSModel="), ModelPath);
	FParse::Value(Cmd, TEXT("QOSRun="), RunLabel);
	FParse::Value(Cmd, TEXT("QOSDuration="), Duration);
	FParse::Value(Cmd, TEXT("QOSWarmup="), Warmup);
	FParse::Value(Cmd, TEXT("QOSEval="), EvalInterval);
	FParse::Value(Cmd, TEXT("QOSRadius="), OrbitRadius);
	FParse::Value(Cmd, TEXT("QOSHeight="), FixedHeight);
	FParse::Value(Cmd, TEXT("QOSEye="), EyeHeight);
	FParse::Value(Cmd, TEXT("QOSStressLights="), StressLights);
	FParse::Value(Cmd, TEXT("QOSStressMeshes="), StressMeshes);
	FParse::Value(Cmd, TEXT("QOSPool="), PoolSizeMB);
	FParse::Value(Cmd, TEXT("QOSOut="), OutDir);
	FString KnobList = DefaultKnobs;
	FParse::Value(Cmd, TEXT("QOSKnobs="), KnobList);
	KnobList.ParseIntoArray(ActiveKnobs, TEXT(","), true);
	for (FString& K : ActiveKnobs)
	{
		K.TrimStartAndEndInline();
		K.ToLowerInline();
	}
	if (OutDir.IsEmpty())
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("QOSBench"));
	}
	EvalInterval = FMath::Max(EvalInterval, 0.05f);

	if (Arm == EArm::Classical || Arm == EArm::QOS)
	{
		FString Error;
		if (ModelPath.IsEmpty() || !Brain.LoadFromFile(ModelPath, Error))
		{
			UE_LOG(LogQOSBench, Error, TEXT("arm %s needs a model: %s"), *ArmName, *Error);
			return false;
		}
	}
	return true;
}

void UQOSBenchSubsystem::ComputeLevelBounds()
{
	// Prefer the placed static meshes: they mark where the authored content is.
	// Landscapes, sky spheres and volumes would drag the center into empty space.
	FBox MeshBox(ForceInit);
	FBox AnyBox(ForceInit);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor->GetRootComponent() || Actor == Camera)
		{
			continue;
		}
		const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
		const FVector Size = ActorBox.GetSize();
		if (!ActorBox.IsValid || Size.GetMax() > 500000.f || Size.GetMax() < 10.f)
		{
			continue;
		}
		AnyBox += ActorBox;
		if (Actor->IsA<AStaticMeshActor>() && Size.GetMax() < 50000.f)
		{
			MeshBox += ActorBox;
		}
	}
	const FBox& Box = MeshBox.IsValid ? MeshBox : AnyBox;
	if (Box.IsValid)
	{
		LevelCenter = Box.GetCenter();
		LevelExtent = Box.GetExtent();
	}
	if (OrbitRadius <= 0.f)
	{
		OrbitRadius = FMath::Clamp(FMath::Max(LevelExtent.X, LevelExtent.Y) * 0.7f, 800.f, 6000.f);
	}
	UE_LOG(LogQOSBench, Display, TEXT("level bounds (%s) center=%s extent=%s | orbit radius=%.0f"),
	       MeshBox.IsValid ? TEXT("static meshes") : TEXT("all actors"), *LevelCenter.ToString(), *LevelExtent.ToString(), OrbitRadius);
}

float UQOSBenchSubsystem::GroundZ(const FVector& At, float Fallback) const
{
	FHitResult Hit;
	const FVector Start(At.X, At.Y, LevelCenter.Z + 50000.f);
	const FVector End(At.X, At.Y, LevelCenter.Z - 50000.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(QOSGround), false);
	if (Camera)
	{
		Params.AddIgnoredActor(Camera);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return Hit.Location.Z;
	}
	return Fallback;
}

void UQOSBenchSubsystem::SpawnStress()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const float Spread = OrbitRadius * 0.9f;

	for (int32 i = 0; i < StressLights; ++i)
	{
		const FVector2D P = SpiralPoint(i, StressLights, Spread);
		FVector Pos(LevelCenter.X + P.X, LevelCenter.Y + P.Y, 0.f);
		Pos.Z = GroundZ(Pos, LevelCenter.Z) + 250.f;
		APointLight* Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(Pos), Params);
		if (!Light)
		{
			continue;
		}
		if (UPointLightComponent* LC = Light->PointLightComponent)
		{
			LC->SetMobility(EComponentMobility::Movable);
			LC->SetIntensity(15000.f);
			LC->SetAttenuationRadius(4000.f);
			LC->SetCastShadows(true);
			LC->SetVisibility(false);
		}
		Lights.Add(Light);
	}

	if (StressMeshes > 0)
	{
		// the three heaviest placed meshes become the stress geometry
		TArray<TPair<int32, UStaticMesh*>> Candidates;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			UStaticMesh* Mesh = It->GetStaticMeshComponent() ? It->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
			if (Mesh && Mesh->GetRenderData())
			{
				Candidates.Emplace(Mesh->GetNumTriangles(0), Mesh);
			}
		}
		Candidates.Sort([](const TPair<int32, UStaticMesh*>& A, const TPair<int32, UStaticMesh*>& B) { return A.Key > B.Key; });
		TArray<UStaticMesh*> Sources;
		for (const TPair<int32, UStaticMesh*>& C : Candidates)
		{
			if (!Sources.Contains(C.Value))
			{
				Sources.Add(C.Value);
			}
			if (Sources.Num() >= 3)
			{
				break;
			}
		}
		for (int32 i = 0; i < StressMeshes && Sources.Num() > 0; ++i)
		{
			const FVector2D P = SpiralPoint(i, StressMeshes, Spread);
			FVector Pos(LevelCenter.X + P.X, LevelCenter.Y + P.Y, 0.f);
			Pos.Z = GroundZ(Pos, LevelCenter.Z);
			const FRotator Rot(0.f, i * 37.f, 0.f);
			AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rot, Pos), Params);
			if (!MeshActor)
			{
				continue;
			}
			MeshActor->SetMobility(EComponentMobility::Movable);
			MeshActor->GetStaticMeshComponent()->SetStaticMesh(Sources[i % Sources.Num()]);
			MeshActor->SetActorHiddenInGame(true);
			Meshes.Add(MeshActor);
		}
		UE_LOG(LogQOSBench, Display, TEXT("stress geometry: %d meshes from %d source meshes (heaviest %d tris)"),
		       Meshes.Num(), Sources.Num(), Candidates.Num() ? Candidates[0].Key : 0);
	}
	UE_LOG(LogQOSBench, Display, TEXT("stress lights: %d spawned"), Lights.Num());
}

void UQOSBenchSubsystem::UpdateStress(float PathTime)
{
	const float Wave = 0.5f * (1.f - FMath::Cos(2.f * PI * StressWaves * PathTime / FMath::Max(Duration, 1.f)));
	const int32 WantLights = FMath::RoundToInt(Wave * Lights.Num());
	const int32 WantMeshes = FMath::RoundToInt(Wave * Meshes.Num());

	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		if (!Lights[i])
		{
			continue;
		}
		const bool bOn = i < WantLights;
		if (UPointLightComponent* LC = Lights[i]->PointLightComponent)
		{
			if (LC->IsVisible() != bOn)
			{
				LC->SetVisibility(bOn);
			}
		}
		if (bOn)
		{
			// keep the shadows dynamic: gently bob each active light
			FVector Pos = Lights[i]->GetActorLocation();
			Pos.Z += 3.f * FMath::Sin(PathTime * 3.f + i);
			Lights[i]->SetActorLocation(Pos);
		}
	}
	for (int32 i = 0; i < Meshes.Num(); ++i)
	{
		if (!Meshes[i])
		{
			continue;
		}
		const bool bOn = i < WantMeshes;
		if (Meshes[i]->IsHidden() != !bOn)
		{
			Meshes[i]->SetActorHiddenInGame(!bOn);
		}
	}
	ActiveLights = WantLights;
	ActiveMeshes = WantMeshes;
}

bool UQOSBenchSubsystem::SetupScene()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return false;  // try again next tick
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
	if (!Camera)
	{
		UE_LOG(LogQOSBench, Error, TEXT("could not spawn benchmark camera"));
		return false;
	}
	ComputeLevelBounds();
	SpawnStress();
	UpdateStress(0.f);
	UpdateCamera(0.f, 0.f);
	PrevCameraPos = Camera->GetActorLocation();
	PC->SetViewTarget(Camera);

	// measurement hygiene: no frame cap, no vsync, same starting quality for every arm
	Exec(TEXT("t.MaxFPS 0"));
	Exec(TEXT("r.VSync 0"));
	for (const TCHAR* Command : BaseQuality)
	{
		Exec(Command);
	}
	if (ActiveKnobs.Contains(TEXT("dynres")))
	{
		Exec(TEXT("r.DynamicRes.OperationMode 2"));
		Exec(TEXT("r.DynamicRes.MinScreenPercentage 50"));
		Exec(TEXT("r.DynamicRes.MaxScreenPercentage 100"));
	}
	if (PoolSizeMB > 0.f)
	{
		Exec(FString::Printf(TEXT("r.Streaming.PoolSize %d"), FMath::RoundToInt(PoolSizeMB)));
	}
	ApplyTier(0);
	TierChanges = 0;

	OpenCsv();
	return true;
}

void UQOSBenchSubsystem::UpdateCamera(float PathTime, float DeltaTime)
{
	if (!Camera)
	{
		return;
	}
	const float Period = FMath::Max(Duration, 1.f);
	auto PositionAt = [&](float T)
	{
		const float A = 2.f * PI * (T / Period);
		FVector Pos = LevelCenter + FVector(OrbitRadius * FMath::Sin(A), OrbitRadius * FMath::Sin(A) * FMath::Cos(A), 0.f);
		Pos.Z = FixedHeight > 0.f ? FixedHeight : GroundZ(Pos, LevelCenter.Z) + EyeHeight;
		return Pos;
	};

	const FVector Pos = PositionAt(PathTime);
	const FVector Ahead = PositionAt(PathTime + 0.5f);
	FRotator Rot = (Ahead - Pos).GetSafeNormal2D().Rotation();
	Rot.Pitch = -6.f;
	Camera->SetActorLocationAndRotation(Pos, Rot);

	CameraSpeed = DeltaTime > 0.f ? FVector::Dist(Pos, PrevCameraPos) / DeltaTime : 0.f;
	PrevCameraPos = Pos;
}

int32 UQOSBenchSubsystem::TierForScore(float Score) const
{
	int32 Tier = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		const float Threshold = (CurrentTier > i) ? TierUp[i] - Hysteresis : TierUp[i];
		if (Score > Threshold)
		{
			Tier = i + 1;
		}
	}
	return Tier;
}

void UQOSBenchSubsystem::EvaluatePolicy(float DeltaTime)
{
	const float LastFrameMs = DeltaTime * 1000.f;
	LastSample = Telemetry.Sample(GetWorld(), Camera->GetActorLocation(), Camera->GetActorRotation(), CameraSpeed, LastFrameMs);

	int32 NewTier = 0;
	switch (Arm)
	{
	case EArm::Baseline:
		RawScore = -1.f;
		NewTier = 0;
		break;

	case EArm::Cycle:
		// knob switching-cost test: walk the tiers on a fixed schedule
		RawScore = -1.f;
		NewTier = static_cast<int32>(Elapsed / 3.f) % 4;
		break;

	case EArm::Threshold:
		// static rule, same shape as qos-core heuristic.threshold_labels
		RawScore = ((LastSample.Proximity > 0.65f && LastSample.ObjectDensity > 0.6f) || LastSample.VramPressure > 0.8f) ? 1.f : 0.f;
		NewTier = RawScore > 0.5f ? 2 : 0;
		break;

	case EArm::Classical:
	case EArm::QOS:
		{
			const TArray<float> Inputs = LastSample.AsVector(Brain.GetNumInputs());
			RawScore = Brain.Evaluate(Inputs);
			if (RawScore >= 0.f)
			{
				SmoothedScore = (1.f - EmaAlpha) * SmoothedScore + EmaAlpha * RawScore;
				NewTier = TierForScore(SmoothedScore);
			}
			else
			{
				NewTier = CurrentTier < 0 ? 0 : CurrentTier;
			}
		}
		break;
	}

	if (NewTier != CurrentTier)
	{
		ApplyTier(NewTier);
	}
}

void UQOSBenchSubsystem::ApplyTier(int32 Tier)
{
	Tier = FMath::Clamp(Tier, 0, 3);
	for (const FKnob& Knob : Knobs)
	{
		if (ActiveKnobs.Contains(Knob.Name))
		{
			Exec(Knob.Commands[Tier]);
		}
	}
	if (CurrentTier >= 0)
	{
		++TierChanges;
		bTierChangedThisFrame = true;
	}
	CurrentTier = Tier;
}

void UQOSBenchSubsystem::Exec(const FString& Command) const
{
	if (GEngine)
	{
		GEngine->Exec(GetWorld(), *Command);
	}
}

void UQOSBenchSubsystem::OpenCsv()
{
	IFileManager::Get().MakeDirectory(*OutDir, true);
	CsvPath = FPaths::Combine(OutDir, FString::Printf(TEXT("%s_run%s.csv"), *ArmName, *RunLabel));
	Csv.Reset(IFileManager::Get().CreateFileWriter(*CsvPath));
	if (!Csv.IsValid())
	{
		UE_LOG(LogQOSBench, Error, TEXT("cannot open %s"), *CsvPath);
		return;
	}
	FString Header = TEXT("frame,time_s,frame_time_ms,game_ms,render_ms,draw_calls,primitives,score,smoothed,tier,tier_changed,active_lights,active_meshes");
	for (int32 i = 0; i < FQOSSample::NumCanonical; ++i)
	{
		Header += FString::Printf(TEXT(",%s"), FQOSSample::CanonicalName(i));
	}
	Header += TEXT(",trace_cm,rendered_actors,pool_required_mb,pool_size_mb,cam_x,cam_y,cam_z\n");
	const auto Ansi = StringCast<ANSICHAR>(*Header);
	Csv->Serialize(const_cast<ANSICHAR*>(Ansi.Get()), Ansi.Length());
	Csv->Flush();
	UE_LOG(LogQOSBench, Display, TEXT("logging frames to %s"), *CsvPath);
}

void UQOSBenchSubsystem::WriteCsvRow(float DeltaTime)
{
	if (!Csv.IsValid())
	{
		return;
	}
	const FVector Pos = Camera ? Camera->GetActorLocation() : FVector::ZeroVector;
	const FQOSSample& S = LastSample;
	const FString Row = FString::Printf(
		TEXT("%lld,%.3f,%.3f,%.3f,%.3f,%d,%d,%.4f,%.4f,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.0f,%d,%.0f,%.0f,%.0f,%.0f,%.0f\n"),
		FrameIndex, Elapsed, DeltaTime * 1000.f,
		FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
		S.RawDrawCalls, S.RawPrimitives, RawScore, SmoothedScore, CurrentTier, bTierChangedThisFrame ? 1 : 0, ActiveLights, ActiveMeshes,
		S.Proximity, S.ObjectDensity, S.VramPressure, S.LightCount, S.PawnCount, S.DrawCalls, S.CameraSpeed, S.FrameLoad,
		S.TraceDistanceCm, S.RenderedActors, S.PoolRequiredMB, S.PoolSizeMB, Pos.X, Pos.Y, Pos.Z);
	const auto Ansi = StringCast<ANSICHAR>(*Row);
	Csv->Serialize(const_cast<ANSICHAR*>(Ansi.Get()), Ansi.Length());
	if ((FrameIndex & 127) == 0)
	{
		Csv->Flush();
	}
}

void UQOSBenchSubsystem::Finish()
{
	if (Csv.IsValid())
	{
		Csv->Close();
		Csv.Reset();
	}
	UE_LOG(LogQOSBench, Display, TEXT("QOS bench done: arm=%s run=%s frames=%lld tier_changes=%d csv=%s"),
	       *ArmName, *RunLabel, FrameIndex, TierChanges, *CsvPath);
	Phase = EPhase::Done;
	bActive = false;
	FPlatformMisc::RequestExit(false, TEXT("QOSBench"));
}

void UQOSBenchSubsystem::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return;
	}

	switch (Phase)
	{
	case EPhase::Setup:
		if (SetupScene())
		{
			Phase = EPhase::Warmup;
			Elapsed = 0.f;
		}
		return;

	case EPhase::Warmup:
		Elapsed += DeltaTime;
		// full stress during warmup so shaders and PSOs compile before measuring
		UpdateStress(Duration / (2.f * StressWaves));
		UpdateCamera(0.f, DeltaTime);
		EvalTimer += DeltaTime;
		if (EvalTimer >= EvalInterval)
		{
			EvalTimer = 0.f;
			EvaluatePolicy(DeltaTime);
		}
		if (Elapsed >= Warmup)
		{
			Phase = EPhase::Running;
			Elapsed = 0.f;
			FrameIndex = 0;
			UpdateStress(0.f);
			UE_LOG(LogQOSBench, Display, TEXT("warmup over, measuring %s for %.0fs"), *ArmName, Duration);
		}
		return;

	case EPhase::Running:
		Elapsed += DeltaTime;
		++FrameIndex;
		UpdateStress(Elapsed);
		UpdateCamera(Elapsed, DeltaTime);
		EvalTimer += DeltaTime;
		if (EvalTimer >= EvalInterval)
		{
			EvalTimer = 0.f;
			EvaluatePolicy(DeltaTime);
		}
		WriteCsvRow(DeltaTime);
		bTierChangedThisFrame = false;
		if (Elapsed >= Duration)
		{
			Finish();
		}
		return;

	default:
		return;
	}
}
