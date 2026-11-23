#pragma once
#include "UI/TBUIText.h"
#include "CoreMinimal.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace TBPaper
{
	inline const FLinearColor PrintInk = FLinearColor::FromSRGBColor(FColor(29, 53, 56));
	inline const FLinearColor PrintSea = FLinearColor::FromSRGBColor(FColor(34, 84, 89));
	inline const FLinearColor PrintCream = FLinearColor::FromSRGBColor(FColor(255, 246, 223));
	inline const FLinearColor PrintHoney = FLinearColor::FromSRGBColor(FColor(255, 206, 89));
	inline const FLinearColor PrintAqua = FLinearColor::FromSRGBColor(FColor(150, 214, 199));
	inline const FLinearColor PrintStock = FLinearColor::FromSRGBColor(FColor(246, 232, 197));
	inline const FLinearColor PrintQuiet = FLinearColor::FromSRGBColor(FColor(102, 105, 91));
	inline FButtonStyle PrintButtonStyle(FLinearColor Face)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Face, 1.f, PrintInk, 2.f));
		Style.SetHovered(FSlateRoundedBoxBrush(PrintAqua, 1.f, PrintInk, 2.f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(238, 120, 94)), 1.f, PrintInk, 2.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(221, 215, 194)), 1.f, PrintInk.CopyWithNewOpacity(.55f), 1.f));
		Style.SetNormalPadding(FMargin(8, 2)).SetPressedPadding(FMargin(9, 3, 7, 1));
		return Style;
	}
	inline const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(53, 24, 5));
	inline const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor(177, 64, 46));
	inline const FLinearColor Cream = FLinearColor::FromSRGBColor(FColor(248, 223, 179));
	inline const FLinearColor Muted = FLinearColor::FromSRGBColor(FColor(105, 86, 72));
	inline const FLinearColor Paper = FLinearColor::FromSRGBColor(FColor(255, 237, 207));
	inline const FLinearColor Teal = FLinearColor::FromSRGBColor(FColor(89, 189, 181));
	inline const FLinearColor HumanBlue = FLinearColor::FromSRGBColor(FColor(44, 126, 152));
	inline const FLinearColor Gold = FLinearColor::FromSRGBColor(FColor(255, 214, 91));
	inline const FLinearColor Danger = FLinearColor::FromSRGBColor(FColor(166, 48, 40));
	inline const FLinearColor Outline = FLinearColor::FromSRGBColor(FColor(59, 27, 9));
	inline const FLinearColor Shadow = FLinearColor(0.025f, 0.012f, 0.006f, .24f);
	enum class EFontRole : uint8 { Body, Heading, Display, Numeric, Hand };
	inline FString Status(const FString& Value)
	{
		if (Value == TEXT("Create a room or find friends to begin."))
		{
			return TBText::Literal(TEXT("Root.Status.Begin"));
		}
		if (Value == TEXT("Creating room..."))
		{
			return TBText::Literal(TEXT("Root.Status.Creating"));
		}
		if (Value == TEXT("Searching rooms..."))
		{
			return TBText::Literal(TEXT("Root.Status.Searching"));
		}
		if (Value == TEXT("Joining room..."))
		{
			return TBText::Literal(TEXT("Root.Status.Joining"));
		}
		if (Value == TEXT("Connecting to host..."))
		{
			return TBText::Literal(TEXT("Root.Status.Connecting"));
		}
		if (Value == TEXT("Leaving room..."))
		{
			return TBText::Literal(TEXT("Root.Status.Leaving"));
		}
		if (Value == TEXT("Room creation failed."))
		{
			return TBText::Literal(TEXT("Root.Status.CreateFailed"));
		}
		if (Value == TEXT("Search failed."))
		{
			return TBText::Literal(TEXT("Root.Status.SearchFailed"));
		}
		if (Value == TEXT("Join failed."))
		{
			return TBText::Literal(TEXT("Root.Status.JoinFailed"));
		}
		if (Value == TEXT("Could not resolve connection."))
		{
			return TBText::Literal(TEXT("Root.Status.ConnectFailed"));
		}
		if (Value == TEXT("Offline. Last match ended; TBHost / TBFind for a new lobby."))
		{
			return TBText::Literal(TEXT("Root.Status.Welcome"));
		}
		return TBText::TranslateNotice(Value);
	}

	inline UFont* Font(UObject* Outer, EFontRole Role)
	{
		auto* Result = NewObject<UFont>(Outer);
		Result->FontCacheType = EFontCacheType::Runtime;
		Result->LegacyFontSize = 14;
		const TCHAR* File = Role == EFontRole::Heading ? TEXT("mochiy.ttf") :
		    Role == EFontRole::Display ? TEXT("mochiy.ttf") :
		    Role == EFontRole::Numeric ? TEXT("toy-ui-numerals.ttf") :
		    Role == EFontRole::Hand ? TEXT("yusei.ttf") : TEXT("zen-medium.ttf");
		auto& Composite = Result->GetMutableInternalCompositeFont();
		Composite.DefaultTypeface.Fonts.Emplace(
		    TEXT("Regular"),
		    FPaths::ProjectContentDir() / TEXT("UI/PaperClub/Fonts") / File,
		    EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		// Numeric faces contain Latin only; keep Japanese labels and missing headline glyphs readable.
		Composite.FallbackTypeface.Typeface.Fonts.Emplace(TEXT("Regular"),
		    FPaths::ProjectContentDir() / TEXT("UI/PaperClub/Fonts/zen.ttf"),
		    EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		return Result;
	}
	inline UFont* Font(UObject* Outer, bool Title = false)
	{
		return Font(Outer, Title ? EFontRole::Display : EFontRole::Body);
	}
	inline UTexture2D* Art(const TCHAR* Name)
	{
		return LoadObject<UTexture2D>(nullptr,
		    *FString::Printf(TEXT("/Game/UI/Designer/Imported/T_UI_%s.T_UI_%s"), Name, Name));
	}
	// Small interactive controls need solid corners; the paper texture is for large cards.
	inline FButtonStyle CompactButtonStyle()
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Paper, 9.f, Outline, 2.5f));
		Style.SetHovered(FSlateRoundedBoxBrush(Gold, 9.f, Outline, 3.f));
		Style.SetPressed(FSlateRoundedBoxBrush(Teal, 9.f, Outline, 3.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(Cream, 9.f, Muted, 2.f));
		Style.SetNormalPadding(FMargin(6, 2));
		Style.SetPressedPadding(FMargin(6, 4, 6, 0));
		return Style;
	}
} // namespace TBPaper
