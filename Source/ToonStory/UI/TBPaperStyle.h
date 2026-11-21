#pragma once
#include "UI/TBUIText.h"
#include "CoreMinimal.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateImageBrush.h"

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
	inline const FLinearColor ReadyGreen = FLinearColor::FromSRGBColor(FColor(56, 160, 126));
	inline const FLinearColor StartOrange = FLinearColor::FromSRGBColor(FColor(216, 122, 48));
	inline const FLinearColor Lavender = FLinearColor::FromSRGBColor(FColor(211, 204, 229));
	inline const FLinearColor Gold = FLinearColor::FromSRGBColor(FColor(255, 214, 91));
	inline const FLinearColor Danger = FLinearColor::FromSRGBColor(FColor(166, 48, 40));
	inline const FLinearColor Outline = FLinearColor::FromSRGBColor(FColor(59, 27, 9));
	inline const FLinearColor Shadow = FLinearColor(0.025f, 0.012f, 0.006f, .24f);
	enum class EFontRole : uint8 { Body, Heading, Display, Numeric };
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
		    Role == EFontRole::Numeric ? TEXT("toy-ui-numerals.ttf") : TEXT("zen-medium.ttf");
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
	inline FSlateBrush ImageBrush(UTexture2D* Texture)
	{
		FSlateBrush Result;
		Result.SetResourceObject(Texture);
		Result.DrawAs = ESlateBrushDrawType::Image;
		if (Texture) Result.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		return Result;
	}
	inline FSlateBrush Brush(UTexture2D* Texture, FLinearColor Tint = FLinearColor::White)
	{
		FSlateBrush Result;
		Result.SetResourceObject(Texture);
		Result.DrawAs = ESlateBrushDrawType::Box;
		Result.Margin = FMargin(.25f);
		Result.ImageSize = FVector2D(128);
		Result.TintColor = Tint;
		return Result;
	}
	// 紙のボタン。Secondary は少し暗い紙。
	inline FButtonStyle ButtonStyle(UTexture2D* Texture, bool Secondary = false)
	{
		FButtonStyle Style;
		Style.SetNormal(Brush(Texture, Secondary ? Cream : Paper));
		Style.SetHovered(Brush(Texture, Gold));
		Style.SetPressed(Brush(Texture, Lavender));
		Style.SetDisabled(Brush(Texture, FLinearColor(.55f, .49f, .40f, .65f)));
		Style.SetNormalPadding(FMargin(12, 6));
		Style.SetPressedPadding(FMargin(12, 9, 12, 3));
		return Style;
	}
	// Small interactive controls need solid corners; the paper texture is for large cards.
	inline FButtonStyle CompactButtonStyle(bool Secondary = false)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Secondary ? Cream : Paper, 9.f, Outline, 2.5f));
		Style.SetHovered(FSlateRoundedBoxBrush(Gold, 9.f, Outline, 3.f));
		Style.SetPressed(FSlateRoundedBoxBrush(Teal, 9.f, Outline, 3.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(Cream, 9.f, Muted, 2.f));
		Style.SetNormalPadding(FMargin(6, 2));
		Style.SetPressedPadding(FMargin(6, 4, 6, 0));
		return Style;
	}

	// Preserve the supplied frame corners while only stretching the plain center and straight edges.
	inline FSlateBrush SlicedArtwork(UTexture2D* Texture, FVector2D LayoutSize, FMargin UVBorder)
	{
		FSlateBrush Result = Brush(Texture);
		Result.ImageSize = LayoutSize;
		Result.Margin = UVBorder;
		return Result;
	}

	inline FButtonStyle ArtButtonStyle(const FSlateBrush& Face, FLinearColor Tint = FLinearColor::White)
	{
		FButtonStyle Style = CompactButtonStyle();
		if (!Face.GetResourceObject() && Face.GetResourceName().IsNone()) return Style;
		FSlateBrush Normal = Face, Hovered = Face, Pressed = Face, Disabled = Face;
		Normal.TintColor = Tint;
		Hovered.TintColor = FMath::Lerp(Tint, FLinearColor::White, .5f) * FLinearColor(1.f, .92f, .74f, 1.f);
		Pressed.TintColor = Tint * FLinearColor(.79f, .79f, .79f, 1.f);
		Disabled.TintColor = FLinearColor(.57f, .51f, .45f, .7f);
		Style.SetNormal(Normal).SetHovered(Hovered).SetPressed(Pressed).SetDisabled(Disabled);
		return Style;
	}

	// Rasterize the entire printed edge at the button's actual size. Slicing the curve
	// into corners and strips introduces visible steps where the pieces meet.
	inline FButtonStyle SignButtonStyle(FLinearColor Tint)
	{
		static const FSlateVectorImageBrush Face(FPaths::ProjectContentDir() / TEXT("UI/PaperClub/Sign.svg"),
		    FVector2D(144, 42));
		if (Tint == Teal) Tint = FLinearColor::FromSRGBColor(FColor(150, 218, 198));
		else if (Tint == Gold) Tint = FLinearColor::FromSRGBColor(FColor(255, 220, 118));
		else if (Tint == Paper) Tint = FLinearColor::FromSRGBColor(FColor(255, 246, 222));
		FButtonStyle Style = ArtButtonStyle(Face, Tint);
		Style.SetNormalPadding(FMargin(8, 4, 8, 6));
		Style.SetPressedPadding(FMargin(8, 5, 8, 5));
		return Style;
	}
} // namespace TBPaper
