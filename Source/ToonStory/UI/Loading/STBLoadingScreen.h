// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class UFont;
class UTexture2D;

enum class ETBLoadingBackdrop { Illustrations, NativeMovie, ViewportMovie };

/**
 * ロード画面の絵。紙色の小さな遊び場で、おもちゃが積み木の陰から顔を出し、箱の陰へ小走りで隠れる。
 * ロード中はゲームの処理が止まるので、UObject の更新に頼らず、時間だけから毎回描く（ロード用のスレッドからも描かれる）。
 */
class STBLoadingScreen : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(STBLoadingScreen) : _Backdrop(ETBLoadingBackdrop::Illustrations), _Poster(nullptr) {}
		SLATE_ARGUMENT(ETBLoadingBackdrop, Backdrop)
		SLATE_ARGUMENT(UTexture2D*, Poster)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~STBLoadingScreen();
	static const TCHAR* MovieName() { return TEXT("ToyDance"); }
	double GetShownAt() const { return ShownAt; }
	void ShowNativePoster() { bShowNativePoster.Store(true); }
	void SetViewportPoster(UTexture2D* Texture);
	void SetMovieVisible(bool bVisible, double MovieStartedAt = 0.);
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
#if WITH_DEV_AUTOMATION_TESTS
	UMediaPlayer* GetPlaybackForReview() const { return Player.Get(); }
#endif
	// 下に出す状態の文字。ロード中は描画が別スレッドなので、ロードを始める前に決めておく。
	void SetStatus(const FText& Text);
	// 出しはじめ。黒から明けて出てくる。
	void Restart() { ShownAt = FPlatformTime::Seconds(), Exit = 0.f, ToyEndsAt = -1.0; }
	// ロード中に MoviePlayer が描いていた絵から、時間・おもちゃ・文字を引き継ぐ。同じ部品を2か所に付けないため、別に作って続ける。
	void ContinueFrom(const STBLoadingScreen& Other)
	{
		SetStatus(Other.Status);
		ShownAt = Other.ShownAt, Started = Other.Started, Exit = 0.f, ToyEndsAt = -1.0;
		Loop = Other.Loop, Toy = Other.Toy;
	}
	// 消え具合。0で出ている、0.5で真っ黒、1で消えきる（下のゲーム画面が明けて見える）。
	void SetExit(float Value);
	float GetExit() const { return Exit; }
	// おもちゃが箱の陰に隠れきって、次が顔を出す前の間か。ここで消せば、走る途中で切れない。
	bool IsToyHidden() const;
	// 暗転を始めた時刻（Elapsed）のおもちゃの周期を、どこで止めるか。走っている途中なら走り終わり、隠れていればその時刻。次の周には入らない。
	static double ToyStopElapsed(double Elapsed, double Peek, double Run, double Hidden);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	                      FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	                      bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280, 720); }

private:
	ETBLoadingBackdrop Backdrop = ETBLoadingBackdrop::Illustrations;
	TAtomic<bool> bShowNativePoster{false};
	bool bStartMovieWhenReady = false;
	double ResumeMovieFrom = 0.;
	TStrongObjectPtr<UMediaPlayer> Player;
	TStrongObjectPtr<UMediaTexture> Film;
	TStrongObjectPtr<UTexture2D> Poster;
	FSlateBrush FilmBrush, PosterBrush;
	FVector2f StatusSize = FVector2f::ZeroVector;
	TArray<TStrongObjectPtr<UTexture2D>> Textures;
	TArray<FSlateBrush> Brushes;
	TStrongObjectPtr<UFont> Font;
	FText Status;
	float Exit = 0.f;
	double ShownAt = 0;
	double Started = 0;
	// 暗転を始めたあと、おもちゃの周期を止める Elapsed。負なら止めない。
	double ToyEndsAt = -1.0;
	// 1周ごとに替えるおもちゃ。描画のたびに時間から求め直す。
	mutable int32 Loop = -1;
	mutable int32 Toy = 0;
	mutable FRandomStream Random;
};
