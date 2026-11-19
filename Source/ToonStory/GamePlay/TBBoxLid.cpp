// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBBoxLid.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/TBGameHelpers.h"
#include "Core/TBGameState.h"
#include "Data/TBPresentationData.h"
#include "GamePlay/TBBox.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"

ATBBoxLid::ATBBoxLid()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Light = CreateDefaultSubobject<USpotLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeRotation(FRotator(-90, 0, 0));
	Light->SetIntensity(0.f);
	Light->SetCastShadows(false);
	// 天井全体から光が落ちてくるように、部屋全体へ広げる。
	Light->SetInnerConeAngle(0.f);
	Light->SetOuterConeAngle(80.f);
}

void ATBBoxLid::BeginPlay()
{
	Super::BeginPlay();
	if (Ceiling)
	{
		if (auto* Mesh = Ceiling->FindComponentByClass<UStaticMeshComponent>())
		{
			CeilingMaterial = Mesh->CreateDynamicMaterialInstance(0);
		}
		// 毎フレーム動かすので、レベル側が Static のままでも動けるようにする。
		Ceiling->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		CeilingRest = Ceiling->GetActorTransform();
		Ceiling->GetActorBounds(false, ClosedOrigin, ClosedExtent);
		// 側面をPivotにして、反対側を持ち上げる。
		Hinge = ClosedOrigin + FVector(0.f, HingeSign() * ClosedExtent.Y, ClosedExtent.Z);
		const FVector Arm = CeilingRest.GetLocation() - Hinge;
		OpenSign = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(1.f)).RotateVector(Arm).Z > Arm.Z ? 1.f : -1.f;
	}
	if (RoomVolume)
	{
		RestExposure = RoomVolume->Settings.AutoExposureBias;
		bRestExposureOverride = RoomVolume->Settings.bOverride_AutoExposureBias;
	}
	Play = StandardPlay();
	SetOpen(0.f);
	SetFlash(0.f);
}

void ATBBoxLid::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 箱は GameState 経由で後から届くことがあるので、見つかるまで探して収納・救助の合図に登録する。
	if (!bBoundToBox)
	{
		const auto* State = TB::GS(GetWorld());
		if (State && State->Box)
		{
			State->Box->OnOpened.AddUObject(this, &ATBBoxLid::Open);
			State->Box->OnEscapeOpened.AddUObject(this, &ATBBoxLid::OpenForEscape);
			bBoundToBox = true;
		}
	}
	if (bResultHeld)
	{
		// リザルトを抜けたら（ロビーへ戻る・次の試合）手放す。
		const auto* State = TB::GS(GetWorld());
		if (!State || State->Phase != ETBPhase::Results) HoldForResult(-1.f);
		return;
	}
	if (Elapsed < 0.f)
	{
		return;
	}
	Elapsed += DeltaSeconds;
	const float Closing = Elapsed - Play.OpenSeconds - Play.HoldSeconds;
	float Alpha =
	    Elapsed < Play.OpenSeconds ? Elapsed / Play.OpenSeconds : Closing < 0.f ? 1.f : 1.f - Closing / Play.CloseSeconds;
	const float Flash =
	    Elapsed < Play.OpenSeconds ? Elapsed / Play.OpenSeconds : 1.f - (Elapsed - Play.OpenSeconds) / Play.FlashSeconds;
	SetFlash(FMath::Clamp(Flash, 0.f, 1.f));
	if (Alpha <= 0.f)
	{
		Alpha = 0.f;
		Elapsed = -1.f;
	}
	SetOpen(Alpha);
	// 閉じきったら、リザルトの演出などのために救助・収納の大きさへ戻しておく。
	if (Elapsed < 0.f) Play = StandardPlay();
}

ATBBoxLid::FPlay ATBBoxLid::StandardPlay()
{
	const auto& Lid = TB::PresentationData().Lid;
	FPlay Out;
	Out.Angle = Lid.OpenAngle;
	Out.PeakIntensity = Lid.PeakIntensity;
	Out.OpenSeconds = Lid.OpenSeconds;
	Out.HoldSeconds = Lid.HoldSeconds;
	Out.CloseSeconds = Lid.CloseSeconds;
	Out.FlashExposure = Lid.FlashExposure;
	Out.FlashSeconds = Lid.FlashSeconds;
	return Out;
}

ATBBoxLid::FPlay ATBBoxLid::EscapePlay()
{
	const auto& Lid = TB::PresentationData().Lid;
	FPlay Out = StandardPlay();
	Out.Angle = Lid.EscapeOpenAngle;
	Out.PeakIntensity = Lid.EscapePeakIntensity;
	Out.OpenSeconds = Lid.EscapeOpenSeconds;
	Out.HoldSeconds = Lid.EscapeHoldSeconds;
	Out.FlashExposure = Lid.EscapeFlashExposure;
	Out.bEscape = true;
	return Out;
}

void ATBBoxLid::Open()
{
	Start(StandardPlay());
}

void ATBBoxLid::OpenForEscape()
{
	Start(EscapePlay());
}

void ATBBoxLid::Start(const FPlay& Next)
{
	if (Elapsed < 0.f)
	{
		Play = Next;
		Elapsed = 0.f;
		return;
	}
	// 脱出で大きく開いている間は、救助・収納の合図で小さくしない。
	if (Play.bEscape && !Next.bEscape) return;
	// 開いている途中で次の合図が来たら、閉じずに今の実際の角度から開き直す。描画は SmoothStep なので、その逆で進み具合を求める。
	const float Closing = Elapsed - Play.OpenSeconds - Play.HoldSeconds;
	const float Alpha = Elapsed < Play.OpenSeconds ? Elapsed / Play.OpenSeconds
	                    : Closing > 0.f             ? FMath::Max(0.f, 1.f - Closing / Play.CloseSeconds)
	                                                : 1.f;
	const float Current = Play.Angle * FMath::SmoothStep(0.f, 1.f, Alpha);
	const float Eased = FMath::Clamp(Current / FMath::Max(Next.Angle, KINDA_SMALL_NUMBER), 0.f, 1.f);
	// SmoothStep(t)=3t^2-2t^3 の逆: t = 1/2 - sin(asin(1-2s)/3)
	const float NextAlpha = .5f - FMath::Sin(FMath::Asin(1.f - 2.f * Eased) / 3.f);
	Play = Next;
	Elapsed = FMath::Clamp(NextAlpha, 0.f, 1.f) * Play.OpenSeconds;
}

void ATBBoxLid::HoldForResult(float Alpha)
{
	if (Alpha < 0.f)
	{
		if (!bResultHeld) return;
		bResultHeld = false;
		SetOpen(0.f);
		SetFlash(0.f);
		return;
	}
	bResultHeld = true;
	Elapsed = -1.f;
	Play = StandardPlay();
	SetOpen(FMath::Clamp(Alpha, 0.f, 1.f));
	SetFlash(0.f);
}

bool ATBBoxLid::ResultShot(FTransform& Out) const
{
	if (!Ceiling) return false;
	// 救助でふたが開いている途中に決着しても、閉じた天井から決める。
	const FVector& Origin = ClosedOrigin;
	const FVector& Extent = ClosedExtent;
	// 床は落下エリアの底。無ければ天井から2.5m下とみなす。
	const auto* Drop = ATBBox::FindDropArea(GetWorld());
	const float FloorZ = Drop ? Drop->GetComponentLocation().Z - Drop->GetScaledBoxExtent().Z : Origin.Z - 250.f;
	// 部屋の隅寄りの低い位置（蝶番の側）から、ふたの持ち上がる側を見上げる。
	const float Side = HingeSign();
	const FVector Eye(Origin.X - Extent.X * .5f, Origin.Y + Side * Extent.Y * .5f, FloorZ + 40.f);
	const FVector Look(Origin.X, Origin.Y - Side * Extent.Y * .3f, Origin.Z);
	Out = FTransform((Look - Eye).Rotation(), Eye);
	return true;
}

float ATBBoxLid::HingeSign() const
{
	return HingeSide == ETBLidHingeSide::NegativeY ? -1.f : 1.f;
}

ATBBoxLid* ATBBoxLid::Find(const UWorld* World)
{
	TActorIterator<ATBBoxLid> It(World);
	return It ? *It : nullptr;
}

void ATBBoxLid::SetOpen(float Alpha)
{
	const float Eased = FMath::SmoothStep(0.f, 1.f, Alpha);
	Light->SetIntensity(Play.PeakIntensity * Eased);
	if (CeilingMaterial)
	{
		CeilingMaterial->SetScalarParameterValue(TEXT("Glow"), Eased);
	}
	if (Ceiling)
	{
		const FQuat Tilt(FVector::ForwardVector, FMath::DegreesToRadians(Play.Angle * Eased) * OpenSign);
		Ceiling->SetActorLocationAndRotation(Hinge + Tilt.RotateVector(CeilingRest.GetLocation() - Hinge),
		                                     Tilt * CeilingRest.GetRotation());
	}
}

void ATBBoxLid::SetFlash(float Alpha)
{
	if (RoomVolume)
	{
		// 光っている間だけ上書きし、終わったら元の設定に戻す。
		RoomVolume->Settings.bOverride_AutoExposureBias = Alpha > 0.f || bRestExposureOverride;
		RoomVolume->Settings.AutoExposureBias = RestExposure + Play.FlashExposure * Alpha;
	}
}
