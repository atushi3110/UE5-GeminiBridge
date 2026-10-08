// Source/GeminiBridge/Private/GeminiClient.cpp

#include "GeminiClient.h"
#include "GeminiSettings.h"
#include "HttpModule.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

void UGeminiClient::GenerateContent(
	const FString& Prompt,
	FGeminiResponseDelegate OnCompleted,
	FString OverrideModel,
	int32 MaxRetries,
	float TimeoutInSeconds)
{
	const UGeminiSettings* Settings = UGeminiSettings::GetGeminiSettings();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("[GeminiClient] GeminiSettings could not be loaded."));
		OnCompleted.ExecuteIfBound(TEXT("Error: Settings fail"), false);
		return;
	}

	FString ApiKey = Settings->ApiKey;
	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[GeminiClient] API Key is empty. Please set it in Project Settings."));
		OnCompleted.ExecuteIfBound(TEXT("Error: API Key is missing"), false);
		return;
	}

	FString Model = OverrideModel.IsEmpty() ? Settings->DefaultModel : OverrideModel;
	if (Model.IsEmpty())
	{
		Model = TEXT("gemini-3.8-flash");
	}

	if (Model.StartsWith(TEXT("models/")))
	{
		Model.RightChopInline(7);
	}

	// 最新エンドポイント URL の構築
	FString Url = FString::Printf(TEXT("https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s"), *Model, *ApiKey);

	// JSONリクエスト構造
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> PartObject = MakeShared<FJsonObject>();
	PartObject->SetStringField(TEXT("text"), Prompt);

	TArray<TSharedPtr<FJsonValue>> PartsArray;
	PartsArray.Add(MakeShared<FJsonValueObject>(PartObject));

	TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
	ContentObject->SetArrayField(TEXT("parts"), PartsArray);

	TArray<TSharedPtr<FJsonValue>> ContentsArray;
	ContentsArray.Add(MakeShared<FJsonValueObject>(ContentObject));

	RootObject->SetArrayField(TEXT("contents"), ContentsArray);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	// リクエスト実行（タイムアウト・リトライ付き）
	ExecuteRequestWithRetry(Url, RequestBody, OnCompleted, MaxRetries, 1.0f, TimeoutInSeconds);
}

void UGeminiClient::ExecuteRequestWithRetry(
	const FString& Url,
	const FString& RequestBody,
	FGeminiResponseDelegate OnCompleted,
	int32 RemainingRetries,
	float CurrentBackoffDelay,
	float TimeoutInSeconds)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(RequestBody);

	// ★ タイムアウト時間の設定 (秒)
	HttpRequest->SetTimeout(TimeoutInSeconds);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[OnCompleted, Url, RequestBody, RemainingRetries, CurrentBackoffDelay, TimeoutInSeconds](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			UGeminiClient::OnResponseReceived(
				Request,
				Response,
				bConnectedSuccessfully,
				OnCompleted,
				Url,
				RequestBody,
				RemainingRetries,
				CurrentBackoffDelay,
				TimeoutInSeconds
			);
		}
	);

	HttpRequest->ProcessRequest();
}

void UGeminiClient::OnResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bConnectedSuccessfully,
	FGeminiResponseDelegate OnCompleted,
	FString Url,
	FString RequestBody,
	int32 RemainingRetries,
	float CurrentBackoffDelay,
	float TimeoutInSeconds)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[GeminiClient] Failed to connect to Gemini API or request timed out."));
		OnCompleted.ExecuteIfBound(TEXT("Error: Connection failed or timed out"), false);
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	FString ResponseContent = Response->GetContentAsString();

	// 503 (Service Unavailable) または 429 (Too Many Requests) の場合にリトライ処理を行う
	if ((ResponseCode == 503 || ResponseCode == 429) && RemainingRetries > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GeminiClient] Server returned %d. Retrying in %.1f seconds... (%d retries left)"),
			ResponseCode, CurrentBackoffDelay, RemainingRetries);

		// 世界（World）から TimerManager を取得して非同期遅延実行
		UWorld* World = nullptr;
		if (GEngine && GEngine->GetWorldContexts().Num() > 0)
		{
			World = GEngine->GetWorldContexts()[0].World();
		}

		if (World)
		{
			FTimerHandle RetryTimerHandle;
			// 指数バックオフ: 次回のリトライ待機時間を2倍にする
			float NextBackoffDelay = CurrentBackoffDelay * 2.0f;
			int32 NextRemainingRetries = RemainingRetries - 1;

			World->GetTimerManager().SetTimer(
				RetryTimerHandle,
				[Url, RequestBody, OnCompleted, NextRemainingRetries, NextBackoffDelay, TimeoutInSeconds]()
				{
					UGeminiClient::ExecuteRequestWithRetry(Url, RequestBody, OnCompleted, NextRemainingRetries, NextBackoffDelay, TimeoutInSeconds);
				},
				CurrentBackoffDelay,
				false
			);
			return;
		}
	}

	if (ResponseCode != 200)
	{
		UE_LOG(LogTemp, Error, TEXT("[GeminiClient] HTTP Error %d: %s"), ResponseCode, *ResponseContent);
		OnCompleted.ExecuteIfBound(FString::Printf(TEXT("HTTP Error %d"), ResponseCode), false);
		return;
	}

	// JSON レスポンス解析
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* CandidatesArray;
		if (JsonObject->TryGetArrayField(TEXT("candidates"), CandidatesArray) && CandidatesArray->Num() > 0)
		{
			TSharedPtr<FJsonObject> Candidate = (*CandidatesArray)[0]->AsObject();
			if (Candidate.IsValid())
			{
				TSharedPtr<FJsonObject> Content = Candidate->GetObjectField(TEXT("content"));
				if (Content.IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* PartsArray;
					if (Content->TryGetArrayField(TEXT("parts"), PartsArray) && PartsArray->Num() > 0)
					{
						TSharedPtr<FJsonObject> Part = (*PartsArray)[0]->AsObject();
						FString GeneratedText;
						if (Part->TryGetStringField(TEXT("text"), GeneratedText))
						{
							OnCompleted.ExecuteIfBound(GeneratedText, true);
							return;
						}
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[GeminiClient] Failed to parse JSON response."));
	OnCompleted.ExecuteIfBound(TEXT("Error: Failed to parse response JSON"), false);
}