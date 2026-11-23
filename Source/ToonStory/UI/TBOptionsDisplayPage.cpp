// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Core/TBGameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TBOptionRow.h"
#include "TBOptionsMenu.h"
#include "TBOptionsPages.h"

namespace
{
	const EWindowMode::Type WindowModes[] = {EWindowMode::Windowed, EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen};
	const TCHAR* WindowModeNames[] = {TEXT("Options.Windowed"), TEXT("Options.Borderless"), TEXT("Options.Fullscreen")};
	const TCHAR* QualityNames[] = {TEXT("Options.Low"), TEXT("Options.Medium"), TEXT("Options.High"), TEXT("Options.Ultra")};

	int32 WindowModeIndex(const UTBGameUserSettings& Settings)
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(WindowModes); ++Index)
		{
			if (WindowModes[Index] == Settings.GetFullscreenMode())
			{
				return Index;
			}
		}
		return 1;
	}

	// 今の画面モードで選べる解像度（小さい順）。ボーダーレスは画面全体なので選ばせない。
	TArray<FIntPoint> Resolutions(const UTBGameUserSettings& Settings)
	{
		TArray<FIntPoint> Result;
		if (Settings.GetFullscreenMode() == EWindowMode::Fullscreen)
		{
			UKismetSystemLibrary::GetSupportedFullscreenResolutions(Result);
		}
		else if (Settings.GetFullscreenMode() == EWindowMode::Windowed)
		{
			UKismetSystemLibrary::GetConvenientWindowedResolutions(Result);
		}
		Result.AddUnique(Settings.GetScreenResolution());
		Result.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		return Result;
	}
}

// エディタで遊んでいるときは、エディタのウィンドウを変えてしまうので解像度と画面モードは触らない。
void TBOptionsPages::BuildDisplay(UTBOptionsMenu& Menu, UTBGameUserSettings& Settings)
{
	auto* S = &Settings;
	auto* M = &Menu;
	const bool bCanChangeWindow = !GIsEditor;
	auto* Mode = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.WindowMode")), false);
	Mode->Value = [S] { return TBText::String(WindowModeNames[WindowModeIndex(*S)]); };
	Mode->Choices = [bCanChangeWindow]
	{
		TArray<FString> Names;
		if (bCanChangeWindow)
			for (const TCHAR* Name : WindowModeNames) Names.Add(TBText::String(Name));
		return Names;
	};
	Mode->Select = [M](int32 Index)
	{
		M->ChangeVideoMode([Index](UTBGameUserSettings& Changing)
		{
			const auto Next = WindowModes[Index];
			const FIntPoint Desktop = Changing.GetDesktopResolution();
			Changing.SetFullscreenMode(Next);
			if (Next == EWindowMode::WindowedFullscreen)
			{
				Changing.SetScreenResolution(Desktop);
			}
			else if (Next == EWindowMode::Windowed && Changing.GetScreenResolution().X >= Desktop.X)
			{
				// 画面と同じ大きさのウィンドウは端がはみ出すので、一回り小さくする。
				for (const FIntPoint& Candidate : Resolutions(Changing))
				{
					if (Candidate.X < Desktop.X && Candidate.Y < Desktop.Y)
					{
						Changing.SetScreenResolution(Candidate);
					}
				}
			}
		});
	};
	auto* Resolution = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.Resolution")), false);
	Resolution->Value = [S]
	{
		const FIntPoint Size = S->GetFullscreenMode() == EWindowMode::WindowedFullscreen ? S->GetDesktopResolution()
		                                                                                : S->GetScreenResolution();
		return FString::Printf(TEXT("%d × %d"), Size.X, Size.Y);
	};
	Resolution->Choices = [S, bCanChangeWindow]
	{
		TArray<FString> Names;
		if (bCanChangeWindow && S->GetFullscreenMode() != EWindowMode::WindowedFullscreen)
			for (const FIntPoint& Size : Resolutions(*S)) Names.Add(FString::Printf(TEXT("%d × %d"), Size.X, Size.Y));
		return Names;
	};
	Resolution->Select = [M, S](int32 Index)
	{
		const auto Available = Resolutions(*S);
		if (Available.IsValidIndex(Index))
			M->ChangeVideoMode([Size = Available[Index]](UTBGameUserSettings& Changing) { Changing.SetScreenResolution(Size); });
	};
	auto* VSync = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.VSync")), false);
	VSync->Value = [S] { return OnOff(S->IsVSyncEnabled()); };
	VSync->Choices = ToggleChoices;
	VSync->Select = [S](int32 Index)
	{
		S->SetVSyncEnabled(Index != 0);
		S->CommitDisplay();
	};
	auto* FrameRate = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.FrameLimit")), false);
	FrameRate->Value = [S]
	{
		const int32 Limit = FMath::RoundToInt(S->GetFrameRateLimit());
		return Limit > 0 ? FString::FromInt(Limit) : FString(TBText::Literal(TEXT("Options.Unlimited")));
	};
	FrameRate->Choices = []
	{
		TArray<FString> Names;
		for (int32 Limit : TB::ConfigData().Display.FrameRateLimits)
			Names.Add(Limit > 0 ? FString::FromInt(Limit) : TBText::String(TEXT("Options.Unlimited")));
		return Names;
	};
	FrameRate->Select = [S](int32 Index)
	{
		S->SetFrameRateLimit(TB::ConfigData().Display.FrameRateLimits[Index]);
		S->CommitDisplay();
	};
	auto* Quality = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.Quality")), false);
	Quality->Value = [S]
	{
		const int32 Level = S->GetQualityPreset();
		return Level >= 0 ? TBText::String(QualityNames[FMath::Min(Level, 3)]) : FString(TBText::Literal(TEXT("Options.Custom")));
	};
	Quality->Choices = []
	{
		TArray<FString> Names;
		for (const TCHAR* Name : QualityNames) Names.Add(TBText::String(Name));
		return Names;
	};
	Quality->Select = [S](int32 Index)
	{
		S->SetQualityPreset(Index);
		S->CommitDisplay();
	};
	auto* ResolutionScale = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.ResolutionScale")), true);
	ResolutionScale->Value = [S] { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S->GetResolutionScalePercent())); };
	ResolutionScale->Fill = [S]
	{
		const auto& Range = TB::ConfigData().Display.ResolutionScale;
		return Fraction(S->GetResolutionScalePercent(), Range.Min, FMath::Min(Range.Max, 100.f));
	};
	// 描画解像度は変えるたびに画面の作り直しが走るので、手を離したときだけ反映する。
	const auto& ScaleRange = TB::ConfigData().Display.ResolutionScale;
	BindSlider(*ResolutionScale, ScaleRange.Min, FMath::Min(ScaleRange.Max, 100.f), ScaleRange.Step,
	           [S](float Percent) { S->SetResolutionScalePercent(Percent); }, nullptr, [S] { S->CommitDisplay(); });
	auto* Brightness = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.Brightness")), true);
	Brightness->Value = [S]
	{
		const auto& Range = TB::ConfigData().Display.Brightness;
		const int32 Offset = FMath::RoundToInt((S->GetBrightness() - Range.Default) / FMath::Max(Range.Step, KINDA_SMALL_NUMBER));
		return Offset == 0 ? FString(TBText::Literal(TEXT("Options.Normal"))) : FString::Printf(TEXT("%+d"), Offset);
	};
	Brightness->Fill = [S]
	{
		const auto& Range = TB::ConfigData().Display.Brightness;
		return Fraction(S->GetBrightness(), Range.Min, Range.Max);
	};
	const auto& BrightnessRange = TB::ConfigData().Display.Brightness;
	BindSlider(*Brightness, BrightnessRange.Min, BrightnessRange.Max, BrightnessRange.Step,
	           [S](float Gamma) { S->SetBrightness(Gamma); }, [S] { S->PreviewBrightness(); }, [S] { S->CommitDisplay(); });
	auto* MotionBlur = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.MotionBlur")), false);
	MotionBlur->Value = [S] { return OnOff(S->IsMotionBlurEnabled()); };
	MotionBlur->Choices = ToggleChoices;
	MotionBlur->Select = [S](int32 Index)
	{
		S->SetMotionBlurEnabled(Index != 0);
		S->CommitDisplay();
	};
	auto* RayTracing = Menu.AddRow(TBOptionsPage::Display, TBText::Get(TEXT("Options.RayTracing")), false);
	RayTracing->Value = [S]
	{
		return UTBGameUserSettings::IsRayTracingSupported() ? OnOff(S->IsRayTracingOn()) : FString(TBText::Literal(TEXT("Options.Unsupported")));
	};
	RayTracing->Choices = [] { return UTBGameUserSettings::IsRayTracingSupported() ? ToggleChoices() : TArray<FString>(); };
	RayTracing->Select = [S](int32 Index)
	{
		S->SetRayTracingOn(Index != 0);
		S->CommitDisplay();
	};
	Menu.AddNote(TBOptionsPage::Display,
	             FText::Format(TBText::Get(TEXT("Options.VideoModeNote")), FMath::RoundToInt(TB::ConfigData().Display.VideoModeConfirmSeconds)));
}
