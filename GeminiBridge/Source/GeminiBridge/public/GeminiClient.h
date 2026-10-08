// Source/GeminiBridge/Public/GeminiClient.h

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GeminiClient.generated.h"

DECLARE_DYNAMIC_DELEGATE_TwoParams(FGeminiResponseDelegate, const FString&, ResponseText, bool, bWasSuccessful);

UCLASS()
class GEMINIBRIDGE_API UGeminiClient : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Gemini API にテキストプロンプトを送信する（自動リトライ・タイムアウト機能付き）
	 * @param Prompt 送信するプロンプトテキスト
	 * @param OnCompleted レスポンス受信時に呼び出されるコールバック
	 * @param OverrideModel 指定する場合はモデル名を上書き
	 * @param MaxRetries 503/429エラー時の最大リトライ回数（デフォルト: 3回）
	 * @param TimeoutInSeconds HTTPリクエストのタイムアウト秒数（デフォルト: 30秒）
	 */
	UFUNCTION(BlueprintCallable, Category = "Gemini Bridge", meta = (AutoCreateRefTerm = "OnCompleted"))
	static void GenerateContent(
		const FString& Prompt,
		FGeminiResponseDelegate OnCompleted,
		FString OverrideModel = TEXT(""),
		int32 MaxRetries = 3,
		float TimeoutInSeconds = 30.0f
	);

private:
	/** リトライ処理を含む内部実行用関数 */
	static void ExecuteRequestWithRetry(
		const FString& Url,
		const FString& RequestBody,
		FGeminiResponseDelegate OnCompleted,
		int32 RemainingRetries,
		float CurrentBackoffDelay,
		float TimeoutInSeconds
	);

	/** HTTP レスポンス受信時のハンドラ */
	static void OnResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully,
		FGeminiResponseDelegate OnCompleted,
		FString Url,
		FString RequestBody,
		int32 RemainingRetries,
		float CurrentBackoffDelay,
		float TimeoutInSeconds
	);
};