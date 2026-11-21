#include "TBRoomLighting.h"
#include "TBRuleMath.h"
#include "Data/TBRuleData.h"
#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Engine/Light.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

void ATBRoomLighting::ApplyStageLightRanges(ULevel& Level)
{
	struct FFixtureRange
	{
		FName ActorName;
		float MaxRadiusCm;
	};
	// Scope to placed fixtures. Box-room lighting, character lights and newly authored lights keep their own ranges.
	static const FFixtureRange HouseLights[] = {
		{TEXT("PointLight2"), 2000.f}, {TEXT("PointLight3"), 2000.f}, {TEXT("PointLight4"), 2000.f},
		{TEXT("RectLight5"), 900.f}, {TEXT("RectLight_1"), 900.f},
		{TEXT("SpotLight5"), 900.f}, {TEXT("SpotLight6"), 900.f}, {TEXT("SpotLight7"), 900.f},
		{TEXT("SpotLight2_4"), 900.f}, {TEXT("SpotLight3"), 900.f}
	};
	static const FFixtureRange DemoLights[] = {
		{TEXT("PointLight_1"), 650.f}, {TEXT("RectLight_0"), 650.f},
		{TEXT("RectLight_1"), 650.f}, {TEXT("RectLight_2"), 650.f}
	};
	const FString Package = UWorld::RemovePIEPrefix(Level.GetOutermost()->GetName());
	TConstArrayView<FFixtureRange> Fixtures;
	if (Package == TEXT("/Game/Maps/TB_StylizedHouse")) Fixtures = HouseLights;
	else if (Package == TEXT("/Game/Maps/TB_ToyWorldDemoExpanded")) Fixtures = DemoLights;
	else return;

	for (const FFixtureRange& Fixture : Fixtures)
	{
		const ALight* Light = FindObjectFast<ALight>(&Level, Fixture.ActorName);
		auto* LocalLight = Light ? Cast<ULocalLightComponent>(Light->GetLightComponent()) : nullptr;
		if (!LocalLight || LocalLight->AttenuationRadius <= Fixture.MaxRadiusCm) continue;
		// SetAttenuationRadius skips registered static fixtures. These stages use dynamic lighting only.
		// Recreate the proxy once so shadow interactions and GPU Scene both receive the same bounds.
		LocalLight->AttenuationRadius = Fixture.MaxRadiusCm;
		LocalLight->MarkRenderStateDirty();
	}
}

ATBRoomLighting::ATBRoomLighting()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RoomBounds"));
	SetRootComponent(Bounds);
	Bounds->SetBoxExtent(FVector(400, 400, 200));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);
}

void ATBRoomLighting::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) bLightsOn = bInitiallyLightsOn;
	OnRep_LightsOn();
}

void ATBRoomLighting::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATBRoomLighting, bLightsOn);
}

void ATBRoomLighting::SetLightsOn(bool bOn)
{
	if (!HasAuthority()) return;
	bLightsOn = bOn;
	OnRep_LightsOn();
	ForceNetUpdate();
}

void ATBRoomLighting::ResetLighting()
{
	SetLightsOn(bInitiallyLightsOn);
}

void ATBRoomLighting::OnRep_LightsOn()
{
	for (ALight* Light : Lights)
	{
		if (IsValid(Light) && Light->GetLightComponent())
		{
			Light->GetLightComponent()->SetVisibility(bLightsOn);
		}
	}
}

bool ATBRoomLighting::Contains(const FVector& Position) const
{
	const FVector Local = Bounds->GetComponentTransform().InverseTransformPosition(Position);
	const FVector Extent = Bounds->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

bool ATBRoomLighting::ProtectsFromGaze(const UWorld* World, const FVector& ToyPosition, const FVector& HumanPosition)
{
	if (!World) return false;
	bool bInDarkRoom = false;
	const double DistanceSquared = FVector::DistSquared(ToyPosition, HumanPosition);
	for (TActorIterator<ATBRoomLighting> It(World); It; ++It)
	{
		if (!It->Contains(ToyPosition)) continue;
		if (!TBRuleMath::DarknessProtects(It->bLightsOn, DistanceSquared, TB::Rules().Vision.DarkProtectionDistanceMeters)) return false;
		bInDarkRoom = true;
	}
	return bInDarkRoom;
}
