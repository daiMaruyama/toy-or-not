// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "TBGameViewportClient.generated.h"

/** ゲーム表示中の画面診断ポリシーと撮影用UI切替。最後のviewport終了時に診断表示を復元する。 */
UCLASS()
class TOONSTORY_API UTBGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()
public:
	virtual void Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
	virtual void DetachViewportClient() override;
	virtual void BeginDestroy() override;
	virtual void Draw(FViewport* InViewport, FCanvas* SceneCanvas) override;
	virtual EMouseCursor::Type GetCursor(FViewport* InViewport, int32 X, int32 Y) override;
	void ToggleUI();
	bool IsUIHidden() const { return bUIHidden; }
	static bool IsUIHidden(const UWorld* World);
private:
	void ReleaseShortcut();
	void ReleaseScreenMessagePolicy();
	bool bOwnsScreenMessagePolicy = false;
	bool bUIHidden = false;
	TSharedPtr<class IInputProcessor> UIShortcut;
	TWeakPtr<class SWidget> FocusBeforeHiding;
};
