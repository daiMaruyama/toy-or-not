#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/TBTestMatch.h"
#include "Core/TBController.h"
#include "Core/TBGameState.h"
#include "Core/TBGameMode.h"
#include "UI/TBHUD.h"
#include "Core/TBPlayerState.h"
#include "Character/TBCharacter.h"
#include "Character/TBMovement.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "Tests/AutomationCommon.h"

namespace
{
	// 陣営を切り替えてから、IMCが付き直るのを待って押すまでの余裕を含めたフレーム数。
	constexpr int32 PressHumanFrame = 2;
	constexpr int32 CheckHumanFrame = 6;
	constexpr int32 ReleaseCheckHumanFrame = 10;
	constexpr int32 SwitchToyFrame = 11;
	constexpr int32 PressToyFrame = 13;
	constexpr int32 CheckToyFrame = 17;
	constexpr int32 ReleaseCheckToyFrame = 21;
	constexpr int32 PressToyFlushFrame = 22;
	constexpr int32 FlushToyFrame = 26;
	constexpr int32 CheckFlushToyFrame = 30;
	constexpr int32 PressToyMenuFrame = 31;
	constexpr int32 OpenToyMenuFrame = 35;
	constexpr int32 CheckToyMenuFrame = 39;
	// 押したまま陣営が変わるケース。
	constexpr int32 BackToHumanFrame = 43;
	constexpr int32 PressHeldCrouchFrame = 45;
	constexpr int32 SwitchHeldCrouchFrame = 49;
	constexpr int32 CheckHeldCrouchFrame = 53;
	constexpr int32 PressHeldSprintFrame = 55;
	constexpr int32 SwitchHeldSprintFrame = 59;
	constexpr int32 CheckHeldSprintFrame = 63;

	void SendRoleKey(ATBController* PC, const FKey& Key, EInputEvent Event)
	{
		FInputKeyEventArgs Args(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, Event == IE_Released ? 0.0 : 1.0, 0.016f, 1, 0);
		Args.Event = Event;
		PC->PlayerInput->InputKey(Args);
	}

	// 人間は左Ctrlでしゃがみ、人形は左Shiftでダッシュが要求され、離すと戻ることを確かめる。
	// 反対の陣営のキーは効かないことも見る。押したまま陣営が変わったときは、IMCが外れてCompletedが来て戻ることを見る。
	// 終わったら陣営とフェーズを元へ戻す。
	class FTBRoleKeysCommand : public IAutomationLatentCommand
	{
	public:
		FTBRoleKeysCommand(ATBController* InPC, FAutomationTestBase* InTest) : PC(InPC), Test(InTest)
		{
		}

		virtual bool Update() override
		{
			auto* Body = PC.IsValid() ? Cast<ATBCharacter>(PC->GetPawn()) : nullptr;
			auto* State = PC.IsValid() ? PC->GetWorld()->GetGameState<ATBGameState>() : nullptr;
			if (!Body || !State)
			{
				Test->AddError(TEXT("Pawn or game state is gone"));
				Finish();
				return true;
			}
			auto* Movement = CastChecked<UTBMovement>(Body->GetCharacterMovement());
			if (!bStarted)
			{
				// 元の状態を控えてから、人間・試合中にする。
				bStarted = true;
				PausedGameMode = PC->GetWorld()->GetAuthGameMode();
				if (PausedGameMode.IsValid())
				{
					OriginalGameModeTick = PausedGameMode->IsActorTickEnabled();
					PausedGameMode->SetActorTickEnabled(false);
				}
				if (auto* HUD = Cast<ATBHUD>(PC->GetHUD()); HUD && HUD->IsStagePickerOpen())
				{
					OriginalStagePicker = true;
					const auto Phase = State->Phase; State->Phase = ETBPhase::Lobby;
					HUD->NotifyHitBoxClick(TEXT("CancelStage")); State->Phase = Phase;
				}
				OriginalTeam = Body->TBPS()->Team;
				OriginalToyState = Body->TBPS()->ToyState;
				OriginalFrozen = Body->TBPS()->bFrozen;
				OriginalMenu = PC->IsLeaveMenuOpen();
				if (OriginalMenu) PC->ToggleLeaveMenu();
				Body->TBPS()->ToyState = ETBToyState::Free;
				Body->TBPS()->bFrozen = false;
				Body->TBPS()->Team = ETBTeam::Human;
				PC->SyncInputContexts();
			}
			// 入力とメニューフォーカスを検査する間は自動勝敗判定を止め、終了時に元へ戻す。
			if (!Playing.IsSet()) Playing.Emplace(*State);
			Playing->Hold();
			switch (Frame)
			{
			case PressHumanFrame:
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Pressed);
				SendRoleKey(PC.Get(), EKeys::LeftShift, IE_Pressed);
				SendRoleKey(PC.Get(), EKeys::RightMouseButton, IE_Pressed);
				break;
			case CheckHumanFrame:
				Test->TestTrue(TEXT("Human: left Ctrl requests crouch"), Movement->bWantsToCrouch);
				Test->TestFalse(TEXT("Human: left Shift does not request sprint"), Movement->bWantsToSprint);
				Test->TestTrue(TEXT("Human: right click requests gaze"), Movement->bWantsToGaze);
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Released);
				SendRoleKey(PC.Get(), EKeys::LeftShift, IE_Released);
				SendRoleKey(PC.Get(), EKeys::RightMouseButton, IE_Released);
				break;
			case ReleaseCheckHumanFrame:
				Test->TestFalse(TEXT("Human: releasing left Ctrl ends crouch"), Movement->bWantsToCrouch);
				Test->TestFalse(TEXT("Human: releasing right click ends gaze"), Movement->bWantsToGaze);
				break;
			case SwitchToyFrame:
				Body->TBPS()->Team = ETBTeam::Toy;
				PC->SyncInputContexts();
				break;
			case PressToyFrame:
				// Changing the existing human pawn to a toy reduces its capsule. Wait for the actual
				// floor contact before sending input; falling intentionally cannot begin in the air.
				if (GroundWaitStarted < 0) GroundWaitStarted = Body->GetWorld()->GetTimeSeconds();
				if (!Movement->IsMovingOnGround() && Body->GetWorld()->GetTimeSeconds() - GroundWaitStarted < 3.) return false;
				if (!Movement->IsMovingOnGround()) Test->AddInfo(FString::Printf(TEXT("Toy input floor: location=%s mode=%d physicsLocked=%d frozen=%d elapsed=%.3f"),
				    *Body->GetActorLocation().ToString(), int32(Movement->MovementMode), Body->IsPhysicsLocked(), Body->IsGazeFrozen(),
				    Body->GetWorld()->GetTimeSeconds() - GroundWaitStarted));
				Test->TestTrue(TEXT("Toy input fixture has settled onto the floor"), Movement->IsMovingOnGround());
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Pressed);
				SendRoleKey(PC.Get(), EKeys::LeftShift, IE_Pressed);
				SendRoleKey(PC.Get(), EKeys::RightMouseButton, IE_Pressed);
				break;
			case CheckToyFrame:
				Test->TestTrue(TEXT("Toy: left Shift requests sprint"), Movement->bWantsToSprint);
				Test->TestTrue(TEXT("Toy: left Ctrl requests the fallen pose"), Movement->bWantsToCrouch);
				Test->TestFalse(TEXT("Toy: right click does not request gaze"), Movement->bWantsToGaze);
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Released);
				SendRoleKey(PC.Get(), EKeys::LeftShift, IE_Released);
				SendRoleKey(PC.Get(), EKeys::RightMouseButton, IE_Released);
				break;
			case ReleaseCheckToyFrame:
				Test->TestFalse(TEXT("Toy: releasing left Shift ends sprint"), Movement->bWantsToSprint);
				Test->TestFalse(TEXT("Toy: releasing Ctrl ends the held fall"), Movement->bWantsToCrouch);
				break;
			case PressToyFlushFrame:
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Pressed);
				break;
			case FlushToyFrame:
				Test->TestTrue(TEXT("Toy: held fall active before input focus flush"), Movement->bWantsToCrouch);
				PC->PlayerInput->FlushPressedKeys();
				break;
			case CheckFlushToyFrame:
				Test->TestFalse(TEXT("Toy: flushing pressed keys cancels the held fall"), Movement->bWantsToCrouch);
				break;
			case PressToyMenuFrame:
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Pressed);
				break;
			case OpenToyMenuFrame:
				Test->TestTrue(TEXT("Toy: held fall active before menu focus"), Movement->bWantsToCrouch);
				PC->ToggleLeaveMenu();
				break;
			case CheckToyMenuFrame:
				Test->TestTrue(TEXT("Toy: menu takes gameplay input focus"), PC->IsLeaveMenuOpen() && PC->IsMoveInputIgnored());
				Test->TestFalse(TEXT("Toy: menu focus releases the held fall without needing key-up"), Movement->bWantsToCrouch);
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Released);
				if (PC->IsLeaveMenuOpen()) PC->ToggleLeaveMenu();
				break;
			case BackToHumanFrame:
				Body->TBPS()->Team = ETBTeam::Human;
				PC->SyncInputContexts();
				break;
			case PressHeldCrouchFrame:
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Pressed);
				SendRoleKey(PC.Get(), EKeys::RightMouseButton, IE_Pressed);
				break;
			case SwitchHeldCrouchFrame:
				Test->TestTrue(TEXT("Held: Human crouch is requested before the switch"), Movement->bWantsToCrouch);
				Test->TestTrue(TEXT("Held: Human gaze is requested before the switch"), Movement->bWantsToGaze);
				Body->TBPS()->Team = ETBTeam::Toy;
				PC->SyncInputContexts();
				break;
			case CheckHeldCrouchFrame:
				// 左Ctrlと右クリックは押したまま。IMCが外れてCompletedが来れば戻る。
				Test->TestFalse(TEXT("Held: crouch ends when the role changes to Toy"), Movement->bWantsToCrouch);
				Test->TestFalse(TEXT("Held: gaze ends when the role changes to Toy"), Movement->bWantsToGaze);
				SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Released);
				SendRoleKey(PC.Get(), EKeys::RightMouseButton, IE_Released);
				break;
			case PressHeldSprintFrame:
				SendRoleKey(PC.Get(), EKeys::LeftShift, IE_Pressed);
				break;
			case SwitchHeldSprintFrame:
				Test->TestTrue(TEXT("Held: Toy sprint is requested before the switch"), Movement->bWantsToSprint);
				Body->TBPS()->Team = ETBTeam::Human;
				PC->SyncInputContexts();
				break;
			case CheckHeldSprintFrame:
				// 左Shiftは押したまま。IMCが外れてCompletedが来れば戻る。
				Test->TestFalse(TEXT("Held: sprint ends when the role changes to Human"), Movement->bWantsToSprint);
				Finish();
				return true;
			default:
				break;
			}
			++Frame;
			return false;
		}

	private:
		// 押したキーを離し、陣営とフェーズを元へ戻す。途中で抜けても残さない。
		void Finish()
		{
			if (!PC.IsValid()) return;
			SendRoleKey(PC.Get(), EKeys::LeftControl, IE_Released);
			SendRoleKey(PC.Get(), EKeys::LeftShift, IE_Released);
			SendRoleKey(PC.Get(), EKeys::RightMouseButton, IE_Released);
			if (auto* Body = Cast<ATBCharacter>(PC->GetPawn()); Body && bStarted)
			{
				Body->UnCrouch();
				Body->TBPS()->Team = OriginalTeam;
				Body->TBPS()->ToyState = OriginalToyState;
				Body->TBPS()->bFrozen = OriginalFrozen;
				Body->ApplyState();
			}
			if (bStarted && PC->IsLeaveMenuOpen() != OriginalMenu) PC->ToggleLeaveMenu();
			Playing.Reset();
			if (OriginalStagePicker)
				if (auto* HUD = Cast<ATBHUD>(PC->GetHUD()))
					if (auto* State = PC->GetWorld()->GetGameState<ATBGameState>())
					{
						const auto Phase = State->Phase; State->Phase = ETBPhase::Lobby;
						HUD->NotifyHitBoxClick(TEXT("OpenSettings")); State->Phase = Phase;
					}
			if (PausedGameMode.IsValid()) PausedGameMode->SetActorTickEnabled(OriginalGameModeTick);
			PC->SyncInputContexts();
		}

		TWeakObjectPtr<ATBController> PC;
		FAutomationTestBase* Test;
		int32 Frame = 0;
		double GroundWaitStarted = -1;
		ETBToyState OriginalToyState = ETBToyState::Free;
		bool OriginalFrozen = false;
		bool OriginalMenu = false;
		bool OriginalStagePicker = false;
		bool OriginalGameModeTick = false;
		TWeakObjectPtr<AActor> PausedGameMode;
		bool bStarted = false;
		ETBTeam OriginalTeam = ETBTeam::None;
		TOptional<TBTest::FPlayingScope> Playing;
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBRoleKeysTest, "ToonStory.Input.RoleKeys",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBRoleKeysTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game) World = Context.World();
	}
	auto* PC = World ? Cast<ATBController>(World->GetFirstPlayerController()) : nullptr;
	if (!TestNotNull(TEXT("Standalone game"), PC)) return false;
	if (!TestTrue(TEXT("Pawn"), Cast<ATBCharacter>(PC->GetPawn()) != nullptr)) return false;
	ADD_LATENT_AUTOMATION_COMMAND(FTBRoleKeysCommand(PC, this));
	return true;
}
#endif
