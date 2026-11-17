// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBUIText.h"
#include "TBHUD.h"
#include "TBPaperStyle.h"
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
#include "Math/RandomStream.h"

namespace
{
	constexpr double DokiSeconds = .8;
}

// 見られ方はおもちゃ本人のPCにしか届かないので、相手の画面には何も出ない。
void ATBHUD::DrawGazeEffects(ATBController& Controller)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY, S = FMath::Min(W / 1280.f, H / 800.f);
	const float Short = FMath::Min(W, H);
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	const double Now = GetWorld()->GetTimeSeconds();
	const auto* Pawn = Controller.GetPawn<ATBCharacter>();
	const auto* Player = Controller.GetPlayerState<ATBPlayerState>();
	// 色と濃さはルールデータの「凝視|演出」で変えられる。
	const auto& Rules = TB::Rules().Gaze;
	using TBRuleMath::EGazeState;
	EGazeState State = EGazeState::None;
	if (Pawn && Player && Player->Team == ETBTeam::Toy)
	{
		State = Pawn->IsGazeFrozen() ? EGazeState::Frozen
		        : Pawn->IsGazed()    ? EGazeState::Gazed
		        : Pawn->IsSeen()     ? EGazeState::Seen
		                             : EGazeState::None;
	}
	// おもちゃ: 見られている間は端にうっすら、凝視されたら一気に影が寄る。
	const bool Gazed = State == EGazeState::Gazed;
	GazeShadow = TBRuleMath::AdvanceShade(GazeShadow, TBRuleMath::GazeShadowTarget(State), DeltaSeconds,
	                                      Gazed ? .12f : .5f, .4f);
	if (GazeShadow > 0.f)
	{
		// 見られているだけの間は、細い影をゆっくり脈打たせて気づかせる。凝視されたら太く濃く寄せきる。
		const float Pulse = State == EGazeState::Seen ? .8f + .2f * FMath::Sin(float(Now) * 4.f) : 1.f;
		// 見られているとき（濃さ0.5）の色から、凝視されて寄りきったとき（1）の色へ移す。
		const FLinearColor Color =
		    FMath::Lerp(Rules.SeenShadowColor, Rules.GazedShadowColor, FMath::Clamp(GazeShadow * 2.f - 1.f, 0.f, 1.f));
		DrawEdgeShade(Color, Short * .4f * GazeShadow,
		              FMath::Min(1.f, GazeShadow * 1.4f) * .85f * Pulse * Rules.ToyShadowStrength);
	}
	// 凝視は0.3秒と短く、通信の間隔しだいで「止まっている」から届くこともあるので、どちらに入っても一度だけ鳴らす。
	const bool Caught = Gazed || State == EGazeState::Frozen;
	if (Caught && !bWasCaught)
	{
		DokiStarted = Now;
		if (auto* Audio = GetGameInstance()->GetSubsystem<UTBAudioSubsystem>())
		{
			Audio->PlaySoundEvent(ETBSoundEvent::GazeAlert);
		}
	}
	bWasCaught = Caught;
	// 「ドキッ」は飛び出して少し震え、薄れて消える。
	if (DokiStarted >= 0 && Now - DokiStarted < DokiSeconds)
	{
		const float T = float(Now - DokiStarted);
		const float Scale = 3.2f * S * (1.f + .4f * FMath::Max(0.f, 1.f - T / .12f));
		const float Shake = FMath::Sin(T * 70.f) * 5.f * S * FMath::Max(0.f, 1.f - T / .4f);
		const float Fade = FMath::Clamp(float(DokiSeconds - T) / .3f, 0.f, 1.f);
		float TextW = 0, TextH = 0;
		GetTextSize(TBText::Literal(TEXT("Root.Gaze.Caught")), TextW, TextH, PaperFont, Scale);
		// 暗い部屋でも読めるよう、紙の札に書く。
		const float PadX = 22.f * S, PadY = 10.f * S;
		const float X = W * .5f - TextW * .5f + Shake, Y = H * .26f;
		FLinearColor Paper = TBPaper::Cream, Ink = TBPaper::Ink;
		Paper.A = Ink.A = Fade;
		PaperPanel(Paper, X - PadX, Y - PadY, TextW + PadX * 2, TextH * .8f + PadY * 2);
		DrawText(TBText::Literal(TEXT("Root.Gaze.Caught")), Ink, X, Y, PaperFont, Scale);
	}
	// 人間: 凝視中は端を暗くして見える範囲を絞り、中心へ向かう集中線を薄く出す。
	if (Pawn && Pawn->IsHuman())
	{
		const float Zoom = FMath::SmoothStep(0.f, 1.f, Pawn->GetGazeZoom());
		if (Zoom > 0.f)
		{
			DrawEdgeShade(Rules.HumanEdgeColor, Short * .38f * Zoom, .86f * Zoom * Rules.HumanEdgeStrength);
			// HUD Canvas is already the camera's constrained view rectangle. End every stroke
			// inside it: a half-diagonal circle extended beyond the view and painted over black bars.
			// Stable angles with gentle breathing avoid replacing the whole pattern every .07 seconds.
			FRandomStream Lines(54821);
			const FVector2D Focus(W * .5f, H * .53f);
			const FVector2D ClearRadius(W * .34f, H * .30f);
			const float HeaderBottom = FMath::Min(120.f * S, H * .24f);
			constexpr int32 Count = 36;
			for (int32 I = 0; I < Count; ++I)
			{
				const float Angle = (I + Lines.FRandRange(-.26f, .26f)) * UE_TWO_PI / Count;
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				const FVector2D Across(-Direction.Y, Direction.X);
				const bool Accent = I % 4 == 0;
				const float HalfWidth = Lines.FRandRange(Accent ? 2.8f : .9f, Accent ? 4.1f : 1.9f) * S;
				const float Phase = Lines.FRandRange(0.f, UE_TWO_PI);
				const float Breath = .82f + .18f * FMath::Sin(float(Now) * 2.1f + Phase);
				const float DesiredLength = Short * Lines.FRandRange(Accent ? .16f : .09f, Accent ? .23f : .17f) * Zoom;
				// Leave room for the widest part and the one-pixel antialias fringe at every edge.
				const float Inset = HalfWidth + 2.f;
				const float DX = Direction.X >= 0 ? W - Inset - Focus.X : Focus.X - Inset;
				const float DY = Direction.Y >= 0 ? H - Inset - Focus.Y : Focus.Y - Inset;
				const float Outer = FMath::Min(DX / FMath::Max(.001f, float(FMath::Abs(Direction.X))),
				    DY / FMath::Max(.001f, float(FMath::Abs(Direction.Y))));
				const float Clear = 1.f / FMath::Sqrt(FMath::Square(float(Direction.X / ClearRadius.X)) +
				    FMath::Square(float(Direction.Y / ClearRadius.Y)));
				const float Length = FMath::Min(DesiredLength * (.92f + .08f * Breath), Outer - Clear - HalfWidth);
				if (Length <= HalfWidth * 3.f) continue;
				const FVector2D Base = Focus + Direction * Outer;
				const FVector2D Shoulder = Base - Direction * (Length * .18f);
				const FVector2D Tip = Base - Direction * Length;
				// Keep the objective, timer and counters clear, even where a diagonal would cross them.
				if (FMath::Min(Base.Y, Tip.Y) - HalfWidth < HeaderBottom) continue;
				FLinearColor Color = FMath::Lerp(TBPaper::Cream, TBPaper::Paper, .35f);
				Color.A = FMath::Clamp(Rules.SpeedLineOpacity * Zoom * Breath * (Accent ? 1.5f : .85f), 0.f, 1.f);
				DrawSmoothPolygon({Base - Across * (HalfWidth * .65f), Base + Across * (HalfWidth * .65f),
				    Shoulder + Across * HalfWidth, Tip, Shoulder - Across * HalfWidth}, Color);
			}
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
