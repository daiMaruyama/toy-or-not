// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBHUD.h"
#include "Audio/TBAudioSubsystem.h"
#include "Character/TBCharacter.h"
#include "Core/TBController.h"
#include "Core/TBPlayerState.h"
#include "Data/TBRuleData.h"
#include "GamePlay/TBRuleMath.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
	// A rounded two-beat envelope, with no discontinuity when the cycle wraps.
	float HeartbeatPulse(float Phase)
	{
		auto Beat = [Phase](float Center, float Radius)
		{
			const float T = FMath::Clamp(1.f - FMath::Abs(Phase - Center) / Radius, 0.f, 1.f);
			return T * T * (3.f - 2.f * T);
		};
		return Beat(.16f, .16f) + .55f * Beat(.43f, .13f);
	}
}

// 見られ方はおもちゃ本人のPCにしか届かないので、相手の画面には何も出ない。
void ATBHUD::DrawGazeEffects(ATBController& Controller)
{
	const float Short = FMath::Min(Canvas->ClipX, Canvas->ClipY);
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	auto* Pawn = Controller.GetPawn<ATBCharacter>();
	const auto* Player = Controller.GetPlayerState<ATBPlayerState>();
	const bool Toy = Pawn && Player && Player->Team == ETBTeam::Toy;
	// A different pawn or role must not inherit the previous toy's peripheral effect.
	if (GazeEffectPawn.Get() != Pawn || !Toy)
	{
		GazeEffectPawn = Pawn;
		GazeShadow = GazePulsePhase = 0.f;
		bWasCaught = false;
	}
	// 色と濃さはルールデータの「凝視|演出」で変えられる。
	const auto& Rules = TB::Rules().Gaze;
	using TBRuleMath::EGazeState;
	EGazeState State = EGazeState::None;
	if (Toy)
	{
		State = Pawn->IsGazeFrozen() ? EGazeState::Frozen
		        : Pawn->IsGazed()    ? EGazeState::Gazed
		        : Pawn->IsSeen()     ? EGazeState::Seen
		                             : EGazeState::None;
	}
	// 凝視は0.3秒と短く、通信の間隔しだいで「止まっている」から届くこともあるので、どちらに入っても一度だけ鳴らす。
	const bool Gazed = State == EGazeState::Gazed;
	const bool Caught = Gazed || State == EGazeState::Frozen;
	if (Caught && !bWasCaught)
	{
		GazePulsePhase = 0.f;
		if (auto* Audio = GetGameInstance()->GetSubsystem<UTBAudioSubsystem>())
		{
			Audio->PlaySoundEvent(ETBSoundEvent::GazeAlert);
		}
	}
	bWasCaught = Caught;
	// おもちゃ: 文字を出さず、周辺の影の二拍の収縮で緊張を伝える。
	GazeShadow = TBRuleMath::AdvanceShade(GazeShadow, TBRuleMath::GazeShadowTarget(State), DeltaSeconds,
	                                      Gazed ? .12f : .5f, .4f);
	if (GazeShadow > 0.f)
	{
		const float Tension = FMath::Clamp(GazeShadow * 2.f - 1.f, 0.f, 1.f);
		GazePulsePhase = FMath::Fmod(
		    GazePulsePhase + FMath::Max(0.f, DeltaSeconds) * FMath::Lerp(.85f, 1.25f, Tension), 1.f);
		const float Pulse = HeartbeatPulse(GazePulsePhase);
		// 見られているとき（濃さ0.5）の色から、凝視されて寄りきったとき（1）の色へ移す。
		const FLinearColor Color = FMath::Lerp(Rules.SeenShadowColor, Rules.GazedShadowColor, Tension);
		DrawEdgeShade(Color, Short * .4f * GazeShadow * (.92f + .08f * Pulse),
		              FMath::Min(1.f, GazeShadow * 1.4f) * .85f * (.78f + .22f * Pulse) * Rules.ToyShadowStrength);
	}
	else
	{
		GazePulsePhase = 0.f;
	}
	// 人間: 白い線を重ねず、カメラのズームと滑らかな周辺の影で集中感を出す。
	if (Pawn && Pawn->IsHuman())
	{
		const float Zoom = FMath::SmoothStep(0.f, 1.f, Pawn->GetGazeZoom());
		if (Zoom > 0.f)
		{
			DrawEdgeShade(Rules.HumanEdgeColor, Short * .38f * Zoom, .86f * Zoom * Rules.HumanEdgeStrength);
		}
	}
}

// Interpolate opacity within each strip so the shade has no hard band boundaries.
void ATBHUD::DrawEdgeShade(FLinearColor Color, float Thickness, float Alpha)
{
	if (!Canvas || Thickness <= 0.f || Alpha <= 0.f) return;
	constexpr int32 Bands = 14;
	const float W = Canvas->ClipX, H = Canvas->ClipY, Band = Thickness / Bands;
	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Bands * 8);
	auto Quad = [&](FVector2D A, FVector2D B, FVector2D C, FVector2D D, float OuterAlpha, float InnerAlpha)
	{
		FCanvasUVTri& First = Triangles.AddDefaulted_GetRef();
		First.V0_Pos = A; First.V1_Pos = B; First.V2_Pos = C;
		First.V0_UV = First.V1_UV = First.V2_UV = FVector2D::ZeroVector;
		First.V0_Color = First.V1_Color = Color.CopyWithNewOpacity(OuterAlpha);
		First.V2_Color = Color.CopyWithNewOpacity(InnerAlpha);
		FCanvasUVTri& Second = Triangles.AddDefaulted_GetRef();
		Second.V0_Pos = A; Second.V1_Pos = C; Second.V2_Pos = D;
		Second.V0_UV = Second.V1_UV = Second.V2_UV = FVector2D::ZeroVector;
		Second.V0_Color = Color.CopyWithNewOpacity(OuterAlpha);
		Second.V1_Color = Second.V2_Color = Color.CopyWithNewOpacity(InnerAlpha);
	};
	for (int32 I = 0; I < Bands; ++I)
	{
		const float Outer = Band * I, Inner = Band * (I + 1);
		const float OuterAlpha = FMath::Min(1.f, Alpha * FMath::Pow(1.f - float(I) / Bands, 1.5f));
		const float InnerAlpha = FMath::Min(1.f, Alpha * FMath::Pow(1.f - float(I + 1) / Bands, 1.5f));
		Quad({0, Outer}, {W, Outer}, {W, Inner}, {0, Inner}, OuterAlpha, InnerAlpha);
		Quad({0, H - Outer}, {W, H - Outer}, {W, H - Inner}, {0, H - Inner}, OuterAlpha, InnerAlpha);
		Quad({Outer, 0}, {Outer, H}, {Inner, H}, {Inner, 0}, OuterAlpha, InnerAlpha);
		Quad({W - Outer, 0}, {W - Outer, H}, {W - Inner, H}, {W - Inner, 0}, OuterAlpha, InnerAlpha);
	}
	FCanvasTriangleItem Item(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, GWhiteTexture);
	Item.TriangleList = MoveTemp(Triangles);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}
