#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include <traceloggingactivity.h>
#include <traceloggingprovider.h>
#include <d3d11.h>
#include <DirectXMath.h>
using namespace DirectX;
#include <wrl.h>

#include <future>

#pragma comment (lib, "d3d11.lib")

/////////////////////////////////////////////////////////////
// Use setdll to force loading our DLL, eg:
// .\setdll.exe /d:D:\XR\Varjo-Patch\PatchCompositor\x64\Debug\PatchCompositor.dll .\VarjoCompositor.exe



#pragma region "Tracelogging"

// {cbf3adcd-42b1-4c38-830b-95980af201f6}
TRACELOGGING_DEFINE_PROVIDER(g_traceProvider,
	"PatchCompositor",
	(0xcbf3adcd, 0x42b1, 0x4c38, 0x93, 0x0b, 0x95, 0x98, 0x0a, 0xf2, 0x01, 0xf6));

#define IsTraceEnabled() TraceLoggingProviderEnabled(g_traceProvider, 0, 0)
#define TraceLocalActivity(activity) TraceLoggingActivity<g_traceProvider> activity;
#define TLArg(var, ...) TraceLoggingValue(var, ##__VA_ARGS__)
#define TLPArg(var, ...) TraceLoggingPointer(var, ##__VA_ARGS__)
#pragma endregion

#pragma region "Detours"
template <class T, typename TMethod>
void DetourMethodAttach(T* instance, unsigned int methodOffset, TMethod hooked, TMethod& original) {
	if (original) {
		// Already hooked.
		return;
	}

	LPVOID* vtable = *((LPVOID**)instance);
	LPVOID target = vtable[methodOffset];

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	original = (TMethod)target;
	DetourAttach((PVOID*)&original, hooked);

	DetourTransactionCommit();
}
#pragma endregion


/////////////////////////////////////////////////////////////
// Shader Constant Buffer definitions from VarjoCompositor
// Obtained via Nsight
//
// 1) Run Nsight Graphics in Frame Debugger mode, configure to start `VarjoCompositor --ipc-noquery --headsetType=xr3`
// 2) Use Process Explorer in Administrator mode to kill VarjoCompositor started by Varjo Base
// 3) Immediately launch Frame Debugger before Varjo Base restarts the process

struct LayersData
{
	struct LayerParameters
	{
		struct ViewParameters
		{
			XMFLOAT4X4 preWarpMatrix;    // Offset:    0
			XMFLOAT4X4 plane;            // Offset:   64
			XMFLOAT4X4 delta;            // Offset:  128
			XMFLOAT4X4 projMatrix;       // Offset:  192
			XMFLOAT4X4 inverseProjMatrix;// Offset:  256

			struct TextureParameters
			{

				XMFLOAT4 viewport;       // Offset:  320
				int32_t textureSize[2];  // Offset:  336
				int32_t pad[2];          // Offset:  344
				int32_t samples[2];      // Offset:  352
				float sampleDelta[2];    // Offset:  360
			} color;                     // Offset:  320

			struct DepthTextureParameters
			{
				struct TextureParameters
				{

					XMFLOAT4 viewport;       // Offset:  368
					int32_t textureSize[2];  // Offset:  384
					int32_t pad[2];          // Offset:  392
					int32_t samples[2];      // Offset:  400
					float sampleDelta[2];    // Offset:  408
				} tex;                      // Offset:  368
				float minDepth;             // Offset:  416
				float maxDepth;             // Offset:  420
				float nearZ;                // Offset:  424
				float farZ;                 // Offset:  428
				int depthTestRangeLimited;  // Offset:  432
				float depthTestNearZ;       // Offset:  436
				float depthTestFarZ;        // Offset:  440
				int padding;                // Offset:  444
			} depth;                        // Offset:  368
		} views[2];                        // Offset:    0
		int viewFlags;                     // Offset:  896
		float depthOverrideValue;          // Offset:  900
		float blendFalloffSpeed;           // Offset:  904
		float blendBlueNoiseFalloffSpeed;  // Offset:  908
		int maskingFlags;                  // Offset:  912
		int timewarpMode;                  // Offset:  916
		int32_t padding[2];                // Offset:  920

	} layers[6];                            // Offset:    0 Size:  5568
	XMFLOAT4X4 colorMatrix;                 // Offset: 5568 Size:    64
	XMFLOAT4X4 flip;                        // Offset: 5632 Size:    64
	XMFLOAT4X4 contextProjectionInv;        // Offset: 5696 Size:    64
	XMFLOAT4X4 focusToContextNoDistortion;  // Offset: 5760 Size:    64
	XMFLOAT4X4 contextToFocusNoDistortion;  // Offset: 5824 Size:    64
	XMFLOAT4 eye;                           // Offset: 5888 Size:    16
	XMFLOAT4 debugColor;                    // Offset: 5904 Size:    16
	XMFLOAT4 vertexUvVp;                    // Offset: 5920 Size:    16
	int layersCount;                        // Offset: 5936 Size:     4
	float time;                             // Offset: 5940 Size:     4
	float stripeCentrum;                    // Offset: 5944 Size:     4
	int disableColorCorrection;             // Offset: 5948 Size:     4
	bool showTelemetry;                     // Offset: 5952 Size:     4
	int blendState;                         // Offset: 5956 Size:     4
	int blendType;                          // Offset: 5960 Size:     4
	int blendShape;                         // Offset: 5964 Size:     4
};

struct ConstantBuffer
{
	XMFLOAT4 eyeViewports;               // Offset:    0 Size:    16
	XMFLOAT4 focusToContextDistortVp;    // Offset:   16 Size:    16 [unused]
	XMFLOAT4 focusMinMax;                // Offset:   32 Size:    16
	float colorRampUvRange[2];           // Offset:   48 Size:     8
	float focusFade;                   // Offset:   56 Size:     4
	int disableColorCorrection;        // Offset:   60 Size:     4
	int blendState;                    // Offset:   64 Size:     4
	int blendType;                     // Offset:   68 Size:     4
	int blendShape;                    // Offset:   72 Size:     4 [unused]
	int showTelemetry;                 // Offset:   76 Size:     4
};

struct DirectModeDeviceParams
{
	bool showUi;                   // Offset:    0
	bool maskedVSTEnabled;         // Offset:    4
	int disableDistortion;         // Offset:    8
	bool disableSuperSampling;     // Offset:   12
} deviceParams;                    // Offset:    0 Size:    16


/////////////////////////////////////////////////////////////
// Hook to patch the constant buffers.

void (*original_ID3D11DeviceContext_UpdateSubresource)(
	ID3D11DeviceContext* pContext,
	ID3D11Resource* pDstResource,
	UINT            DstSubresource,
	const D3D11_BOX* pDstBox,
	const void* pSrcData,
	UINT            SrcRowPitch,
	UINT            SrcDepthPitch) = nullptr;
void ID3D11DeviceContext_UpdateSubresource(
	ID3D11DeviceContext* pContext,
	ID3D11Resource* pDstResource,
	UINT            DstSubresource,
	const D3D11_BOX* pDstBox,
	const void* pSrcData,
	UINT            SrcRowPitch,
	UINT            SrcDepthPitch) {
	TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource");
	LayersData local1;
	ConstantBuffer local2;
	DirectModeDeviceParams local3;
	if (pDstResource) {
		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		if (SUCCEEDED(pDstResource->QueryInterface(buffer.GetAddressOf()))) {
			D3D11_BUFFER_DESC desc{};
			buffer->GetDesc(&desc);
			TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource",
				TLArg(desc.ByteWidth, "Size"),
				TLArg(SrcRowPitch, "RowPitch"));

			// Detect which constant buffer is being uploaded.
			if (desc.ByteWidth == sizeof(LayersData)) {
				TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic_LayersData");

				// Alter data.
				LayersData* p = (LayersData*)pSrcData;
				local1 = *p;

				TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic", TLArg(local1.layersCount, "LayersCount"));
				for (uint32_t i = 0; i < local1.layersCount; i++) {
					TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic",
						TLArg(local1.layers[i].views[0].preWarpMatrix._11, "P_11"), TLArg(local1.layers[i].views[0].preWarpMatrix._12, "P_12"), TLArg(local1.layers[i].views[0].preWarpMatrix._13, "P_13"), TLArg(local1.layers[i].views[0].preWarpMatrix._14, "P_14"),
						TLArg(local1.layers[i].views[0].preWarpMatrix._21, "P_21"), TLArg(local1.layers[i].views[0].preWarpMatrix._22, "P_22"), TLArg(local1.layers[i].views[0].preWarpMatrix._23, "P_23"), TLArg(local1.layers[i].views[0].preWarpMatrix._24, "P_24"),
						TLArg(local1.layers[i].views[0].preWarpMatrix._31, "P_31"), TLArg(local1.layers[i].views[0].preWarpMatrix._32, "P_32"), TLArg(local1.layers[i].views[0].preWarpMatrix._33, "P_33"), TLArg(local1.layers[i].views[0].preWarpMatrix._34, "P_34"),
						TLArg(local1.layers[i].views[0].preWarpMatrix._41, "P_41"), TLArg(local1.layers[i].views[0].preWarpMatrix._42, "P_42"), TLArg(local1.layers[i].views[0].preWarpMatrix._43, "P_43"), TLArg(local1.layers[i].views[0].preWarpMatrix._44, "P_44"));
#if 0
					// PATCH 1: Remove DELTA
					memset(&local1.layers[i].views[0].delta, 0, sizeof(local1.layers[0].views[0].delta));
					memset(&local1.layers[i].views[1].delta, 0, sizeof(local1.layers[0].views[1].delta));
#endif
					TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic",
						TLArg(local1.layers[i].views[0].delta._11, "D_11"), TLArg(local1.layers[i].views[0].delta._12, "D_12"), TLArg(local1.layers[i].views[0].delta._13, "D_13"), TLArg(local1.layers[i].views[0].delta._14, "D_14"),
						TLArg(local1.layers[i].views[0].delta._21, "D_21"), TLArg(local1.layers[i].views[0].delta._22, "D_22"), TLArg(local1.layers[i].views[0].delta._23, "D_23"), TLArg(local1.layers[i].views[0].delta._24, "D_24"),
						TLArg(local1.layers[i].views[0].delta._31, "D_31"), TLArg(local1.layers[i].views[0].delta._32, "D_32"), TLArg(local1.layers[i].views[0].delta._33, "D_33"), TLArg(local1.layers[i].views[0].delta._34, "D_34"),
						TLArg(local1.layers[i].views[0].delta._41, "D_41"), TLArg(local1.layers[i].views[0].delta._42, "D_42"), TLArg(local1.layers[i].views[0].delta._43, "D_43"), TLArg(local1.layers[i].views[0].delta._44, "D_44"));
#if 0
					// PATCH 2: Disable TW
					local1.layers[i].timewarpMode = 0;
#endif
					TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic", TLArg(local1.layers[i].timewarpMode, "TimewarpMode"));
				}
#if 0
				// PATCH 3: Disable blend - though I suspect it is related to focus view blending
				local1.blendState = 0;
#endif
				TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic", TLArg(local1.blendState, "BlendState"));
#if 0
				// PATCH 4: Disable time advance
				local1.time = 0;
#endif
				TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic", TLArg(local1.time, "Time"));

				pSrcData = &local1;
			}
			else if (desc.ByteWidth == sizeof(ConstantBuffer)) {
				TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic_ConstantBuffer");

				// Alter data.
				ConstantBuffer* p = (ConstantBuffer*)pSrcData;
				local2 = *p;

#if 0
				// PATCH 3: Disable blend
				local2.blendState = 0;
#endif
				TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic", TLArg(local2.blendState, "BlendState"));

				pSrcData = &local2;
			}
#if 0
			else if (desc.ByteWidth == sizeof(DirectModeDeviceParams)) {
				TraceLoggingWrite(g_traceProvider, "ID3D11DeviceContext_UpdateSubresource_Magic_DirectModeDeviceParams");

				// Alter data.
				DirectModeDeviceParams* p = (DirectModeDeviceParams*)pSrcData;
				local3 = *p;

				pSrcData = &local3;
			}
#endif
		}
	}
	return original_ID3D11DeviceContext_UpdateSubresource(pContext, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_context;
std::future<void> g_deferredHook;

void DeferredHooking() {
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	D3D_FEATURE_LEVEL level;
	CoInitialize(0);
	const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), &level, g_context.GetAddressOf());
	if (SUCCEEDED(hr)) {
		// THis is how VarjoCompositor is updating constant buffers.
		DetourMethodAttach(g_context.Get(), 48, ID3D11DeviceContext_UpdateSubresource, original_ID3D11DeviceContext_UpdateSubresource);
		TraceLoggingWrite(g_traceProvider, "D3D11CreateDevice_Hooked");
	}
	else {
		TraceLoggingWrite(g_traceProvider, "D3D11CreateDevice_Failed", TLArg(hr));
	}
}

// Detours require at least one exported symbol.
void __declspec(dllexport) dummy() {}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		DetourRestoreAfterWith();
		TraceLoggingRegister(g_traceProvider);
		TraceLoggingWrite(g_traceProvider, "Hello");
		// Not good to create COM/D3D stuff from DllMain - defer it to later.
		g_deferredHook = std::async(std::launch::async, [] { DeferredHooking(); });
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

