// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Core/TBTitleGameMode.h"
#include "Input/TBInputSettings.h"
#include "Core/TBSession.h"
#include "UI/TBTitleMenu.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "CineCameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/TBTitleEmote.h"
#include "Character/TBToyPresentation.h"
#include "EngineUtils.h"

namespace
{
	const FName TitleToyTag(TEXT("TitleToy"));
}

ATBTitleGameMode::ATBTitleGameMode()
{
	DefaultPawnClass = nullptr;
	SpectatorClass = nullptr;
	// Keep the base empty HUD: ClientSetHUD attempts SpawnActor even for nullptr.
	PlayerControllerClass = ATBTitleController::StaticClass();
}

ATBTitleController::ATBTitleController()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Pile(
		TEXT("/Game/Characters/ToyCollectionRealistic/TeddyBear/Materials/M_TitleBearPlush"));
	PortraitPile = Pile.Object;
}

void ATBTitleController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	// 子ども部屋には寸劇用のカメラも並ぶので、タイトル用の印が付いたカメラを先に選ぶ。
	ACameraActor* Camera = nullptr;
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("TitleCamera")))
		{
			Camera = *It;
			if (auto* Lens = Cast<UCineCameraComponent>(Camera->GetCameraComponent()))
			{
				auto Focus = Lens->FocusSettings;
				Focus.FocusMethod = ECameraFocusMethod::Disable;
				Lens->SetFocusSettings(Focus);
				auto& Grade = Lens->PostProcessSettings;
				Grade.bOverride_MotionBlurAmount = true;
				Grade.MotionBlurAmount = 0.f;
				Grade.bOverride_ColorSaturation = true;
				Grade.ColorSaturation = FVector4(1, 1, 1, .91f);
				Grade.bOverride_ColorGain = true;
				Grade.ColorGain = FVector4(1.f, .985f, .96f, .94f);
				Grade.bOverride_IndirectLightingIntensity = true;
				Grade.IndirectLightingIntensity = .75f;

				// A broad, off-axis reflection gives the bead eyes a catchlight and shapes the nap.
				auto* Softbox = NewObject<URectLightComponent>(Camera);
				Softbox->SetupAttachment(Lens);
				Softbox->SetMobility(EComponentMobility::Movable);
				Softbox->SetRelativeLocation(FVector(10, -65, 65));
				Softbox->SetRelativeRotation(FVector(95, 65, -58).Rotation());
				Softbox->SetIntensityUnits(ELightUnits::Lumens);
				Softbox->SetIntensity(420.f);
				Softbox->SetSourceWidth(70.f);
				Softbox->SetSourceHeight(90.f);
				Softbox->SetAttenuationRadius(260.f);
				Softbox->SetLightColor(FLinearColor(1.f, .93f, .86f));
				Softbox->RegisterComponent();
			}
			break;
		}
		if (!Camera) Camera = *It;
	}
	if (Camera) SetViewTarget(Camera);
	// The window is a broad light source relative to the miniature set.
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		if (auto* Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			Sun->SetLightSourceAngle(FMath::Max(Sun->LightSourceAngle, 3.f));
	}
	// ループで踊るおもちゃに、ゲームと同じエモートを出させる。メニューが寸劇を流し始める前に整える。
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TitleToyTag) && !It->FindComponentByClass<UTBTitleEmote>())
		{
			auto* Emote = NewObject<UTBTitleEmote>(*It);
			Emote->RegisterComponent();
			TBToyPresentation::UseLookPose(*It);
			// Each fur layer shares the body pose. Keep its depth and its eye/nose cutouts.
			for (auto* Part : TInlineComponentArray<USkeletalMeshComponent*>(*It))
			{
				if (!PortraitPile || !Part->LeaderPoseComponent.IsValid()) continue;
				const UMaterialInterface* Material = Part->GetMaterial(0);
				float ShellDepth;
				if (!Material || !Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ShellDepth")), ShellDepth)) continue;
				// The opaque fabric and its silhouette need the same nap and diffuse response.
				auto* Body = Part->LeaderPoseComponent.Get();
				if (Body->GetMaterial(0) != PortraitPile) Body->SetMaterial(0, PortraitPile);
				auto* Layer = UMaterialInstanceDynamic::Create(PortraitPile, Part);
				Layer->SetScalarParameterValue(TEXT("ShellDepth"), ShellDepth);
				for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
					if (Part->GetMaterial(Slot) == Material) Part->SetMaterial(Slot, Layer);
			}
		}
	}
	GetGameInstance()->GetSubsystem<UTBSession>()->Activate();
	// デザイナーが配置を直せる WBP があればそれを使い、読めなければ C++ だけで作る。
	UClass* MenuClass = LoadClass<UTBTitleMenu>(nullptr, TEXT("/Game/UI/Designer/WBP_TitleMenu.WBP_TitleMenu_C"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	Menu = CreateWidget<UTBTitleMenu>(this, MenuClass ? MenuClass : UTBTitleMenu::StaticClass());
	if (Menu)
	{
		Menu->AddToViewport();
		FInputModeGameAndUI Input;
		Input.SetWidgetToFocus(Menu->TakeWidget());
		Input.SetHideCursorDuringCapture(false);
		Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Input);
	}
	bShowMouseCursor = true;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
}

void ATBTitleController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ATBTitleController::ClickToyPressed);
}

bool ATBTitleController::ClickToy()
{
	if (!Menu || Menu->GetState() != UTBTitleMenu::EState::Menu) return false;
	FVector Origin, Direction;
	if (!DeprojectMousePositionToWorld(Origin, Direction)) return false;
	UTBTitleEmote* Toy = PickToy(Origin, Direction);
	return Toy && Toy->Next();
}

UTBTitleEmote* ATBTitleController::PickToy(const FVector& Origin, const FVector& Direction) const
{
	const FVector End = Origin + Direction * 100000.;
	UTBTitleEmote* Best = nullptr;
	double BestMiss = TNumericLimits<double>::Max();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		auto* Emote = It->ActorHasTag(TitleToyTag) ? It->FindComponentByClass<UTBTitleEmote>() : nullptr;
		if (!Emote) continue;
		FVector Center, Extent, Hit, Normal;
		It->GetActorBounds(true, Center, Extent);
		if (Extent.IsNearlyZero()) It->GetActorBounds(false, Center, Extent);
		float Time = 0.f;
		if (!FMath::LineExtentBoxIntersection(FBox(Center - Extent, Center + Extent), Origin, End, FVector::ZeroVector, Hit, Normal, Time)) continue;
		// 箱は重なりやすいので、クリックした線に体の中心がいちばん近いおもちゃを選ぶ（大きさで割って比べる）。
		const FVector ToCenter = Center - Origin;
		const double Miss = (ToCenter - Direction * (ToCenter | Direction)).Size() / FMath::Max(Extent.Size(), 1.);
		if (Miss >= BestMiss) continue;
		// 壁や家具の向こうにいるおもちゃは選ばない。
		FHitResult Block;
		FCollisionQueryParams Params(TEXT("TitleToyClick"), false, *It);
		if (GetWorld()->LineTraceSingleByChannel(Block, Origin, Hit, ECC_Visibility, Params)) continue;
		Best = Emote;
		BestMiss = Miss;
	}
	return Best;
}

// タイトルのキー設定でも使うので。
void ATBTitleController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	if (IsLocalController())
	{
		UTBInputSettings::RegisterContexts(GetLocalPlayer());
	}
}

void ATBTitleController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Menu) Menu->RemoveFromParent();
	Super::EndPlay(Reason);
}

bool ATBTitleController::IsStoryPlaying() const
{
	return Menu && Menu->GetState() == UTBTitleMenu::EState::Story;
}

void ATBTitleController::TBHost() { GetGameInstance()->GetSubsystem<UTBSession>()->Host(); }
void ATBTitleController::TBFind() { GetGameInstance()->GetSubsystem<UTBSession>()->Find(); }
void ATBTitleController::TBJoin(int32 Index) { GetGameInstance()->GetSubsystem<UTBSession>()->Join(Index); }
void ATBTitleController::TBLeave() { GetGameInstance()->GetSubsystem<UTBSession>()->Leave(); }
