// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TBTitleMenu.generated.h"

class UTBSession;
class UButton;
class UTextBlock;
class UTexture2D;
class UScrollBox;

/** Title menu; observes the existing session subsystem. */
UCLASS()
class TOONSTORY_API UTBTitleMenu : public UUserWidget
{
	GENERATED_BODY()
public:
	enum class EState : uint8
	{
		Story, // 寸劇の終わり際にメニューを重ねる
		Menu,  // メニューをフェードで出す。後ろでは部屋の様子がループする
	};
	EState GetState() const
	{
		return State;
	}
	// 物語に入る。寸劇が無ければ入らない。
	void EnterStory();
	// 物語を飛ばしてメニューへ。
	void SkipStory();
	// 寸劇を流している間、どのキーでも飛ばせる。テストからもキー入力を流す。
	FReply SendKeyDownForTest(const FKeyEvent& KeyEvent)
	{
		return NativeOnPreviewKeyDown(FGeometry(), KeyEvent);
	}
	float GetMenuOpacity() const;
	bool AreRoomCardsShown() const
	{
		return bRoomCardsShown;
	}
	// 寸劇を飛ばした直後に挟む黒の濃さ。
	float GetBlackoutOpacity() const
	{
		return BlackoutFade;
	}
	// 寸劇の間、クリックを受け止める板が出ているか。
	bool IsCatchingClicks() const
	{
		return Blackout && Blackout->GetVisibility() == ESlateVisibility::Visible;
	}
	// 放置と、止めたループの再開を、時間を進めて確かめる。
	void AdvanceIdleForTest(float Seconds)
	{
		UpdateIdle(Seconds);
	}

protected:
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry, const FAnalogInputEvent& InputEvent) override;
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	// 以下は WBP_TitleMenu に置けば、置いたものを使う。名前を合わせる。無ければ C++ が従来どおり作る。
	// ScaleBox（TitleScale）の中の 1600×900 の Canvas（TitleCanvas）に、ボタンなどを置く。
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<class UScaleBox> TitleScale;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<class UCanvasPanel> TitleCanvas;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<UButton> HostButton;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<UButton> FindButton;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<UButton> HelpButton;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<UButton> OptionsButton;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<UButton> QuitButton;
	// ロゴの画像。ブラシに画像を設定すると、仮の TOY / or NOT? の文字は出さない。画像が無ければ隠す。
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<class UImage> LogoImage;
	// カーソルを乗せたボタンの左に出す集中線。普段は隠れている。
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<class UImage> HostBurst;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<class UImage> FindBurst;
	UPROPERTY(Transient, meta = (BindWidgetOptional)) TObjectPtr<class UImage> HelpBurst;

private:
	void BuildPaperLayout();
	void EnterMenu(bool bBeforeStoryEnd = false);
	bool bOpeningOverlap = false;
	// 寸劇を流さずメニューへ入るとき、寸劇を最後のコマの状態にする。
	void SettleStory();
	void StartLoop();
	// 触ったら、放置の数え直し。
	void NoteInput();
	void UpdateIdle(float DeltaTime);
	bool IsBusyInMenu() const;
	void SetRoomCardsShown(bool bShown);
	UFUNCTION()
	void StoryFinished();
	EState State = EState::Menu;
	float MenuFade = 1.f;
	float BlackoutFade = 0.f;
	// 最後の入力からの秒数。
	float IdleSeconds = 0.f;
	FVector2D LastCursor = FVector2D::ZeroVector;
	bool bRoomCardsShown = false;
	uint32 LanguageRevision = 0;
	TWeakObjectPtr<class ALevelSequenceActor> StoryActor;
	UPROPERTY(Transient) TObjectPtr<UWidget> Blackout;
	UPROPERTY(Transient) TObjectPtr<UWidget> MenuLayer;
	UPROPERTY(Transient) TObjectPtr<UWidget> TitlePrintSheet;
	UPROPERTY(Transient) TObjectPtr<UWidget> StorySkipHint;
	UPROPERTY(Transient) TObjectPtr<UWidget> FinderOverlay;
	UPROPERTY(Transient) TObjectPtr<UTBSession> Session;
	UPROPERTY(Transient) TObjectPtr<UButton> JoinButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RefreshRoomsButton;
	UPROPERTY(Transient) TObjectPtr<UButton> BackFromFinderButton;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> RoomList;
	int32 SelectedRoom = INDEX_NONE;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RoomHintText;
	UFUNCTION() void Refresh();
	UFUNCTION() void Hover();
	UFUNCTION() void Host();
	UFUNCTION() void Find();
	UFUNCTION() void CloseFinder();
	UFUNCTION() void Join();
	UFUNCTION()
	void Quit();
	UFUNCTION() void AskQuit();
	UFUNCTION() void CancelQuit();
	UPROPERTY(Transient) TObjectPtr<UWidget> QuitOverlay;
	UPROPERTY(Transient) TObjectPtr<UButton> CancelQuitButton;
	UFUNCTION()
	void Help();
	UFUNCTION() void CloseHelp();
	UFUNCTION() void ShowToyHelp();
	UFUNCTION() void ShowHumanHelp();
	// 陣営を切り替えたら、その漫画と遊び方を一緒に更新する。
	void RefreshHelpText();
	bool bHumanHelp = false;
	UPROPERTY(Transient) TObjectPtr<UButton> HelpToyTab;
	UPROPERTY(Transient) TObjectPtr<UButton> HelpHumanTab;
	UPROPERTY(Transient) TObjectPtr<class UImage> HelpComic;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HelpGoal;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> HelpCaptions;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> HelpSpeech;
	UPROPERTY(Transient) TObjectPtr<UWidget> TitleContent;
	UPROPERTY(Transient) TObjectPtr<UWidget> HelpOverlay;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseHelpButton;
	// オプション画面（UTBOptionsMenu）を開く。ロビー・試合中のメニューと同じ画面。
	UFUNCTION() void OpenOptions();
	TWeakObjectPtr<class UTBOptionsMenu> Options;
	UPROPERTY(Transient)
	TObjectPtr<class UFont> PaperFont;
	UPROPERTY(Transient)
	TObjectPtr<class UFont> LogoFont;
	UFUNCTION() void RoomChecked(bool bChecked);
};
