// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBHUD.h"
#include "UI/TBGameViewportClient.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
#include "Core/TBLobbySettings.h"
#include "TBPaperStyle.h"
#include "TBUIText.h"
#include "TBOptionsMenu.h"
#include "Audio/TBAudioSubsystem.h"
#include "Character/TBCharacter.h"
#include "Core/TBPlayerState.h"
#include "Core/TBGameState.h"
#include "Core/TBController.h"
#include "Core/TBSession.h"
#include "Core/TBGameHelpers.h"
#include "GamePlay/TBBox.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

void ATBHUD::NotifyHitBoxClick(FName Name)
{
	if (UTBLoadingScreenSubsystem::IsCovering(GetGameInstance())) return;
	Super::NotifyHitBoxClick(Name);
	LastClickedButton = Name;
	LastButtonClickTime = GetWorld()->GetTimeSeconds();
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	const auto* State = TB::GS(GetWorld());
	if (!PC || !State)
	{
		return;
	}
	// The shared options window owns input until it closes; preserve the stage draft underneath.
	if (Options.IsValid() && Options->IsInViewport()) return;
	if (auto* Audio = PC->GetGameInstance()->GetSubsystem<UTBAudioSubsystem>())
	{
		if (Name != TEXT("Ready") && Name != TEXT("Child") && Name != TEXT("Toy") && Name != TEXT("Either") &&
		    Name != TEXT("MoreChildren") && Name != TEXT("FewerChildren") && Name != TEXT("MoreToys") && Name != TEXT("FewerToys") &&
		    Name != TEXT("LessTime") && Name != TEXT("MoreTime") && Name != TEXT("FewerBatteries") && Name != TEXT("MoreBatteries"))
			Audio->PlaySoundEvent(Name == TEXT("Resume") || Name == TEXT("Leave") || Name == TEXT("CloseSettings") || Name == TEXT("CancelStage") ? ETBSoundEvent::UIBack : ETBSoundEvent::UIClick);
	}
	if (bConfirmingLeave)
	{
		if (Name == TEXT("CancelLeave")) bConfirmingLeave = false;
		else if (Name == TEXT("ConfirmLeave") && !State->bTravelPending)
		{
			bConfirmingLeave = false;
			PC->TBLeave();
		}
		return;
	}
	if (HandleSafetyClick(Name)) return;
	if (Name == TEXT("OpenOptions"))
	{
		Options = UTBOptionsMenu::Open(PC);
		return;
	}
	if (Name == TEXT("Resume"))
	{
		PC->ToggleLeaveMenu();
		return;
	}
	if (Name == TEXT("Leave") && !State->bTravelPending && (State->Phase == ETBPhase::Lobby || PC->IsLeaveMenuOpen()))
	{
		bConfirmingLeave = true;
		return;
	}
	if (Name == TEXT("RetryLobbyReturn") && State->IsLobbyReturnStopped() && PC->HasAuthority())
	{
		PC->TBLobby();
		return;
	}
	if (State->Phase != ETBPhase::Lobby)
	{
		return;
	}
	if (HandleLobbyPanelClick(Name) || TBLobbySettings::HandleClick(PC, Name)) return;
	if (Name == TEXT("Ready"))
	{
		PC->TBReady();
	}
	else if (Name == TEXT("Start"))
	{
		PC->TBStart();
	}
	else if (Name == TEXT("Child"))
	{
		PC->TBPrefer(1);
	}
	else if (Name == TEXT("Toy"))
	{
		PC->TBPrefer(2);
	}
	else if (Name == TEXT("Either"))
	{
		PC->TBPrefer(0);
	}
	else if (Name == TEXT("MoreChildren") || Name == TEXT("FewerChildren"))
	{
		PC->TBRules(State->Settings.Humans + (Name == TEXT("MoreChildren") ? 1 : -1), State->Settings.Toys,
		            State->Settings.Duration, State->Settings.RequiredItems);
	}
	else if (Name == TEXT("MoreToys") || Name == TEXT("FewerToys"))
	{
		PC->TBRules(State->Settings.Humans, State->Settings.Toys + (Name == TEXT("MoreToys") ? 1 : -1),
		            State->Settings.Duration, State->Settings.RequiredItems);
	}
}

bool ATBHUD::DismissConfirmation()
{
	if (bConfirmingLeave)
	{
		bConfirmingLeave = false;
		return true;
	}
	if (bPlayersOpen)
	{
		NotifyHitBoxClick(KickSelection.IsValid() ? TEXT("CancelKick") : TEXT("ClosePlayers"));
		return true;
	}
	return false;
}

bool ATBHUD::IsChatBlocked() const
{
	// 設定の開閉はロビーを離れても残るので、ロビーの間だけ見る。
	const auto* State = TB::GS(GetWorld());
	return bConfirmingLeave || bPlayersOpen || (bLobbySettingsOpen && State && State->Phase == ETBPhase::Lobby) ||
	       (Options.IsValid() && Options->IsInViewport());
}

// HUDは状態の表示だけを行う。勝敗や取得の判定はサーバー側で行う。
void ATBHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || UTBGameViewportClient::IsUIHidden(GetWorld()))
	{
		return;
	}
	const auto* State = TB::GS(GetWorld());
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	if (!State || !PC)
	{
		return;
	}
	if (State->Phase != ETBPhase::Lobby || State->bTravelPending)
	{
		bLobbySettingsOpen = false;
		bStagePickerOpen = false;
		DraftStage = INDEX_NONE;
		StageDraftNotice = NAME_None;
	}
	if (State->Phase == ETBPhase::Results || State->Phase == ETBPhase::Aborted ||
	    (State->Phase == ETBPhase::Playing && !PC->IsLeaveMenuOpen()))
	{
		bConfirmingLeave = false;
		bPlayersOpen = false;
		KickSelection.Reset();
		if (Options.IsValid() && Options->IsInViewport())
		{
			Options->Dismiss();
		}
	}
	if (State->bTravelPending) bConfirmingLeave = false;
	if (bConfirmingLeave)
	{
		const bool Host = PC->HasAuthority();
		DrawConfirmation(TBText::String(Host ? TEXT("Root.Room.ConfirmClose") : TEXT("Root.Room.ConfirmLeave")),
		    Host ? TBText::String(TEXT("Root.Room.CloseDetail")) : FString(), TEXT("CancelLeave"), TEXT("ConfirmLeave"),
		    TBText::String(Host ? TEXT("Root.Room.Close") : TEXT("Stage.Leave")));
		return;
	}
	if (bPlayersOpen)
	{
		DrawPlayers();
		return;
	}
	if (State->Phase == ETBPhase::Lobby)
	{
		DrawLobby();
		return;
	}
	if (State->Phase == ETBPhase::Results || State->Phase == ETBPhase::Aborted)
	{
		DrawResults(*State);
		DrawResultCover(*State);
	}
	else
	{
		DrawMatch(*State, *PC);
		if (PC->IsLeaveMenuOpen()) DrawPause();
	}
}

void ATBHUD::DrawPause()
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float X = Canvas->ClipX * .5f - 360 * S, Y = Canvas->ClipY * .5f - 344 * S;
	DrawRect(FLinearColor(0, 0, 0, .48f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	FramePanel(X, Y, 720 * S, 688 * S);
	// The illustrated header's cloud is separate from its title so every language fits on the same art.
	const FLinearColor Sky = FLinearColor::FromSRGBColor(FColor(92, 131, 150));
	const FLinearColor Rim = FLinearColor::FromSRGBColor(FColor(98, 67, 49));
	const FLinearColor Ivory = FLinearColor::FromSRGBColor(FColor(255, 241, 213));
	static const TArray<FVector2D> CloudEdge = []
	{
		const FVector2D Curves[][2] = {
		    {{202, 65}, {230, 55}}, {{246, 47}, {259, 52}}, {{281, 23}, {321, 39}},
		    {{357, 14}, {394, 43}}, {{423, 34}, {442, 57}}, {{468, 58}, {477, 82}},
		    {{486, 110}, {460, 123}}, {{446, 134}, {421, 127}}, {{390, 149}, {345, 131}},
		    {{307, 154}, {267, 131}}, {{235, 140}, {214, 124}}, {{193, 112}, {202, 83}}};
		TArray<FVector2D> Edge;
		FVector2D Start(202, 83);
		for (const auto& Curve : Curves)
		{
			for (int32 Step = 0; Step < 8; ++Step)
			{
				const double T = Step / 8.0, U = 1.0 - T;
				Edge.Add(U * U * Start + 2.0 * U * T * Curve[0] + T * T * Curve[1]);
			}
			Start = Curve[1];
		}
		return Edge;
	}();
	auto CloudLayer = [&](float Inset, FLinearColor Color)
	{
		TArray<FVector2D> Points;
		const int32 Count = CloudEdge.Num();
		Points.Reserve(Count);
		for (int32 I = 0; I < Count; ++I)
		{
			const FVector2D Tangent = (CloudEdge[(I + 1) % Count] - CloudEdge[(I + Count - 1) % Count]).GetSafeNormal();
			Points.Add(FVector2D(X, Y) + (CloudEdge[I] + FVector2D(-Tangent.Y, Tangent.X) * Inset) * S);
		}
		DrawSmoothPolygon(Points, Color);
	};
	RoundedPanel(Ivory, X + 252 * S, Y + 125 * S, 22 * S, 22 * S, 11, 3, Rim);
	CloudLayer(0, Rim);
	CloudLayer(3.5f, Ivory);
	CloudLayer(10, Sky);
	RoundedPanel(Ivory, X + 226 * S, Y + 69 * S, 45 * S, 45 * S, 22.5f, 0);
	for (int32 I = 0; I < 2; ++I)
		RoundedPanel(Sky, X + (238 + I * 12) * S, Y + 80 * S, 7 * S, 22 * S, 2, 0);
	DrawFittedText(TBText::String(TEXT("Canvas.PauseTitle")), Ivory,
	    X + 277 * S, Y + 77 * S, 150 * S, 1.9f * S, true, true);
	DrawArt(TEXT("PauseHeader"), X + 414 * S, Y + 70 * S, 95 * S, 74 * S);
	for (int32 I = 0; I < 3; ++I)
		DrawText(TEXT("Z"), Ivory, X + (423 + 13 * I) * S, Y + (58 - 11 * I) * S,
		    HeadingFont, (.70f + .25f * I) * S, false, true);
	DrawFittedText(TBText::Literal(TEXT("Canvas.OnlineContinues")), TBPaper::Muted,
	    X + 145 * S, Y + 164 * S, 430 * S, 1.05f * S, true, true);
	DrawArt(TEXT("Underline_Coral"), X + 158 * S, Y + 187 * S, 370 * S, 13 * S);
	float MX = 0, MY = 0;
	GetOwningPlayerController()->GetMousePosition(MX, MY);
	const FName Ids[] = {TEXT("Unstuck"), TEXT("Resume"), TEXT("Players")};
	const TCHAR* First[] = {TBText::Literal(TEXT("Canvas.ResetLine1")), TBText::Literal(TEXT("Canvas.ResumeLine1")), TBText::Literal(TEXT("Canvas.PlayersLine1"))};
	const TCHAR* Second[] = {TBText::Literal(TEXT("Canvas.ResetLine2")), TBText::Literal(TEXT("Canvas.ResumeLine2")), TBText::Literal(TEXT("Canvas.PlayersLine2"))};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float CX = X + (37 + Index * 216) * S, CY = Y + 253 * S;
		const bool Hover = MX >= CX && MX < CX + 198 * S && MY >= CY && MY < CY + 242 * S;
		const float Lift = Hover ? 4 * S : 0;
		DrawArt(Hover ? TEXT("Card_HangingActive") : TEXT("Card_Hanging"), CX, CY - Lift, 198 * S, 242 * S);
		DrawArt(TEXT("Tape_Brown"), CX + 86 * S, CY - 28 * S - Lift, 28 * S, 56 * S);
		if (Index == 0)
		{
			// Clockwise loop and arrowhead: the reset-position action, independent of any key binding.
			const FVector2D Center(CX + 99 * S, CY + 77 * S - Lift);
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const FLinearColor Color = Pass == 0 ? TBPaper::Outline : TBPaper::Gold;
				const float Thickness = (Pass == 0 ? 8 : 4) * S;
				constexpr int32 Segments = 48;
				constexpr float Feather = .65f;
				const float Radius = 23 * S, AngleFeather = Feather / Radius;
				const float Start = -135.f * PI / 180.f, End = 153.f * PI / 180.f;
				const float Radii[] = {Radius - Thickness * .5f - Feather, Radius - Thickness * .5f + Feather,
				    Radius + Thickness * .5f - Feather, Radius + Thickness * .5f + Feather};
				TArray<FVector2D, TInlineAllocator<51>> Directions;
				for (int32 Step = 0; Step <= Segments + 2; ++Step)
				{
					const float Angle = Step == 0 ? Start - AngleFeather : Step == 1 ? Start + AngleFeather :
					    Step == Segments + 1 ? End - AngleFeather : Step == Segments + 2 ? End + AngleFeather :
					    FMath::Lerp(Start, End, float(Step - 1) / Segments);
					Directions.Emplace(FMath::Cos(Angle), FMath::Sin(Angle));
				}
				TArray<FCanvasUVTri> Triangles;
				Triangles.Reserve((Segments + 2) * 6);
				auto Position = [&](int32 Step, int32 Band)
				{ return Center + Directions[Step] * Radii[Band]; };
				auto Coverage = [&](int32 Step, int32 Band)
				{ return Color.CopyWithNewOpacity(Step == 0 || Step == Segments + 2 || Band == 0 || Band == 3 ? 0.f : Color.A); };
				auto Add = [&](int32 AStep, int32 ABand, int32 BStep, int32 BBand, int32 CStep, int32 CBand)
				{
					FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
					Triangle.V0_Pos = Position(AStep, ABand); Triangle.V0_Color = Coverage(AStep, ABand);
					Triangle.V1_Pos = Position(BStep, BBand); Triangle.V1_Color = Coverage(BStep, BBand);
					Triangle.V2_Pos = Position(CStep, CBand); Triangle.V2_Color = Coverage(CStep, CBand);
					Triangle.V0_UV = Triangle.V1_UV = Triangle.V2_UV = FVector2D::ZeroVector;
				};
				// Shared radial edges prevent seams between segments; only the stroke perimeter fades.
				for (int32 Segment = 0; Segment < Segments + 2; ++Segment)
					for (int32 Band = 0; Band < 3; ++Band)
					{
						Add(Segment, Band, Segment + 1, Band, Segment + 1, Band + 1);
						Add(Segment, Band, Segment + 1, Band + 1, Segment, Band + 1);
					}
				FCanvasTriangleItem Arc(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, GWhiteTexture);
				Arc.TriangleList = MoveTemp(Triangles);
				Arc.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Arc);
			}
			auto ArrowStroke = [&](FVector2D A, FVector2D B)
			{
				const FVector2D Direction = (B - A).GetSafeNormal();
				const FVector2D Side(Direction.Y * 3.5f * S, -Direction.X * 3.5f * S);
				DrawSmoothPolygon({A - Side, B - Side, B + Side, A + Side}, TBPaper::Outline);
			};
			ArrowStroke(Center + FVector2D(-22, -20) * S, Center + FVector2D(-23, -3) * S);
			ArrowStroke(Center + FVector2D(-23, -3) * S, Center + FVector2D(-5, -8) * S);
		}
		else if (Index == 1)
		{
			const FVector2D Origin(CX + 78 * S, CY + 52 * S - Lift);
			const FVector2D Outline[] = {{2, 4}, {3, 1}, {6, 0}, {9, 1}, {43, 22}, {46, 25}, {46, 28},
			    {43, 31}, {9, 53}, {6, 54}, {3, 53}, {2, 50}};
			auto PlayMark = [&](float Scale, FLinearColor Color)
			{
				TArray<FVector2D> Points;
				const FVector2D Center(17, 27);
				for (const FVector2D& Point : Outline)
					Points.Add(Origin + (Center + (Point - Center) * Scale) * S);
				DrawSmoothPolygon(Points, Color);
			};
			PlayMark(1, TBPaper::Outline);
			PlayMark(.77f, Ivory);
		}
		else DrawArt(TEXT("Icon_PlayerRatio"), CX + 58 * S, CY + 50 * S - Lift, 82 * S, 70 * S);
		DrawFittedText(First[Index], TBPaper::Ink, CX + 16 * S, CY + 139 * S - Lift, 166 * S, 1.6f * S, true, true);
		DrawFittedText(Second[Index], TBPaper::Ink, CX + 16 * S, CY + 174 * S - Lift, 166 * S, 1.6f * S, true, true);
		if (Index == 0) DrawFittedText(TBText::Literal(TEXT("Canvas.ResetWait")), TBPaper::Muted, CX + 16 * S, CY + 211 * S - Lift, 166 * S, .68f * S, true, true);
		if (Index == 1) DrawKeycap(TEXT("Esc"), CX + 116 * S, CY + 198 * S - Lift, 57 * S, 26 * S);
		AddHitBox(FVector2D(CX, CY), FVector2D(198 * S, 242 * S), Ids[Index], true);
	}
	auto IconButton = [&](FName Id, FName Art, float PX)
	{
		const float IX = X + PX * S, IY = Y + 539 * S;
		const bool Hover = MX >= IX && MX < IX + 112 * S && MY >= IY && MY < IY + 112 * S;
		DrawArt(Art, IX, IY - (Hover ? 4 * S : 0), 112 * S, 112 * S,
		    Hover ? FLinearColor(1.15f, 1.15f, 1.15f, 1) : FLinearColor::White);
		AddHitBox(FVector2D(IX, IY), FVector2D(112 * S, 112 * S), Id, true);
	};
	IconButton(TEXT("OpenOptions"), TEXT("Button_Gear"), 392);
	IconButton(TEXT("Leave"), TEXT("Button_Leave"), 534);
}

// 通常HUDに内部状態だけを追加する。ロビー・結果画面の操作は置き換えない。
void ATBHUD::DrawDebugHUD()
{
	const auto* State = TB::GS(GetWorld());
	if (!State)
	{
		return;
	}
	const float Scale = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	float Y = 155.f * Scale;
	auto Line = [this, Scale, &Y](const FString& Text)
	{
		DrawText(Text, FLinearColor::Yellow, 24.f * Scale, Y, PaperFont, Scale);
		Y += 22.f * Scale;
	};
	for (const APlayerState* Player : State->PlayerArray)
	{
		if (const auto* Info = Cast<ATBPlayerState>(Player))
		{
			Line(FString::Printf(TEXT("%s | Team:%d State:%d Frozen:%d"), *Info->GetPlayerName(), int32(Info->Team),
			                     int32(Info->ToyState), Info->bFrozen));
		}
	}
	const auto* Viewer =
	    GetOwningPlayerController() ? GetOwningPlayerController()->GetPlayerState<ATBPlayerState>() : nullptr;
	if (Viewer && Viewer->Team == ETBTeam::Toy && State->Box)
	{
		Line(FString::Printf(TEXT("Rescue: %.0f%% | Rescuers: %d"), State->Box->RescueProgress * 100,
		                     State->Box->Rescuers));
	}
	// おもちゃ本人にだけ見られ方を出す。人間には出さない。
	if (const auto* Toy = Cast<ATBCharacter>(GetOwningPawn()); Toy && Viewer && Viewer->Team == ETBTeam::Toy)
	{
		Line(FString::Format(TBText::Literal(TEXT("Canvas.DebugGaze")), {Toy->IsGazeFrozen() ? TBText::Literal(TEXT("Canvas.DebugFrozen"))
		                                    : Toy->IsGazed()      ? TBText::Literal(TEXT("Canvas.DebugGazed"))
		                                    : Toy->IsSeen()       ? TBText::Literal(TEXT("Canvas.DebugSeen"))
		                                                          : TBText::Literal(TEXT("Canvas.DebugNone"))}));
	}
}

void ATBHUD::NotifyHitBoxBeginCursorOver(FName BoxName)
{
    Super::NotifyHitBoxBeginCursorOver(BoxName);
    if (auto* PC = GetOwningPlayerController())
        if (auto* Audio = PC->GetGameInstance()->GetSubsystem<UTBAudioSubsystem>()) Audio->PlaySoundEvent(ETBSoundEvent::UIHover);
}
