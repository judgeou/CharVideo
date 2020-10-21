#ifndef GRAPHICS_20201021
#define GRAPHICS_20201021

#include <Windows.h>
#include <d3d11.h>
#include <wrl.h>
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

class Graphics {
public:
	Graphics(HWND hwnd) {
		DXGI_SWAP_CHAIN_DESC sd = {};
		sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sd.SampleDesc.Count = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 1;
		sd.OutputWindow = hwnd;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		sd.Flags = 0;

		D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			0,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&sd,
			&pSwp,
			&pDevice,
			nullptr,
			&pContext
		);

		ComPtr<ID3D11Resource> pBackBuffer;
		pSwp->GetBuffer(0, __uuidof(ID3D11Resource), &pBackBuffer);
		pDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &pTarget);

	}

	~Graphics () {

	}

	void EndFrame() {
		pSwp->Present(1, 0);
	}

	void ClearBuffer(float r = 0, float g = 0, float b = 0) {
		FLOAT color[] = { r, g, b, 1.0f };
		pContext->ClearRenderTargetView(pTarget.Get(), color);
	}
private:
	ComPtr<ID3D11Device> pDevice;
	ComPtr<IDXGISwapChain> pSwp;
	ComPtr<ID3D11DeviceContext> pContext;
	ComPtr<ID3D11RenderTargetView> pTarget;
};

#endif // !GRAPHICS_20201021
