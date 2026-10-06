// Source/GeminiBridge/Public/GeminiSettings.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GeminiSettings.generated.h"

/**
 * Gemini APIの設定をProject Settingsで管理するための設定クラス
 */
UCLASS(Config = Engine, defaultconfig, meta = (DisplayName = "Gemini Bridge"))
class GEMINIBRIDGE_API UGeminiSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGeminiSettings();

	/** Gemini APIキー */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API Settings", meta = (PasswordField = true))
	FString ApiKey;

	/** 使用するデフォルトのGeminiモデル名（例: gemini-1.5-flash, gemini-1.5-pro） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API Settings")
	FString DefaultModel;

	/** 設定インスタンスを簡単に取得するためのヘルパー関数 */
	UFUNCTION(BlueprintPure, Category = "Gemini Bridge")
	static const UGeminiSettings* GetGeminiSettings();

	// UDeveloperSettings Overrides
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};