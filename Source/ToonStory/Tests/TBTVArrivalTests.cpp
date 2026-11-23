// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "GamePlay/TBBoxTV.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

// 箱に入ったおもちゃの向きは、目の位置から画面の中心を通る。テレビを動かしても大きさを変えても、リモコンの高さ（目の高さ）が
// 変わっても、その中心を通る。実際のマップのテレビで動かして確かめる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBTVArrivalAimTest, "ToonStory.Television.ArrivalAim",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBTVArrivalAimTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
	ATBBoxTV* TV = nullptr;
	if (World) { TActorIterator<ATBBoxTV> It(World); if (It) TV = *It; }
	if (!TestNotNull(TEXT("Authored television"), TV) || !TestNotNull(TEXT("Screen mesh"), TV ? TV->Screen.Get() : nullptr)) return false;
	const FTransform Original = TV->GetActorTransform();
	const auto OriginalMobility = TV->GetRootComponent()->Mobility;
	TV->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	ON_SCOPE_EXIT
	{
		TV->SetActorTransform(Original, false, nullptr, ETeleportType::TeleportPhysics);
		TV->GetRootComponent()->SetMobility(OriginalMobility);
	};
	// 画面の中心を、テレビのアクタから見た位置として覚えておく。動かしたり大きさを変えたら、同じ位置に付いてくるはず。
	const FVector LocalCenter = Original.InverseTransformPosition(TV->ScreenCenter());
	struct FCase { FVector Move; FVector Scale; };
	const FCase Cases[] = {{FVector::ZeroVector, FVector(1.f)}, {FVector(0, 0, 50), FVector(1.f)}, {FVector(-40, 30, 0), FVector(1.f)},
	                       {FVector(0, 0, 0), FVector(1.5f)}, {FVector(20, -50, 30), FVector(.8f)}};
	// 目の高さ。リモコンの高さや着地した面で変わる。
	const double EyeHeights[] = {30., 63., 78., 120.};
	for (const FCase& Case : Cases)
	{
		FTransform Moved = Original;
		Moved.AddToTranslation(Case.Move);
		Moved.SetScale3D(Original.GetScale3D() * Case.Scale);
		TV->SetActorTransform(Moved, false, nullptr, ETeleportType::TeleportPhysics);
		const FVector Center = TV->ScreenCenter();
		const FVector Expected = Moved.TransformPosition(LocalCenter);
		TestTrue(*FString::Printf(TEXT("Screen center follows the television (move %s scale %.1f): %s vs %s"), *Case.Move.ToString(), Case.Scale.X,
		                          *Center.ToString(), *Expected.ToString()), Center.Equals(Expected, .5));
		const FVector Floor = Center - FVector(330, 40, 0);
		for (const double Height : EyeHeights)
		{
			const FVector Eye(Floor.X, Floor.Y, Height);
			const FRotator Aim = ATBBoxTV::ArrivalRotation(Eye, Center);
			const double Distance = FVector::Dist(Eye, Center);
			const FVector Through = Eye + Aim.Vector() * Distance;
			TestTrue(*FString::Printf(TEXT("Aim from eye height %.0f passes through the screen center (off by %.2f)"), Height, FVector::Dist(Through, Center)),
			         Through.Equals(Center, .5));
			// ヨーもピッチも、同じ目から画面の中心への向き。
			const FVector Delta = Center - Eye;
			TestTrue(TEXT("Yaw is toward the screen center"),
			         FMath::Abs(FMath::FindDeltaAngleDegrees(Aim.Yaw, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)))) < .01);
			TestTrue(TEXT("Pitch is toward the screen center"),
			         FMath::Abs(Aim.Pitch - FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y)))) < .01);
		}
		// 目が画面の中心より高ければ見下ろし、低ければ見上げる（以前のように、決まった高さだけ見上げない）。
		TestTrue(TEXT("An eye above the center looks down"), ATBBoxTV::ArrivalRotation(Center + FVector(-300, 0, 40), Center).Pitch < 0.f);
		TestTrue(TEXT("An eye below the center looks up"), ATBBoxTV::ArrivalRotation(Center + FVector(-300, 0, -40), Center).Pitch > 0.f);
	}
	return !HasAnyErrors();
}
#endif
