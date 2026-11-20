// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "STBLoadingScreen.h"
#include "TBLoadingScreenSettings.h"
#include "UI/TBPaperStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "FileMediaSource.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

void STBLoadingScreen::Construct(const FArguments& InArgs)
{
	const auto* Settings = GetDefault<UTBLoadingScreenSettings>();
	Backdrop = InArgs._Backdrop;
	Poster.Reset(InArgs._Poster);
	PosterBrush.SetResourceObject(Poster.Get());
	if (Backdrop == ETBLoadingBackdrop::ViewportMovie)
	{
		Player.Reset(NewObject<UMediaPlayer>());
		Film.Reset(NewObject<UMediaTexture>());
		Film->NewStyleOutput = true;
		Film->SetMediaPlayer(Player.Get());
		Film->UpdateResource();
		FilmBrush.SetResourceObject(Film.Get());
		auto* Source = NewObject<UFileMediaSource>(Player.Get());
		Source->SetFilePath(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Movies") /
		                                                      (FString(MovieName()) + TEXT(".mp4"))));
		FMediaPlayerOptions Options;
		Options.PlayOnOpen = EMediaPlayerOptionBooleanOverride::Disabled;
		Options.Loop = EMediaPlayerOptionBooleanOverride::Enabled;
		Player->OpenSourceWithOptions(Source, Options);
		SetMovieVisible(true);
	}
	Font.Reset(TBPaper::Font(GetTransientPackage()));
	Restart();
	SetVisibility(EVisibility::HitTestInvisible);
	if (Backdrop != ETBLoadingBackdrop::Illustrations) return;
	// ロード中に消されないよう、絵とフォントはここで読み込んで持っておく。
	for (const auto& Path : Settings->Toys)
	{
		if (UTexture2D* Texture = Path.LoadSynchronous())
		{
			Textures.Emplace(Texture);
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
			Brushes.Add(Brush);
		}
	}
	Random.Initialize(int32(FPlatformTime::Cycles()));
	// 始まった瞬間から、おもちゃがもう顔を出している。途中で切れても不自然にならないように。
	Started = FPlatformTime::Seconds() - Settings->PeekSeconds * .4;
}

STBLoadingScreen::~STBLoadingScreen()
{
	if (Player.IsValid()) Player->Close();
}

void STBLoadingScreen::SetMovieVisible(bool bVisible, double MovieStartedAt)
{
	bStartMovieWhenReady = bVisible;
	ResumeMovieFrom = MovieStartedAt;
	if (!bVisible && Player.IsValid() && Player->IsPlaying()) Player->Pause();
}

void STBLoadingScreen::Tick(const FGeometry&, double, float)
{
	if (!bStartMovieWhenReady || !Player.IsValid() || !Player->IsReady()) return;
	if (ResumeMovieFrom > 0.)
	{
		// Match the native stream's current position when map travel hands off to the game viewport.
		const double Duration = Player->GetDuration().GetTotalSeconds();
		if (Duration > 0.)
			Player->Seek(FTimespan::FromSeconds(FMath::Fmod(FPlatformTime::Seconds() - ResumeMovieFrom, Duration)));
		ResumeMovieFrom = 0.;
	}
	bStartMovieWhenReady = !Player->Play();
}

void STBLoadingScreen::SetStatus(const FText& Text)
{
	Status = Text;
	if (Backdrop != ETBLoadingBackdrop::Illustrations && Font.IsValid())
		StatusSize = FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Status, FSlateFontInfo(Font.Get(), 18)));
}

bool STBLoadingScreen::IsToyHidden() const
{
	const auto* Settings = GetDefault<UTBLoadingScreenSettings>();
	const double Period = Settings->PeekSeconds + Settings->RunSeconds + Settings->HiddenSeconds;
	const double T = FMath::Fmod(FPlatformTime::Seconds() - Started, Period);
	// 隠れたあとの間が0なら、隠れきる瞬間がないので待たない。
	return Brushes.IsEmpty() || Settings->HiddenSeconds <= 0.f || T >= Settings->PeekSeconds + Settings->RunSeconds;
}

void STBLoadingScreen::SetExit(float Value)
{
	const float Old = Exit;
	Exit = FMath::Clamp(Value, 0.f, 1.f);
	if (Backdrop != ETBLoadingBackdrop::Illustrations) return;
	const auto* Settings = GetDefault<UTBLoadingScreenSettings>();
	if (Exit <= 0.f) ToyEndsAt = -1.0;
	else if (Old <= 0.f)
		ToyEndsAt = ToyStopElapsed(FPlatformTime::Seconds() - Started, Settings->PeekSeconds, Settings->RunSeconds, Settings->HiddenSeconds);
}

double STBLoadingScreen::ToyStopElapsed(double Elapsed, double Peek, double Run, double Hidden)
{
	const double Period = Peek + Run + Hidden;
	if (Period <= 0.0) return Elapsed;
	const double CycleStart = Elapsed - FMath::Fmod(Elapsed, Period);
	// 隠れる間が0でも、次の周には入らない。
	return FMath::Min(CycleStart + FMath::Max(Elapsed - CycleStart, Peek + Run), CycleStart + Period - KINDA_SMALL_NUMBER);
}

int32 STBLoadingScreen::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
                                FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const auto* Settings = GetDefault<UTBLoadingScreenSettings>();
	const FVector2f Size = FVector2f(Geometry.GetLocalSize());
	const float W = Size.X, H = Size.Y;
	if (W <= 0 || H <= 0 || Exit >= 1.f) return Layer;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	// 入るときは黒から明け、出るときはいったん真っ黒に落としてから、黒だけを薄くしてゲーム画面を見せる。
	// ロード画面とゲーム画面が半透明で重ならないようにする。
	const float In = Settings->FadeInSeconds > 0.f
	                     ? FMath::Clamp(float(FPlatformTime::Seconds() - ShownAt) / Settings->FadeInSeconds, 0.f, 1.f)
	                     : 1.f;
	const float Black = FMath::Max(1.f - In, Exit < .5f ? Exit * 2.f : (1.f - Exit) * 2.f);
	auto Curtain = [&](int32 At)
	{
		if (Black > 0.f)
			FSlateDrawElement::MakeBox(Out, At, Geometry.ToPaintGeometry(), White, ESlateDrawEffect::None,
			                           FLinearColor(0, 0, 0, Black));
	};
	if (Exit >= .5f)
	{
		Curtain(Layer);
		return Layer;
	}
	static const FSlateRoundedBoxBrush Rounded(FLinearColor::White, 8.f);
	// Let Slate derive the radius from each element's height, including tiny dots and shadows.
	static const FSlateRoundedBoxBrush Pill(FLinearColor::White);
	const float S = FMath::Min(W / 1280.f, H / 720.f);
	if (Backdrop != ETBLoadingBackdrop::Illustrations)
	{
		// During a blocking map load the native movie viewport paints underneath this widget.
		// The game viewport displays the matching still until the first frame has decoded.
		if (Backdrop == ETBLoadingBackdrop::ViewportMovie || bShowNativePoster.Load())
		{
			FSlateDrawElement::MakeBox(Out, Layer++, Geometry.ToPaintGeometry(), White, ESlateDrawEffect::None,
			                           FLinearColor::FromSRGBColor(FColor(35, 30, 24)));
			const FSlateBrush* Frame = Poster.IsValid() ? &PosterBrush : nullptr;
			if (Film.IsValid() && Film->GetWidth() > 2 && Film->GetHeight() > 2) Frame = &FilmBrush;
			if (Frame)
			{
				const FVector2f FilmSize(1280 * S, 720 * S);
				FSlateDrawElement::MakeBox(Out, Layer++, Geometry.ToPaintGeometry(FilmSize, FSlateLayoutTransform((Size - FilmSize) * .5f)),
				                           Frame, ESlateDrawEffect::None, FLinearColor::White);
			}
		}
		if (!Status.IsEmpty() && Font.IsValid())
		{
			const FVector2f GradientSize(W, 86 * S);
			FSlateDrawElement::MakeGradient(Out, Layer++, Geometry.ToPaintGeometry(GradientSize,
			    FSlateLayoutTransform(FVector2f(0, H - GradientSize.Y))),
			    {FSlateGradientStop(FVector2D(0, 0), FLinearColor::Transparent),
			     FSlateGradientStop(FVector2D(0, GradientSize.Y), FLinearColor(.023f, .012f, .005f, .66f))}, Orient_Horizontal);
			const FVector2f Label(W - W * .032f - StatusSize.X * S, H - (21.f + StatusSize.Y) * S);
			FSlateDrawElement::MakeText(Out, Layer++, Geometry.ToPaintGeometry(StatusSize, FSlateLayoutTransform(S, Label)),
			                            Status, FSlateFontInfo(Font.Get(), 18), ESlateDrawEffect::None,
			                            FLinearColor::FromSRGBColor(FColor(255, 246, 223)));
		}
		Curtain(Layer);
		return Layer;
	}
	auto Box = [&](const FSlateBrush* Brush, float X, float Y, float BW, float BH, FLinearColor Color)
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(BW, BH), FSlateLayoutTransform(FVector2f(X, Y))),
		                           Brush, ESlateDrawEffect::None, Color);
	};
	auto Shade = [](FLinearColor Color, float Scale)
	{
		return FLinearColor(Color.R * Scale, Color.G * Scale, Color.B * Scale, Color.A);
	};

	// Quiet paper backdrop. Keep the animation as one small scene, without a title or logo.
	const FLinearColor Wall = FMath::Lerp(TBPaper::Paper, Settings->WallColor, .035f);
	const FLinearColor Floor = FMath::Lerp(TBPaper::Cream, Settings->FloorColor, .08f);
	const float FloorY = H * .5f + 55 * S;
	FSlateDrawElement::MakeGradient(
	    Out, Layer, Geometry.ToPaintGeometry(),
	    {FSlateGradientStop(FVector2D(0, 0), Wall),
	     FSlateGradientStop(FVector2D(0, H), TBPaper::Cream)},
	    Orient_Horizontal);
	++Layer;
	// A pale rug and a fine base line give the cutout toys somewhere to stand.
	Box(&Pill, W * .5f - 456 * S, FloorY - 10 * S, 912 * S, 57 * S, Floor.CopyWithNewOpacity(.6f));
	Box(&Pill, W * .5f - 417 * S, FloorY + 3 * S, 834 * S, 2 * S,
	    TBPaper::Muted.CopyWithNewOpacity(.18f));
	++Layer;

	// おもちゃ。積み木の陰から顔を出し、様子を見てから、箱の陰へ跳ねながら走る。
	const float BlockRight = W * .5f - 250 * S, BoxLeft = W * .5f + 250 * S;
	const float Peek = Settings->PeekSeconds, Run = Settings->RunSeconds;
	const double Period = Peek + Run + Settings->HiddenSeconds;
	double Elapsed = FPlatformTime::Seconds() - Started;
	// 暗転が始まったら、始まった周の中で止める。
	if (ToyEndsAt >= 0.0) Elapsed = FMath::Min(Elapsed, ToyEndsAt);
	const int32 Current = int32(Elapsed / Period);
	const float T = float(Elapsed - Current * Period);
	if (Current != Loop && Brushes.Num() > 0)
	{
		// 続けて同じおもちゃが出ないように選ぶ。
		const int32 Next = Brushes.Num() > 1 ? (Toy + 1 + Random.RandRange(0, Brushes.Num() - 2)) % Brushes.Num() : 0;
		Toy = Loop < 0 ? Random.RandRange(0, Brushes.Num() - 1) : Next;
		Loop = Current;
	}
	if (Brushes.IsValidIndex(Toy) && T < Peek + Run)
	{
		const FSlateBrush& Brush = Brushes[Toy];
		const float ToyH = 720 * S * Settings->ToyHeight * .82f;
		const float ToyW = ToyH * float(Brush.ImageSize.X / FMath::Max(Brush.ImageSize.Y, 1.0));
		const float PeekX = BlockRight - ToyW * .45f;
		float X, Lift = 0, Angle = 0;
		bool bLookBack = false;
		if (T < Peek)
		{
			// そっと出てきて、きょろきょろと後ろを振り返る。
			const float Out01 = FMath::InterpEaseOut(0.f, 1.f, FMath::Min(T / .35f, 1.f), 2.f);
			X = FMath::Lerp(BlockRight - ToyW, PeekX, Out01);
			Lift = FMath::Abs(FMath::Sin(T * 7.f)) * 3 * S;
			bLookBack = T > Peek * .45f && T < Peek * .75f;
		}
		else
		{
			const float U = (T - Peek) / Run;
			X = FMath::Lerp(PeekX, BoxLeft + ToyW * .15f, FMath::InterpEaseInOut(0.f, 1.f, U, 1.6f));
			const float Hop = FMath::Sin(U * Settings->Hops * PI);
			Lift = FMath::Abs(Hop) * 20 * S;
			Angle = FMath::DegreesToRadians(-5.f + 6.f * FMath::Sin(U * Settings->Hops * 2.f * PI));
		}
		// 影は床に残し、体だけ跳ねる。
		Box(&Pill, X + ToyW * .2f, FloorY - 4 * S, ToyW * .6f, 10 * S,
		    TBPaper::Muted.CopyWithNewOpacity(.19f));
		FSlateBrush Drawn = Brush;
		Drawn.Mirroring = bLookBack ? ESlateBrushMirrorType::Horizontal : ESlateBrushMirrorType::NoMirror;
		FSlateDrawElement::MakeBox(
		    Out, Layer,
		    Geometry.ToPaintGeometry(FVector2f(ToyW, ToyH), FSlateLayoutTransform(FVector2f(X, FloorY - ToyH - Lift)),
		                             FSlateRenderTransform(FQuat2f(Angle)), FVector2f(.5f, 1.f)),
		    &Drawn, ESlateDrawEffect::None, Settings->ToyTint);
	}
	++Layer;

	// 手前の積み木と箱。おもちゃはこの陰に隠れる。
	const float Cube = 82 * S;
	const TArray<FLinearColor>& Colors = Settings->BlockColors;
	auto Block = [&](int32 Index, float X, float Y)
	{
		const FLinearColor Original = Colors.Num() ? Colors[Index % Colors.Num()] : TBPaper::Teal;
		const FLinearColor Color = FMath::Lerp(Original, TBPaper::Cream, .42f);
		Box(&Rounded, X + 3 * S, Y + 5 * S, Cube, Cube, TBPaper::Shadow.CopyWithNewOpacity(.12f));
		Box(&Rounded, X, Y, Cube, Cube, TBPaper::Muted);
		Box(&Rounded, X + 2 * S, Y + 2 * S, Cube - 4 * S, Cube - 4 * S, Color);
		Box(&Rounded, X + Cube * .13f, Y + Cube * .13f, Cube * .74f, Cube * .74f, TBPaper::Cream);
		Box(&Rounded, X + Cube * .17f, Y + Cube * .17f, Cube * .66f, Cube * .66f, Color);
		// Simple inset shapes instead of letters: the decoration needs no translation.
		if (Index == 1)
		{
			Box(&Pill, X + Cube * .34f, Y + Cube * .34f, Cube * .32f, Cube * .32f, TBPaper::Cream);
			Box(&Pill, X + Cube * .41f, Y + Cube * .41f, Cube * .18f, Cube * .18f, Color);
		}
		else
		{
			Box(&Rounded, X + Cube * .29f, Y + Cube * .45f, Cube * .42f, Cube * .1f, TBPaper::Cream);
			Box(&Rounded, X + Cube * .45f, Y + Cube * .29f, Cube * .1f, Cube * .42f, TBPaper::Cream);
		}
	};
	Block(0, BlockRight - Cube * 2.f, FloorY - Cube);
	Block(1, BlockRight - Cube, FloorY - Cube);
	Block(2, BlockRight - Cube * 1.55f, FloorY - Cube * 2.f);
	const float BoxW = 182 * S;
	const float BoxH = FMath::Max(196 * S, 720 * S * Settings->ToyHeight * .82f + 8 * S);
	const FLinearColor Cardboard = FMath::Lerp(Settings->BoxColor, TBPaper::Cream, .56f);
	Box(&Pill, BoxLeft - 5 * S, FloorY - 3 * S, BoxW + 20 * S, 14 * S,
	    TBPaper::Muted.CopyWithNewOpacity(.2f));
	Box(&Rounded, BoxLeft, FloorY - BoxH, BoxW, BoxH, TBPaper::Muted);
	Box(&Rounded, BoxLeft + 2 * S, FloorY - BoxH + 2 * S, BoxW - 4 * S, BoxH - 4 * S, Cardboard);
	++Layer;
	// Folded cardboard rim, paper tape and a small pull slot, with no printed label.
	Box(&Rounded, BoxLeft - 5 * S, FloorY - BoxH - 4 * S, BoxW + 10 * S, 21 * S, TBPaper::Muted);
	Box(&Rounded, BoxLeft - 3 * S, FloorY - BoxH - 2 * S, BoxW + 6 * S, 17 * S, Shade(Cardboard, 1.1f));
	Box(White, BoxLeft + BoxW * .44f, FloorY - BoxH, BoxW * .12f, BoxH - 2 * S, TBPaper::Gold);
	++Layer;
	Box(&Pill, BoxLeft + BoxW * .35f, FloorY - BoxH * .72f, BoxW * .3f, 9 * S, TBPaper::Muted);
	Box(&Pill, BoxLeft + BoxW * .35f + 2 * S, FloorY - BoxH * .72f + 2 * S,
	    BoxW * .3f - 4 * S, 3 * S, Shade(Cardboard, .65f));
	++Layer;

	if (!Status.IsEmpty() && Font.IsValid())
	{
		const float StatusX = W * .5f - 250 * S, StatusY = FloorY + 88 * S;
		Box(&Pill, StatusX, StatusY, 500 * S, 54 * S, TBPaper::Paper.CopyWithNewOpacity(.85f));
		// Indeterminate dots, not a fabricated loading percentage.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Pulse = .5f + .5f * FMath::Sin(float(FPlatformTime::Seconds()) * 3.f - Index * .7f);
			Box(&Pill, StatusX + (426 + Index * 17) * S, StatusY + 23 * S, 7 * S, 7 * S,
			    FMath::Lerp(TBPaper::Cream, TBPaper::Teal, Pulse));
		}
		++Layer;
		const FSlateFontInfo Info(Font.Get(), FMath::Max(12.f, 22 * S));
		FSlateDrawElement::MakeText(Out, Layer,
		                            Geometry.ToPaintGeometry(FVector2f(388 * S, 36 * S),
		                                FSlateLayoutTransform(FVector2f(StatusX + 24 * S, StatusY + 10 * S))),
		                            Status, Info, ESlateDrawEffect::None, TBPaper::Ink);
	}
	Curtain(++Layer);
	return Layer;
}
