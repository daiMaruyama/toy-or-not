#include "GamePlay/Items/TBEscapeVehicle.h"
#include "Audio/TBAudioSubsystem.h"
#include "Camera/CameraTypes.h"
#include "Components/AudioComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TBGameHelpers.h"
#include "Core/TBGameState.h"
#include "Core/TBLobbySettings.h"
#include "Core/TBPlayerState.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"

namespace
{
	// Match the 24 fps storyboard, interpolating between its frames at the display's frame rate.
	constexpr double RelayFrame = 25.;
	constexpr double ReadyFrame = 63.;
	double Ease(double T)
	{
		T = FMath::Clamp(T, 0., 1.);
		return T * T * (3. - 2. * T);
	}
	double Roll(double Frame) { return 7.2 * Ease((Frame - ReadyFrame - 8.) / 19.); }
}

void ATBEscapeVehicle::CalcCamera(float DeltaTime, FMinimalViewInfo& Out)
{
	const auto* Match = TB::GS(GetWorld());
	if (!Match || !Match->IsIgniting() || Match->Ignition.Vehicle != this)
	{
		Super::CalcCamera(DeltaTime, Out);
		return;
	}
	const double Frame = Match->IgnitionElapsed() * 24.;
	FVector Eye, Aim;
	if (Frame < RelayFrame)
	{
		Eye = FMath::Lerp(FVector(159,-96,29), FVector(143,-82,25), FMath::Min(Frame / 24., 1.));
		// The Demo car is tucked beside a cabinet. Approach from the aisle.
		if (TBLobbySettings::StageIndex(GetWorld()) == TBLobbySettings::DemoStageId) Eye.Y *= -1.;
		Aim = FVector(1,0,0);
		Out.FOV = 49.f;
	}
	else if (Frame < ReadyFrame)
	{
		Eye = FMath::Lerp(FVector(101,37,1), FVector(97,30,-.5), FMath::Min((Frame - RelayFrame) / 37., 1.));
		Aim = FVector(31,0,-3.5);
		Out.FOV = 42.f;
	}
	else
	{
		Eye = FMath::Lerp(FVector(136,84,14), FVector(150,106,23), Ease((Frame - ReadyFrame) / 47.));
		Aim = FVector(4. + .65 * Roll(Frame),0,0);
		Out.FOV = 48.f;
	}
	const FTransform& Car = GetActorTransform();
	Out.Location = Car.TransformPosition(ShotCenter + Eye);
	Out.Rotation = (Car.TransformPosition(ShotCenter + Aim) - Out.Location).Rotation();
	Out.AspectRatio = 16.f / 9.f;
	Out.bConstrainAspectRatio = true;
	Out.PerspectiveNearClipPlane = 2.f * GetActorScale3D().GetAbsMin();
}

void ATBEscapeVehicle::StartIgnitionEffects(float Elapsed, USoundBase* Sound)
{
	LampRig = NewObject<USceneComponent>(this);
	LampRig->SetupAttachment(Mesh);
	// The reflector coordinates were measured in parked vehicle space.
	LampRig->SetRelativeTransform(FTransform(ShotCenter).GetRelativeTransform(Mesh->GetRelativeTransform()));
	LampRig->RegisterComponent();
	const float Scale = GetActorScale3D().GetAbsMin();
	LensGlow = UMaterialInstanceDynamic::Create(LensMaterial, this);
	if (LensGlow) LensGlow->SetScalarParameterValue(TEXT("Brightness"), 0.f);
	for (float Side : {-1.f, 1.f})
	{
		// Outline of the two inset reflectors on SM_ToyCar_01, in vehicle space.
		const FVector Across(.78,-Side*5.94,.47), Down(.26,-Side*.24,-4.32);
		auto* Lens = NewObject<UStaticMeshComponent>(this);
		Lens->SetupAttachment(LampRig);
		Lens->SetStaticMesh(LensPlane);
		Lens->SetMaterial(0, LensGlow);
		Lens->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lens->SetCanEverAffectNavigation(false);
		Lens->SetCastShadow(false);
		Lens->SetRelativeLocation(FVector(52.88,Side*16.32,-1.55));
		Lens->SetRelativeRotation(FRotationMatrix::MakeFromXY(Across, Down).Rotator());
		Lens->SetRelativeScale3D(FVector(Across.Size()/100., Down.Size()/100., 1.));
		Lens->RegisterComponent();

		auto* Light = NewObject<USpotLightComponent>(this);
		Light->SetupAttachment(LampRig);
		Light->SetRelativeLocation(FVector(54,Side*16.5,-1));
		Light->SetRelativeRotation(FVector(1,Side*.045,-.25).Rotation());
		Light->SetAttenuationRadius(330.f * Scale);
		Light->SetInnerConeAngle(22.f);
		Light->SetOuterConeAngle(38.f);
		Light->SetLightColor(FLinearColor(1,.81,.53));
		Light->SetCastShadows(false);
		Light->SetIntensity(0);
		Light->RegisterComponent();
		Headlights.Add(Light);
	}
	const auto* Audio = GetGameInstance()->GetSubsystem<UTBAudioSubsystem>();
	const float Volume = Audio ? Audio->GetCategoryVolume(ETBAudioCategory::Effects) : 1.f;
	MotorAudio = UGameplayStatics::SpawnSound2D(this, Sound, Volume, 1.f, Elapsed);
}

void ATBEscapeVehicle::UpdateIgnitionEffects(const ATBGameState* Match)
{
	if (!Match || !Match->IsIgniting() || Match->Ignition.Vehicle != this)
	{
		if (LampRig) StopIgnitionEffects();
		return;
	}
	const double Elapsed = Match->IgnitionElapsed();
	if (!LampRig)
	{
		const auto* Local = GetGameInstance()->GetFirstLocalPlayerController(GetWorld());
		const auto* Listener = Local ? Local->GetPlayerState<ATBPlayerState>() : nullptr;
		// On a client the listener's team can arrive after the shared ignition state.
		if (!Listener || Listener->Team == ETBTeam::None) return;
		USoundBase* Sound = Listener->Team == ETBTeam::Toy && ToyIgnitionSound ? ToyIgnitionSound.Get() : IgnitionSound.Get();
		StartIgnitionEffects(Elapsed, Sound);
	}
	const double Frame = Elapsed * 24.;
	const double Relay = Frame - RelayFrame;
	const double Level = Ease((Relay - 9.) / 4.) * (1. - .25 * FMath::Exp(-FMath::Square((Relay - 15.) / 1.5)));
	const double Kick = Frame - ReadyFrame - 8.;
	const double Bounce = Frame < ReadyFrame ? .10 * FMath::Sin(Relay * 1.7) * Level :
		Kick < 0. ? 0. : .28 * FMath::Sin(Kick * .95) * FMath::Exp(-Kick / 14.);
	const FVector Shift(Roll(Frame), 0, Bounce);
	Mesh->SetRelativeLocation(ParkedMeshLocation + Shift);
	if (LensGlow) LensGlow->SetScalarParameterValue(TEXT("Brightness"), 4.5f * Level);
	const float Intensity = 170.f * Level * FMath::Square(GetActorScale3D().GetAbsMin());
	for (USpotLightComponent* Lamp : Headlights) Lamp->SetIntensity(Intensity);
	if (IsValid(MotorAudio))
		if (const auto* Audio = GetGameInstance()->GetSubsystem<UTBAudioSubsystem>())
		{
			const float Volume = Audio->GetCategoryVolume(ETBAudioCategory::Effects);
			if (MotorAudio->VolumeMultiplier != Volume) MotorAudio->SetVolumeMultiplier(Volume);
		}
}

void ATBEscapeVehicle::StopIgnitionEffects()
{
	if (!LampRig) return;
	if (IsValid(MotorAudio)) MotorAudio->Stop();
	MotorAudio = nullptr;
	Mesh->SetRelativeLocation(ParkedMeshLocation);
	const auto Parts = LampRig->GetAttachChildren();
	for (USceneComponent* Part : Parts) Part->DestroyComponent();
	LampRig->DestroyComponent();
	LampRig = nullptr;
	LensGlow = nullptr;
	Headlights.Reset();
}
