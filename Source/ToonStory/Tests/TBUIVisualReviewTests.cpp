// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/TBCharacter.h"
#include "Character/TBMovement.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Core/TBController.h"
#include "Core/TBChat.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBGameUserSettings.h"
#include "Core/TBPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Input/TBKeyLabels.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Tests/TBTestMatch.h"
#include "UI/TBHUD.h"
#include "UI/TBOptionsMenu.h"
#include "UI/TBOptionsPages.h"
#include "UI/TBPaperStyle.h"
#include "UI/TBTitleMenu.h"
#include "UI/TBUIText.h"
#include "UI/Loading/TBLoadingScreenSettings.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/IToolTip.h"

namespace
{
	UWorld* ReviewWorld()
	{
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World() &&
			    Context.World()->GetNetMode() == NM_Standalone)
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	// This is an opt-in screenshot fixture, not a screenshot-diff or network test.
	// Use a fresh standalone process at -ResX=1280 -ResY=800, then at 1920x1080.
	class FUIReview : public IAutomationLatentCommand
	{
	public:
		FUIReview(FAutomationTestBase* InTest, UWorld* InWorld, bool bInTitle)
		    : Test(InTest), World(InWorld), bTitle(bInTitle), Deadline(FPlatformTime::Seconds() + 180)
		{
			OldLanguage = TBText::GetLanguage();
			bRosterOnly = !bTitle && FParse::Param(FCommandLine::Get(), TEXT("UIReviewRosterOnly"));
			if (auto* Settings = UTBGameUserSettings::Get()) OldSavedLanguage = Settings->GetLanguage();
			FString RequestedLanguage;
			if (FParse::Value(FCommandLine::Get(), TEXT("UIReviewLanguage="), RequestedLanguage))
			{
				Test->TestTrue(TEXT("Requested UI language activates"), TBText::SetLanguage(RequestedLanguage));
				if (auto* Settings = UTBGameUserSettings::Get()) Settings->SetLanguage(RequestedLanguage);
			}
			PC = Cast<ATBController>(InWorld->GetFirstPlayerController());
			State = InWorld->GetGameState<ATBGameState>();
			Mode = InWorld->GetAuthGameMode<ATBGameMode>();
			if (!bTitle && PC.IsValid() && State.IsValid())
			{
				Player = PC->GetPlayerState<ATBPlayerState>();
				HUD = Cast<ATBHUD>(PC->GetHUD());
				Body = PC->GetPawn<ATBCharacter>();
				OldViewTarget = PC->GetViewTarget();
				if (Body.IsValid())
				{
					if (const auto* Movement = Cast<UTBMovement>(Body->GetCharacterMovement()))
						bOldGazeRequested = Movement->bWantsToGaze;
					ReviewCamera = Body->Camera;
					if (ReviewCamera.IsValid())
					{
						OldCameraAspect = ReviewCamera->AspectRatio;
						bOldCameraConstrained = ReviewCamera->bConstrainAspectRatio;
					}
				}
				OldTeam = Player->Team;
				bOldReady = Player->bReady;
				OldSettings = State->Settings;
				OldWinner = State->Winner;
				OldStyle = State->ResultStyle;
				OldReason = State->Reason;
				OldCollected = State->Collected;
				bOldEscape = State->bEscapePhase;
				OldResultStart = State->ResultStartTime;
				bOldSettings = HUD->IsLobbySettingsOpen();
				OldRound = State->HidePreparation.Round;
				bOldModeTick = Mode.IsValid() && Mode->IsActorTickEnabled();
				if (Mode.IsValid()) Mode->SetActorTickEnabled(false);
			}
		}

		~FUIReview() override
		{
			RestoreTitleButton();
			RestoreChat();
			if (Options.IsValid()) Options->Dismiss();
			if (PC.IsValid())
			{
				PC->CloseChat();
				if (PC->IsLeaveMenuOpen()) PC->ToggleLeaveMenu();
			}
			if (State.IsValid() && !bTitle)
			{
				State->Winner = OldWinner;
				State->ResultStyle = OldStyle;
				State->Reason = OldReason;
				State->Collected = OldCollected;
				State->bEscapePhase = bOldEscape;
				State->ResultStartTime = OldResultStart;
				State->Settings = OldSettings;
			}
			Playing.Reset();
			if (Player.IsValid()) { Player->Team = OldTeam; Player->bReady = bOldReady; }
			if (Body.IsValid())
			{
				Body->SetGazeRequested(bOldGazeRequested);
				Body->ApplyState();
			}
			RestoreOverview();
			if (ReviewCamera.IsValid())
			{
				ReviewCamera->AspectRatio = OldCameraAspect;
				ReviewCamera->bConstrainAspectRatio = bOldCameraConstrained;
			}
			if (bChangedGazeView && PC.IsValid() && OldViewTarget.IsValid()) PC->SetViewTarget(OldViewTarget.Get());
			for (const auto& Dummy : Roster)
			{
				if (!Dummy.IsValid()) continue;
				if (State.IsValid()) State->RemovePlayerState(Dummy.Get());
				Dummy->Destroy();
			}
			if (PC.IsValid()) PC->ClientPreparationRole(OldRound, OldTeam);
			if (HUD.IsValid())
			{
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				HUD->NotifyHitBoxClick(bOldSettings ? TEXT("OpenSettings") : TEXT("CloseSettings"));
			}
			if (Mode.IsValid()) Mode->SetActorTickEnabled(bOldModeTick);
			TBText::SetLanguage(OldLanguage);
			if (auto* Settings = UTBGameUserSettings::Get()) Settings->SetLanguage(OldSavedLanguage);
		}

		bool Update() override
		{
			if (!World.IsValid() || FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("UI review lost its world or exceeded 180 seconds."));
				return true;
			}
			if (State.IsValid() && (State->bTravelPending || World->IsInSeamlessTravel()))
			{
				Test->AddError(TEXT("UI fixture unexpectedly initiated travel; refusing misleading captures."));
				return true;
			}
			if (Playing && ((Step >= 9 && Step <= 15) || (Step >= 19 && Step <= 22)))
			{
				Playing->Hold(State->GetServerWorldTimeSeconds() + (Step == 19 ? 10 : 187));
				if (Step == 12)
				{
					State->HidePreparation.bHiding = true;
					State->HidePreparation.StartsAt = State->GetServerWorldTimeSeconds() - 3;
					State->HidePreparation.EndsAt = State->GetServerWorldTimeSeconds() + 12;
				}
			}
			if (!bEntered)
			{
				if (bTitle ? !EnterTitle() : !EnterGame()) return true;
				bEntered = true;
				const auto* Loading = GetDefault<UTBLoadingScreenSettings>();
				const double Delay = !bTitle && Step == 9 ? .25 :
				    !bTitle && (Step == 10 || Step == 19) ? Loading->MatchSwitchSeconds + Loading->FadeOutSeconds + 1.0 :
				    !bTitle && Step == 18 ? Loading->MatchSwitchSeconds + Loading->SettleWaitSeconds + Loading->FadeOutSeconds + 1.0 : 1.25;
				ReadyAt = FPlatformTime::Seconds() + Delay;
				Frames = 0;
				return false;
			}
			// Automation runs with an unfocused window; Slate can close an edit box
			// after the opening tick. Keep this fixture open through the capture frame.
			if (!bTitle && Step == 4 && PC.IsValid()) PC->OpenChat();
			if (TitleSlateButton.IsValid() && !HoldTitleButton()) return true;
			if (bOverviewActive) HoldOverview();
			if (++Frames < 12 || FPlatformTime::Seconds() < ReadyAt) return false;
			if (!bRequested)
			{
				if (bTitle && Step == 0)
				{
#if WITH_EDITOR
					for (const auto& Texture : ReviewTextures)
						if (Texture->IsCompiling()) return false;
#endif
					for (const auto& Texture : ReviewTextures)
						Test->TestTrue(*FString::Printf(TEXT("UI artwork has generated mips: %s (%d)"),
						    *Texture->GetName(), Texture->GetNumMips()), Texture->GetNumMips() > 1);
				}
				if (TitleSlateButton.IsValid() && !CheckTitleButton()) return true;
				if (!bTitle && (Step == 1 || Step == 2 || Step == 23))
				{
					const bool bNPCEnabled = Step != 1;
					if (!Test->TestEqual(TEXT("NPC human controls appear only while enabled"),
					    HUD->GetHitBoxWithName(TEXT("DraftMoreNPCHumans")) != nullptr, bNPCEnabled) ||
					    !Test->TestEqual(TEXT("NPC toy controls appear only while enabled"),
					    HUD->GetHitBoxWithName(TEXT("DraftMoreNPCToys")) != nullptr, bNPCEnabled)) return true;
					if (!Test->TestNotNull(TEXT("Stage has the shared settings entry"), HUD->GetHitBoxWithName(TEXT("OpenOptions")))) return true;
				}
				if (!bTitle && ((Step >= 10 && Step <= 15 && Step != 12) || Step == 19 || Step == 21 || Step == 22) &&
				    !Test->TestFalse(TEXT("HUD capture is not obscured by preparation masking"), PC->ShouldHidePreparationView()))
					return true;
				if (!bTitle && (Step == 21 || Step == 22))
				{
					if (!Test->TestTrue(TEXT("Gaze capture uses the possessed pawn view"), Body.IsValid() &&
					    PC->GetPawn() == Body.Get() && PC->GetViewTarget() == Body.Get())) return true;
					if (!Test->TestTrue(TEXT("Human gaze has reached full camera zoom"), Body->IsHuman() &&
					    Body->IsGazing() && Body->GetGazeZoom() > .98f)) return true;
					if (!Test->TestNotNull(TEXT("Gaze camera manager"), PC->PlayerCameraManager.Get())) return true;
					const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
					if (!Test->TestTrue(TEXT("Gaze view keeps the authored room overview"), bOverviewActive &&
					    View.Location.Equals(OverviewLocation, 2.f) && View.Rotation.Equals(OverviewRotation, 1.f))) return true;
					if (Step == 22)
					{
						if (!Test->TestTrue(TEXT("Gaze camera renders constrained side bars"), View.bConstrainAspectRatio &&
						    FMath::IsNearlyEqual(View.AspectRatio, 4.f / 3.f, .001f) && PC->ViewAspect > View.AspectRatio * 1.01f)) return true;
					}
				}
				if (!bTitle && Step == 4 &&
				    !Test->TestTrue(TEXT("Chat remains open on the capture frame"), PC->IsChatOpen())) return true;
				const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
				const FString Folder = FPaths::ProjectSavedDir() / TEXT("UIRefresh/ScreenshotsV2") / TBText::GetLanguage() /
				    FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
				IFileManager::Get().MakeDirectory(*Folder, true);
				Path = Folder / (Name + TEXT(".png"));
				FScreenshotRequest::RequestScreenshot(Path, true, false);
				bRequested = true;
				Frames = 0;
				return false;
			}
			if (FScreenshotRequest::IsScreenshotRequested()) return false;
			Test->TestTrue(*FString::Printf(TEXT("Rendered screenshot: %s"), *Name),
			              IFileManager::Get().FileSize(*Path) > 1024);
			if ((!bTitle && Step == 22) || (bTitle && Step == 3)) return true;
			// Photograph the settings return before the existing match sequence;
			// retain the earlier step indices used by transition/gaze assertions.
			if (bTitle && Step == 0) Step = 4;
			else if (bTitle && Step == 9) { RestoreTitleButton(); Step = 1; }
			else if (!bTitle && Step == 8) Step = 23;
			else if (!bTitle && Step == 23) Step = 9;
			else ++Step;
			if (bRosterOnly && Step == 3) Step = 18;
			else if (bRosterOnly && Step == 19) return true;
			bEntered = false;
			bRequested = false;
			return false;
		}

	private:
		bool BeginOverview()
		{
			if (!Test->TestNotNull(TEXT("Overview character"), Body.Get()) ||
			    !Test->TestNotNull(TEXT("Overview pawn camera"), ReviewCamera.Get())) return false;
			if (!bOverviewActive)
			{
				UCameraComponent* AuthoredCamera = nullptr;
				for (TActorIterator<ACameraActor> It(World.Get()); It; ++It)
				{
					if (!It->ActorHasTag(TEXT("TBLobbyCamera"))) continue;
					AuthoredCamera = It->GetCameraComponent();
					break;
				}
				if (!Test->TestNotNull(TEXT("Authored lobby overview camera"), AuthoredCamera)) return false;
				OverviewMovement = Cast<UTBMovement>(Body->GetCharacterMovement());
				if (!Test->TestNotNull(TEXT("Overview character movement"), OverviewMovement.Get())) return false;
				OverviewLocation = AuthoredCamera->GetComponentLocation();
				OverviewRotation = AuthoredCamera->GetComponentRotation();
				OldBodyTransform = Body->GetActorTransform();
				OldControlRotation = PC->GetControlRotation();
				OldMovementMode = OverviewMovement->MovementMode;
				OldCustomMovementMode = OverviewMovement->CustomMovementMode;
				OldVelocity = OverviewMovement->Velocity;
				bOldMovementTick = OverviewMovement->IsComponentTickEnabled();
				bOverviewActive = true;
				// ApplyState revives MOVE_None; suspend only movement ticking so the
				// actual pawn CalcCamera still drives gaze zoom without falling.
				OverviewMovement->SetComponentTickEnabled(false);
				OverviewMovement->StopMovementImmediately();
			}
			Body->ApplyState();
			HoldOverview();
			return true;
		}

		void HoldOverview()
		{
			if (!bOverviewActive || !Body.IsValid() || !ReviewCamera.IsValid() || !PC.IsValid()) return;
			PC->SetControlRotation(OverviewRotation);
			const FRotator Heading(0.f, OverviewRotation.Yaw, 0.f);
			if (!Body->GetActorRotation().Equals(Heading, .01f)) Body->SetActorRotation(Heading, ETeleportType::TeleportPhysics);
			// Human/toy eye heights differ. Align the real camera component, not
			// BaseEyeHeight or an assumed offset, after each role's ApplyState.
			const FVector CameraOffset = ReviewCamera->GetComponentLocation() - Body->GetActorLocation();
			const FVector Position = OverviewLocation - CameraOffset;
			if (!Body->GetActorLocation().Equals(Position, .01f))
				Body->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
		}

		void RestoreOverview()
		{
			if (!bOverviewActive) return;
			if (Body.IsValid()) Body->SetActorTransform(OldBodyTransform, false, nullptr, ETeleportType::TeleportPhysics);
			if (PC.IsValid()) PC->SetControlRotation(OldControlRotation);
			if (OverviewMovement.IsValid())
			{
				OverviewMovement->SetMovementMode(OldMovementMode, OldCustomMovementMode);
				OverviewMovement->Velocity = OldVelocity;
				OverviewMovement->SetComponentTickEnabled(bOldMovementTick);
			}
			bOverviewActive = false;
		}

		void RestoreChat()
		{
			if (!SampleChat.IsValid()) return;
			SampleChat->Clear();
			// The public log API restores content/order, not IDs or timestamps.
			// This opt-in fixture requires a fresh standalone process.
			for (const FTBChatLine& Line : OldChatLines)
			{
				if (Line.bNotice) SampleChat->AddNotice(Line.Text);
				else SampleChat->Add(Line.Name, Line.Text, Line.bSelf);
			}
			SampleChat.Reset();
			OldChatLines.Reset();
		}

		bool ClickTitle(FName ButtonName)
		{
			auto* Button = Title.IsValid() ? Cast<UButton>(Title->WidgetTree->FindWidget(ButtonName)) : nullptr;
			if (!Test->TestNotNull(*ButtonName.ToString(), Button)) return false;
			Button->OnClicked.Broadcast();
			return true;
		}

		bool PrepareTitleReview()
		{
			bool bValid = true;
			for (const TCHAR* ButtonName : {TEXT("HostButton"), TEXT("FindButton"), TEXT("HelpButton"),
			    TEXT("OptionsButton"), TEXT("QuitButton")})
			{
				auto* Button = Cast<UButton>(Title->WidgetTree->FindWidget(FName(ButtonName)));
				if (!Test->TestNotNull(ButtonName, Button)) return false;
				UWidgetTree::ForEachWidgetAndChildrenUntil(Button, [&](UWidget* Widget)
				{
					const auto Slate = Widget->GetCachedWidget();
					const auto Tooltip = Slate.IsValid() ? Slate->GetToolTip() : nullptr;
					bValid &= Test->TestTrue(*FString::Printf(TEXT("Title control has no default tooltip: %s/%s"),
					    ButtonName, *Widget->GetName()), Widget->GetToolTipText().IsEmpty() && !Widget->GetToolTip() &&
					    (!Tooltip.IsValid() || Tooltip->IsEmpty()));
					return true;
				});
			}
			for (const TCHAR* Art : {TEXT("TitleLogo"), TEXT("Button_HostRoom_Normal"),
			    TEXT("Button_HostRoom_Hover"), TEXT("Button_HostRoom_Pressed"), TEXT("Button_FindRoom_Normal"),
			    TEXT("Button_FindRoom_Hover"), TEXT("Button_FindRoom_Pressed"), TEXT("Button_HowToPlay_Normal"),
			    TEXT("Button_HowToPlay_Hover"), TEXT("Button_HowToPlay_Pressed"), TEXT("Frame_Settings"),
			    TEXT("Icon_Hourglass"), TEXT("Icon_BatteryPickup"), TEXT("StagePreview_Demo"), TEXT("StagePreview_House")})
			{
				auto* Texture = TBPaper::Art(Art);
				if (!Test->TestNotNull(*FString::Printf(TEXT("UI review artwork loads: %s"), Art), Texture)) return false;
				ReviewTextures.Emplace(Texture);
			}
			return bValid;
		}

		void RestoreTitleButton()
		{
			if (TitleSlateButton.IsValid())
			{
				// Release without a key-up/click while every action delegate is still detached.
				TitleSlateButton->OnMouseCaptureLost(FCaptureLostEvent(0, 0));
				TitleSlateButton->OnMouseLeave(FPointerEvent());
				if (bOldTitleHovered)
					TitleSlateButton->OnMouseEnter(TitleSlateButton->GetCachedGeometry(), FPointerEvent());
			}
			if (TitleVisualButton.IsValid())
			{
				TitleVisualButton->SetPressMethod(OldTitlePressMethod);
				TitleVisualButton->OnClicked = OldTitleClicked;
				TitleVisualButton->OnPressed = OldTitlePressed;
				TitleVisualButton->OnReleased = OldTitleReleased;
			}
			TitleSlateButton.Reset();
			TitleVisualButton.Reset();
		}

		bool HoldTitleButton()
		{
			if (!TitleSlateButton->IsHovered())
				TitleSlateButton->OnMouseEnter(TitleSlateButton->GetCachedGeometry(), FPointerEvent());
			if (bTitlePressed && !TitleSlateButton->IsPressed())
			{
				// UE 5.8 exposes AppearPressed only to subclasses. Use the real Slate
				// pressed state, with UButton actions detached and no key-up dispatched.
				const FKeyEvent Accept(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
				return Test->TestTrue(TEXT("Title visual press is handled"),
				    TitleSlateButton->OnKeyDown(TitleSlateButton->GetCachedGeometry(), Accept).IsEventHandled());
			}
			return true;
		}

		bool CheckTitleButton()
		{
			if (!Test->TestTrue(TEXT("Title visual fixture has no bound click/press/release actions"),
			    !TitleVisualButton->OnClicked.IsBound() && !TitleVisualButton->OnPressed.IsBound() &&
			    !TitleVisualButton->OnReleased.IsBound()) ||
			    !Test->TestTrue(TEXT("UMG title control is hovered"), TitleVisualButton->IsHovered()) ||
			    !Test->TestEqual(TEXT("UMG title control reports the captured pressed state"),
			        TitleVisualButton->IsPressed(), bTitlePressed)) return false;
			if (TBText::GetLanguage() == TEXT("en"))
			{
				const auto& Style = TitleVisualButton->GetStyle();
				UObject* Expected = (bTitlePressed ? Style.Pressed : Style.Hovered).GetResourceObject();
				int32 Patches = 0;
				bool bMatches = true;
				UWidgetTree::ForEachWidgetAndChildrenUntil(TitleVisualButton.Get(), [&](UWidget* Widget)
				{
					if (const auto* Patch = Cast<UImage>(Widget))
					{
						++Patches;
						bMatches &= Patch->GetBrush().GetResourceObject() == Expected;
					}
					return true;
				});
				return Test->TestTrue(TEXT("Translated caption patch follows the actual button state"), Patches > 0 && bMatches);
			}
			return true;
		}

		bool EnterTitle()
		{
			switch (Step)
			{
			case 0:
			{
				TArray<UUserWidget*> Found;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World.Get(), Found, UTBTitleMenu::StaticClass(), true);
				if (!Test->TestEqual(TEXT("One actual title menu"), Found.Num(), 1)) return false;
				Title = Cast<UTBTitleMenu>(Found[0]);
				Title->SkipStory();
				if (!PrepareTitleReview()) return false;
				Name = TEXT("01-title");
				break;
			}
			case 1:
				if (!ClickTitle(TEXT("HelpButton"))) return false;
				Name = TEXT("02-how-to-play");
				break;
			case 2:
				if (!ClickTitle(TEXT("CloseHelp")) || !ClickTitle(TEXT("FindButton"))) return false;
				Test->TestTrue(TEXT("Room finder reveals session cards"), Title->AreRoomCardsShown());
				Name = TEXT("03-room-finder");
				break;
			case 3:
			{
				if (!ClickTitle(TEXT("OptionsButton"))) return false;
				TArray<UUserWidget*> Found;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World.Get(), Found, UTBOptionsMenu::StaticClass(), true);
				if (!Test->TestEqual(TEXT("Title opens shared options"), Found.Num(), 1)) return false;
				Options = Cast<UTBOptionsMenu>(Found[0]);
				Name = TEXT("04-title-options");
				break;
			}
			case 4: case 5: case 6: case 7: case 8: case 9:
			{
				RestoreTitleButton();
				const TCHAR* Buttons[] = {TEXT("HostButton"), TEXT("FindButton"), TEXT("HelpButton")};
				const TCHAR* Captures[] = {TEXT("38-title-host-hover"), TEXT("39-title-host-pressed"),
				    TEXT("40-title-find-hover"), TEXT("41-title-find-pressed"), TEXT("42-title-help-hover"),
				    TEXT("43-title-help-pressed")};
				auto* Button = Cast<UButton>(Title->WidgetTree->FindWidget(FName(Buttons[(Step - 4) / 2])));
				if (!Test->TestNotNull(TEXT("Title visual control"), Button) ||
				    !Test->TestTrue(TEXT("Title visual control has a Slate widget"), Button->GetCachedWidget().IsValid()) ||
				    !Test->TestFalse(TEXT("Title control starts unpressed"), Button->IsPressed())) return false;
				TitleVisualButton = Button;
				TitleSlateButton = StaticCastSharedPtr<SButton>(Button->GetCachedWidget());
				OldTitleClicked = Button->OnClicked;
				OldTitlePressed = Button->OnPressed;
				OldTitleReleased = Button->OnReleased;
				OldTitlePressMethod = Button->GetPressMethod();
				bOldTitleHovered = Button->IsHovered();
				Button->OnClicked.Clear();
				Button->OnPressed.Clear();
				Button->OnReleased.Clear();
				Button->SetPressMethod(EButtonPressMethod::DownAndUp);
				bTitlePressed = Step % 2 == 1;
				Name = Captures[Step - 4];
				if (!HoldTitleButton()) return false;
				break;
			}
			default:
				return false;
			}
			return true;
		}

		bool EnterGame()
		{
			if (!PC.IsValid() || !State.IsValid() || !HUD.IsValid() || !Player.IsValid())
			{
				Test->AddError(TEXT("UI review lost its controller, HUD or state."));
				return false;
			}
			switch (Step)
			{
			case 0:
				State->Phase = ETBPhase::Lobby;
				Player->Team = ETBTeam::None;
				HUD->NotifyHitBoxClick(TEXT("CloseSettings"));
				Name = TEXT("10-lobby");
				break;
			case 1:
				// Local screenshot state only; the fixture restores the original rules.
				State->Settings.NPCHumans = State->Settings.NPCToys = 0;
				HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
				Test->TestTrue(TEXT("Game settings opens directly to stage and rules"),
				    HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				// Select a known preview through the draft only; no map or rules save.
				HUD->NotifyHitBoxClick(TEXT("StageExpanded"));
				Name = TEXT("11-room-settings");
				break;
			case 2:
				HUD->NotifyHitBoxClick(TEXT("DraftNextStage"));
				HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
				Test->TestTrue(TEXT("Changing the stage stays in the same settings screen"),
				    HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				Name = TEXT("12-stage-picker");
				break;
			case 3:
				HUD->NotifyHitBoxClick(TEXT("CancelStage"));
				Test->TestFalse(TEXT("Cancel closes stage and rules"), HUD->IsStagePickerOpen());
				Test->TestFalse(TEXT("Cancel returns directly to the lobby"), HUD->IsLobbySettingsOpen());
				HUD->NotifyHitBoxClick(TEXT("Players"));
				Test->TestTrue(TEXT("Player panel blocks chat"), HUD->IsChatBlocked());
				Name = TEXT("13-players");
				break;
			case 4:
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				// The previous panel removed the chat widget. Recreate it through the
				// normal controller tick before exercising OpenChat, as real input does.
				PC->PlayerTick(0.f);
				PC->OpenChat();
				Test->TestTrue(TEXT("Lobby chat opens"), PC->IsChatOpen());
				if (auto* Log = World->GetGameInstance()->GetSubsystem<UTBChatLog>())
				{
					// Local fixture data only: never use SendChat / RPCs.
					SampleChat = Log;
					OldChatLines = Log->GetLines();
					Log->Add(TEXT("くま"), TEXT("次はおもちゃで参加するね！"), false);
					Log->Add(TEXT("おもちゃ探検隊の長いお名前"),
					    TEXT("電池を見つけたよ！みんなの準備ができたら、リビングの出口に集まって一緒に脱出しよう！"), false);
					Log->Add(TEXT("あなた"), TEXT("了解！準備できたよ。"), true);
				}
				Name = TEXT("14-chat");
				break;
			case 5:
				PC->CloseChat();
				RestoreChat();
				HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
				HUD->NotifyHitBoxClick(TEXT("StageExpanded"));
				HUD->NotifyHitBoxClick(TEXT("DraftMoreTime"));
				HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
				HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCHumans"));
				HUD->NotifyHitBoxClick(TEXT("OpenOptions"));
				{
					TArray<UUserWidget*> Found;
					UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World.Get(), Found, UTBOptionsMenu::StaticClass(), true);
					if (!Test->TestEqual(TEXT("Stage opens one shared settings menu"), Found.Num(), 1)) return false;
					Options = Cast<UTBOptionsMenu>(Found[0]);
				}
				if (!Test->TestNotNull(TEXT("Stage shared options opens"), Options.Get())) return false;
				Options->ShowPage(TBOptionsPage::Audio);
				Name = TEXT("15-options-audio");
				break;
			case 6:
				Options->ShowPage(TBOptionsPage::Display);
				Test->TestEqual(TEXT("Display tab selected"), Options->GetPage(), int32(TBOptionsPage::Display));
				Name = TEXT("16-options-display");
				break;
			case 7:
				Options->ShowPage(TBOptionsPage::Controls);
				Test->TestTrue(TEXT("Controls has real remapping rows"), !Options->GetKeyRows().IsEmpty());
				Name = TEXT("17-options-controls");
				break;
			case 8:
				Options->BeginKeyCapture(TB::Keys::LobbyReady);
				Test->TestTrue(TEXT("Key capture opens without changing a binding"), Options->IsCapturing());
				Name = TEXT("18-key-capture");
				break;
			case 9:
				Options->Dismiss();
				HUD->NotifyHitBoxClick(TEXT("CancelStage"));
				Test->TestFalse(TEXT("Visual review cancels the returned draft before the match"), HUD->IsStagePickerOpen());
				Playing = MakeUnique<TBTest::FPlayingScope>(*State, State->GetServerWorldTimeSeconds() + 190);
				State->HidePreparation.Round = OldRound + 1;
				Player->Team = ETBTeam::Toy;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				if (!BeginOverview()) return false;
				PC->PlayerTick(0.f);
				Name = TEXT("19-loading");
				break;
			case 10:
				Name = TEXT("20-toy-hud");
				break;
			case 11:
				Player->Team = ETBTeam::Human;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				Name = TEXT("21-human-hud");
				break;
			case 12:
				Name = TEXT("22-hide-preparation");
				break;
			case 13:
				Player->Team = ETBTeam::Toy;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				State->bEscapePhase = true;
				State->Collected = State->Settings.RequiredItems;
				Name = TEXT("23-escape-hud");
				break;
			case 14:
				PC->ToggleLeaveMenu();
				Test->TestTrue(TEXT("Pause menu opens"), PC->IsLeaveMenuOpen());
				Name = TEXT("24-pause");
				break;
			case 15:
				HUD->NotifyHitBoxClick(TEXT("Resume"));
				Test->TestFalse(TEXT("Resume closes pause menu"), PC->IsLeaveMenuOpen());
				PC->ToggleLeaveMenu();
				HUD->NotifyHitBoxClick(TEXT("Players"));
				Test->TestTrue(TEXT("Match participant list opens from pause"), HUD->IsChatBlocked());
				Name = TEXT("25-match-players");
				break;
			case 16:
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				HUD->NotifyHitBoxClick(TEXT("Resume"));
				State->Phase = ETBPhase::Results;
				State->Winner = ETBWinner::Toys;
				State->Reason = TEXT("乗り物で脱出！");
				State->ResultStyle = ETBResultStyle::None;
				State->ResultStartTime = State->GetServerWorldTimeSeconds() - 30;
				State->EndTime = State->GetServerWorldTimeSeconds() + 120;
				Name = TEXT("30-results-toys");
				break;
			case 17:
				State->Winner = ETBWinner::Humans;
				State->Reason = TEXT("All toys boxed");
				Name = TEXT("31-results-humans");
				break;
			case 18:
			{
				RestoreOverview();
				State->Phase = ETBPhase::Lobby;
				Player->Team = ETBTeam::None;
				Player->bReady = true;
				State->bEscapePhase = false;
				State->Settings.Humans = 4;
				State->Settings.Toys = 4;
				State->Settings.NPCHumans = State->Settings.NPCToys = 0;
				const TCHAR* Names[] = {TEXT("くま"), TEXT("小さな探検家"), TEXT("ながいなまえのプレイヤーさん"),
				    TEXT("おもちゃ係"), TEXT("ねこのロボット"), TEXT("HIDE AND SEEK"), TEXT("ともだち")};
				for (int32 Index = State->PlayerArray.Num(); Index < 8; ++Index)
				{
					FActorSpawnParameters Params;
					Params.ObjectFlags |= RF_Transient;
					auto* Dummy = World->SpawnActor<ATBPlayerState>(Params);
					if (!Test->TestNotNull(TEXT("Local visual roster entry"), Dummy)) return false;
					Roster.Add(Dummy);
					Dummy->SetPlayerName(Names[(Index - 1 + UE_ARRAY_COUNT(Names)) % UE_ARRAY_COUNT(Names)]);
					Dummy->SetPlayerId(-1000 - Index);
					Dummy->Preference = Index % 3 == 0 ? ETBTeam::None : Index % 3 == 1 ? ETBTeam::Toy : ETBTeam::Human;
					Dummy->bReady = Index % 3 != 0;
				}
				Test->TestEqual(TEXT("Full eight-player roster"), State->PlayerArray.Num(), 8);
				HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
				Test->TestTrue(TEXT("Eight-player lobby opens directly to stage settings"),
				    HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				HUD->NotifyHitBoxClick(TEXT("Players"));
				Test->TestTrue(TEXT("Eight-player participant list blocks chat input"), HUD->IsChatBlocked());
				Name = TEXT("32-eight-player-roster");
				break;
			}
			case 19:
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				HUD->NotifyHitBoxClick(TEXT("CloseSettings"));
				Playing->Hold(State->GetServerWorldTimeSeconds() + 10);
				State->HidePreparation.Round = OldRound + 2;
				Player->Team = ETBTeam::Toy;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				if (!BeginOverview()) return false;
				Name = TEXT("33-urgent-timer");
				break;
			case 20:
				PC->ToggleLeaveMenu();
				Options = UTBOptionsMenu::Open(PC.Get());
				if (!Test->TestNotNull(TEXT("Language settings opens"), Options.Get())) return false;
				Options->ShowPage(TBOptionsPage::Language);
				Name = TEXT("34-options-language");
				break;
			case 21:
				Options->Dismiss();
				if (PC->IsLeaveMenuOpen()) HUD->NotifyHitBoxClick(TEXT("Resume"));
				if (!Test->TestNotNull(TEXT("Gaze review character"), Body.Get()) ||
				    !Test->TestNotNull(TEXT("Gaze review camera"), ReviewCamera.Get())) return false;
				Player->Team = ETBTeam::Human;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				if (!BeginOverview()) return false;
				PC->PlayerTick(0.f);
				PC->SetViewTarget(Body.Get());
				bChangedGazeView = true;
				Body->SetGazeRequested(true);
				Name = TEXT("35-human-gaze");
				break;
			case 22:
				// Simulate ultrawide side bars at either review resolution without
				// resizing the viewport or changing a saved camera/DA setting.
				ReviewCamera->AspectRatio = 4.f / 3.f;
				ReviewCamera->bConstrainAspectRatio = true;
				Name = TEXT("36-human-gaze-letterbox");
				break;
			case 23:
			{
				const FKeyEvent Escape(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);
				Test->TestTrue(TEXT("Escape cancels key capture"), Options->SendKeyDownForTest(Escape).IsEventHandled());
				Test->TestFalse(TEXT("Key capture has closed"), Options->IsCapturing());
				Test->TestTrue(TEXT("Cancelling capture keeps settings open"), Options->IsInViewport());
				Options->SendKeyUpForTest(Escape);
				Test->TestTrue(TEXT("Escape returns from settings"), Options->SendKeyDownForTest(Escape).IsEventHandled());
				Test->TestFalse(TEXT("Returned settings overlay is removed"), Options->IsInViewport());
				Test->TestTrue(TEXT("Settings return keeps the stage draft"), HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				Name = TEXT("37-stage-options-return");
				break;
			}
			default:
				return false;
			}
			return true;
		}

		FAutomationTestBase* Test;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ATBController> PC;
		TWeakObjectPtr<ATBCharacter> Body;
		TWeakObjectPtr<UCameraComponent> ReviewCamera;
		TWeakObjectPtr<UTBMovement> OverviewMovement;
		TWeakObjectPtr<AActor> OldViewTarget;
		TWeakObjectPtr<ATBGameMode> Mode;
		TWeakObjectPtr<ATBGameState> State;
		TWeakObjectPtr<ATBPlayerState> Player;
		TWeakObjectPtr<ATBHUD> HUD;
		TWeakObjectPtr<UTBTitleMenu> Title;
		TWeakObjectPtr<UButton> TitleVisualButton;
		TSharedPtr<SButton> TitleSlateButton;
		TArray<TStrongObjectPtr<UTexture2D>> ReviewTextures;
		FOnButtonClickedEvent OldTitleClicked;
		FOnButtonPressedEvent OldTitlePressed;
		FOnButtonReleasedEvent OldTitleReleased;
		EButtonPressMethod::Type OldTitlePressMethod = EButtonPressMethod::DownAndUp;
		bool bTitlePressed = false, bOldTitleHovered = false;
		TWeakObjectPtr<UTBOptionsMenu> Options;
		TWeakObjectPtr<UTBChatLog> SampleChat;
		TArray<FTBChatLine> OldChatLines;
		TArray<TWeakObjectPtr<ATBPlayerState>> Roster;
		FTBSettings OldSettings;
		TUniquePtr<TBTest::FPlayingScope> Playing;
		ETBTeam OldTeam = ETBTeam::None;
		ETBWinner OldWinner = ETBWinner::None;
		ETBResultStyle OldStyle = ETBResultStyle::None;
		FString OldReason, Name, Path, OldLanguage, OldSavedLanguage;
		int32 OldCollected = 0, Step = 0, Frames = 0;
		uint32 OldRound = 0;
		bool bTitle, bEntered = false, bRequested = false, bOldModeTick = false;
		bool bRosterOnly = false;
		bool bOldSettings = false, bOldEscape = false, bOldReady = false;
		bool bOldGazeRequested = false, bOldCameraConstrained = false, bChangedGazeView = false;
		bool bOverviewActive = false, bOldMovementTick = false;
		EMovementMode OldMovementMode = MOVE_None;
		uint8 OldCustomMovementMode = 0;
		FTransform OldBodyTransform = FTransform::Identity;
		FVector OverviewLocation = FVector::ZeroVector, OldVelocity = FVector::ZeroVector;
		FRotator OverviewRotation = FRotator::ZeroRotator, OldControlRotation = FRotator::ZeroRotator;
		float OldCameraAspect = 0;
		double OldResultStart = 0, Deadline, ReadyAt = 0;
	};

	bool CanReview(FAutomationTestBase& Test)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("UIReviewScreenshots")))
		{
			Test.AddInfo(TEXT("Opt in with -UIReviewScreenshots and a rendered standalone -game process."));
			return false;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")) || !GEngine ||
		    !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
		{
			Test.AddError(TEXT("UI review requires a rendered viewport; do not use -NullRHI."));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUIReviewGame, "ToonStory.UI.VisualReview.Game",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUIReviewGame::RunTest(const FString&)
{
	if (!CanReview(*this)) return !HasAnyErrors();
	auto* World = ReviewWorld();
	auto* PC = World ? Cast<ATBController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->GetPlayerState<ATBPlayerState>() || !Cast<ATBHUD>(PC->GetHUD()) ||
	    !World->GetGameState<ATBGameState>() || World->GetGameState<ATBGameState>()->Phase != ETBPhase::Lobby)
	{
		AddError(TEXT("Run in a fresh standalone /Game/Maps/TB_Lobby -game process."));
		return false;
	}
	for (const TCHAR* File : {TEXT("zen-medium.ttf"), TEXT("yusei.ttf"), TEXT("mochiy.ttf"), TEXT("toy-ui-numerals.ttf"), TEXT("zen.ttf")})
	{
		TestTrue(*FString::Printf(TEXT("Font staged: %s"), File),
		    IFileManager::Get().FileSize(*(FPaths::ProjectContentDir() / TEXT("UI/PaperClub/Fonts") / File)) > 1024);
	}
	for (const TCHAR* Art : {TEXT("Icon_Battery"), TEXT("Icon_Hourglass"), TEXT("Tape_Cream"),
	    TEXT("StagePreview_Demo"), TEXT("StagePreview_House")})
	{
		TestNotNull(*FString::Printf(TEXT("Designer artwork loads: %s"), Art), TBPaper::Art(Art));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FUIReview(this, World, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUIReviewTitle, "ToonStory.UI.VisualReview.Title",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUIReviewTitle::RunTest(const FString&)
{
	if (!CanReview(*this)) return !HasAnyErrors();
	auto* World = ReviewWorld();
	if (!World || !World->GetMapName().EndsWith(TEXT("Title")))
	{
		AddError(TEXT("Run in a fresh standalone /Game/Maps/Title -game process."));
		return false;
	}
	const auto* Online = Online::GetSubsystem(World);
	if (!Online || Online->GetSubsystemName() != FName(TEXT("NULL")))
	{
		AddError(TEXT("Room-finder review requires LAN/Null, never a public Steam session."));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FUIReview(this, World, true));
	return true;
}
#endif
