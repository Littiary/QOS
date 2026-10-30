#include "QOSTelemetry.h"

#include "CollisionQueryParams.h"
#include "Components/LightComponent.h"
#include "ContentStreaming.h"
#include "DynamicRHI.h"
#include "Engine/Light.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "RHI.h"
#include "RHIStats.h"

const TCHAR* FQOSSample::CanonicalName(int32 Index)
{
	static const TCHAR* Names[NumCanonical] = {
		TEXT("proximity"), TEXT("object_density"), TEXT("vram_pressure"), TEXT("light_count"),
		TEXT("pawn_count"), TEXT("draw_calls"), TEXT("camera_speed"), TEXT("frame_load") };
	return (Index >= 0 && Index < NumCanonical) ? Names[Index] : TEXT("?");
}

TArray<float> FQOSSample::AsVector(int32 Count) const
{
	const float All[NumCanonical] = { Proximity, ObjectDensity, VramPressure, LightCount,
	                                  PawnCount, DrawCalls, CameraSpeed, FrameLoad };
	TArray<float> Out;
	const int32 N = FMath::Clamp(Count, 0, NumCanonical);
	Out.Append(All, N);
	return Out;
}

static float Normalize(float Value, float Max)
{
	return Max > 0.f ? FMath::Clamp(Value / Max, 0.f, 1.f) : 0.f;
}

FQOSSample FQOSTelemetry::Sample(UWorld* World, const FVector& CameraPos, const FRotator& CameraRot,
                                 float CameraSpeedCmPerSec, float LastFrameMs) const
{
	FQOSSample S;
	if (!World)
	{
		return S;
	}

	// 1. proximity: how close is the nearest surface along the view direction
	{
		FHitResult Hit;
		const FVector End = CameraPos + CameraRot.Vector() * Config.MaxTraceDistanceCm;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QOSProximity), false);
		if (World->LineTraceSingleByChannel(Hit, CameraPos, End, ECC_Visibility, Params))
		{
			S.TraceDistanceCm = Hit.Distance;
			S.Proximity = 1.f - Normalize(Hit.Distance, Config.MaxTraceDistanceCm);
		}
	}

	// 2, 4, 5. one pass over the actors: rendered density, movable lights, pawns
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->WasRecentlyRendered(0.1f))
		{
			++S.RenderedActors;
		}
		if (const ALight* Light = Cast<ALight>(Actor))
		{
			const ULightComponent* LC = Light->GetLightComponent();
			if (LC && LC->Mobility == EComponentMobility::Movable && LC->IsVisible())
			{
				++S.MovableLights;
			}
		}
		else if (Actor->IsA<APawn>())
		{
			++S.Pawns;
		}
	}
	S.LightCount = Normalize(static_cast<float>(S.MovableLights), Config.MaxMovableLights);
	S.PawnCount = Normalize(static_cast<float>(S.Pawns), Config.MaxPawns);

	// 3. VRAM pressure: texture streaming demand vs pool, with a total VRAM fallback. With texture streaming off (Movie
	// Render Queue turns it off) there is no streaming manager: asking for it asserts.
	{
		const bool bStreaming = IStreamingManager::Get().IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::None);
		const int64 Pool = bStreaming ? IStreamingManager::Get().GetRenderAssetStreamingManager().GetPoolSize() : 0;
		const int64 Required = bStreaming ? IStreamingManager::Get().GetRenderAssetStreamingManager().GetRequiredPoolSize() : 0;
		if (Pool > 0)
		{
			S.PoolSizeMB = static_cast<float>(Pool) / (1024.f * 1024.f);
			S.PoolRequiredMB = static_cast<float>(Required) / (1024.f * 1024.f);
			S.VramPressure = FMath::Clamp(static_cast<float>(Required) / static_cast<float>(Pool), 0.f, 1.f);
		}

		FTextureMemoryStats Stats;
		RHIGetTextureMemoryStats(Stats);
		if (Stats.TotalGraphicsMemory > 0)
		{
			const float Used = static_cast<float>(Stats.StreamingMemorySize + Stats.NonStreamingMemorySize)
			                 / static_cast<float>(Stats.TotalGraphicsMemory);
			S.VramPressure = FMath::Max(S.VramPressure, FMath::Clamp(Used, 0.f, 1.f));
		}
	}

	// 6. draw calls (last rendered frame)
	S.RawDrawCalls = GNumDrawCallsRHI[0];
	S.RawPrimitives = GNumPrimitivesDrawnRHI[0];
	S.ObjectDensity = Normalize(static_cast<float>(S.RawPrimitives), Config.MaxPrimitives);
	S.DrawCalls = Normalize(static_cast<float>(S.RawDrawCalls), Config.MaxDrawCalls);

	// 7. camera speed, 8. frame load
	S.RawCameraSpeed = CameraSpeedCmPerSec;
	S.CameraSpeed = Normalize(CameraSpeedCmPerSec, Config.MaxCameraSpeed);
	S.FrameLoad = Normalize(LastFrameMs, Config.TargetFrameMs);

	return S;
}
