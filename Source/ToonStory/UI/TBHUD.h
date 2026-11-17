// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Data/TBTypes.h"
#include "TBHUD.generated.h"

/** 同期された状態を読み取り、操作説明と進捗を画面に表示する。 */
UCLASS()
class TOONSTORY_API ATBHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
	bool IsLobbySettingsOpen() const { return bLobbySettingsOpen; }
	bool IsStagePickerOpen() const { return bStagePickerOpen; }
	// 設定・参加者一覧・オプションを開いている間は、チャットを出さない。
	bool IsChatBlocked() const;
	virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;

private:
 bool bPlayersOpen = false;
 TWeakObjectPtr<class APlayerState> KickSelection;
 void DrawPlayers();
 bool HandleSafetyClick(FName Name);
	// ロビーと試合中のメニューから開いたオプション画面。試合の切り替わりで閉じる。
	TWeakObjectPtr<class UTBOptionsMenu> Options;
 FName LastClickedButton;
	double LastButtonClickTime = -1;
	bool bLobbySettingsOpen = false;
	bool bStagePickerOpen = false;
	FTBSettings StageDraft;
	FIntPoint LastDraftNPCCounts = FIntPoint(0, 1);
	int32 DraftStage = INDEX_NONE;
	int32 DraftPlayerCount = 0;
	FName StageDraftNotice;
	void BeginStageDraft();
	void NormalizeStageDraft();
	bool HandleLobbyPanelClick(FName Name);
	UPROPERTY(Transient)
	TObjectPtr<class UFont> PaperFont;
	UPROPERTY(Transient)
	TObjectPtr<class UFont> HeadingFont;
	UPROPERTY(Transient)
	TObjectPtr<class UFont> NumericFont;
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> PaperTexture;
	// Designer artwork is loaded once and kept alive across garbage collection.
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<class UTexture2D>> ArtTextures;
	void DrawArt(FName Name, float X, float Y, float W, float H, FLinearColor Tint = FLinearColor::White);
	void DrawButton(FName Id, const FString& Label, float X, float Y, float W, float H,
	                bool Enabled = true, bool Selected = false, bool Destructive = false);
	void DrawProgress(float Value, float X, float Y, float W, float H, FLinearColor Color);
	void DrawFittedText(const FString& Text, FLinearColor Color, float X, float Y, float Width, float Scale,
	                    bool Heading = false, bool Center = false);
	void PaperPanel(FLinearColor Tint, float X, float Y, float W, float H);
	void FramePanel(float X, float Y, float W, float H);
	void PaperSign(FLinearColor Fill, float X, float Y, float W, float H);
	void DrawSmoothPolygon(const TArray<FVector2D>& Points, FLinearColor Color);
	void RoundedPanel(FLinearColor Fill, float X, float Y, float W, float H, float Radius = 24.f,
	                  float Border = 4.f, FLinearColor Outline = FLinearColor::Transparent);
	void DrawKeycap(const FString& Key, float X, float Y, float W, float H);
	void DrawText(const FString& Text, FLinearColor Color, float X, float Y, UFont* Font = nullptr, float Scale = 1.f,
	              bool ScalePosition = false);
	void DrawResults(const class ATBGameState& MatchState);
	void DrawResultCover(const class ATBGameState& MatchState);
	void DrawMomBubble(const class ATBGameState& MatchState, float CenterX, float CenterY, float Width, float Height, float Alpha);
	void DrawMatch(const class ATBGameState& MatchState, class ATBController& Controller);
	void DrawLobby();
	void DrawDebugHUD();
	// 凝視の演出。おもちゃには迫る紙の影と「ドキッ」、人間には暗くなる端と集中線。本人の画面にだけ出す。
	void DrawGazeEffects(class ATBController& Controller);
	void DrawEdgeShade(FLinearColor Color, float Thickness, float Alpha);
	float GazeShadow = 0;
	// 前の描画で凝視されているか止まっていたか。入った瞬間だけ「ドキッ」を出す。
	bool bWasCaught = false;
	double DokiStarted = -1;
};
