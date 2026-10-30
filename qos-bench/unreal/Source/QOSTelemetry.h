#pragma once

#include "CoreMinimal.h"

/**
 * One telemetry sample. The eight normalized values follow the canonical QOS
 * input order used by qos-core configs, so a model trained on the first N of
 * them can be fed with AsVector(N).
 */
struct FQOSSample
{
	// normalized [0,1]
	float Proximity = 0.f;      // 1 = something is right in front of the camera
	float ObjectDensity = 0.f;  // primitives drawn / MaxPrimitives
	float VramPressure = 0.f;   // texture streaming demand / pool (or VRAM used / total)
	float LightCount = 0.f;     // movable lights / MaxMovableLights
	float PawnCount = 0.f;      // pawns / MaxPawns
	float DrawCalls = 0.f;      // RHI draw calls / MaxDrawCalls
	float CameraSpeed = 0.f;    // cm/s / MaxCameraSpeed
	float FrameLoad = 0.f;      // last frame ms / TargetFrameMs

	// raw values for the CSV
	float TraceDistanceCm = -1.f;
	int32 RenderedActors = 0;
	float PoolRequiredMB = 0.f;
	float PoolSizeMB = 0.f;
	int32 MovableLights = 0;
	int32 Pawns = 0;
	int32 RawDrawCalls = 0;
	int32 RawPrimitives = 0;
	float RawCameraSpeed = 0.f;

	static constexpr int32 NumCanonical = 8;
	static const TCHAR* CanonicalName(int32 Index);

	/** First Count canonical values, in order. */
	TArray<float> AsVector(int32 Count) const;
};

struct FQOSTelemetryConfig
{
	float MaxTraceDistanceCm = 5000.f;
	float MaxRenderedActors = 1500.f;
	float MaxPrimitives = 200000.f;
	float MaxMovableLights = 128.f;
	float MaxPawns = 64.f;
	float MaxDrawCalls = 5000.f;
	float MaxCameraSpeed = 2000.f;
	float TargetFrameMs = 33.3f;
};

class OTUKEN_API FQOSTelemetry
{
public:
	FQOSTelemetryConfig Config;

	FQOSSample Sample(UWorld* World, const FVector& CameraPos, const FRotator& CameraRot,
	                  float CameraSpeedCmPerSec, float LastFrameMs) const;
};
