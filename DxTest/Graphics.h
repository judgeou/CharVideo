#ifndef GRAPHICS_20201021
#define GRAPHICS_20201021

#include <array>
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "D3DCompiler.lib")

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

	auto GetDevice() {
		return pDevice;
	}

	void DrawTestTriangle() {
		struct Vertex {
			float x, y;
		};

		Vertex v[] = { 
			{ 0, 0.5 },
			{ 0.5, -0.5 },
			{ -0.5, -0.5 }
		}; 
		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = 0;
		bufferDesc.ByteWidth = sizeof(v);
		bufferDesc.StructureByteStride = sizeof(Vertex);
		
		D3D11_SUBRESOURCE_DATA subRes = {};
		subRes.pSysMem = v;

		ComPtr<ID3D11Buffer> buffer;
		auto hret = pDevice->CreateBuffer(&bufferDesc, &subRes, &buffer);

		UINT stride = sizeof(Vertex);
		UINT offset = 0;
		pContext->IASetVertexBuffers(0, 1, buffer.GetAddressOf(), &stride, &offset);

		ComPtr<ID3D11VertexShader> vertexShader;
		ComPtr<ID3DBlob> blob;
		hret = D3DReadFileToBlob(L"Shader\\VertexShader.cso", &blob);
		hret = pDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vertexShader);
		pContext->VSSetShader(vertexShader.Get(), nullptr, 0);

		ComPtr<ID3D11PixelShader> pixelShader;
		ComPtr<ID3DBlob> pixelBlob;
		hret = D3DReadFileToBlob(L"Shader\\PixelShader.cso", &pixelBlob);
		hret = pDevice->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &pixelShader);
		pContext->PSSetShader(pixelShader.Get(), nullptr, 0);

		ComPtr<ID3D11InputLayout> pInputLayout;
		D3D11_INPUT_ELEMENT_DESC ied[] = {
			{ "Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		hret = pDevice->CreateInputLayout(ied, std::size(ied), blob->GetBufferPointer(), blob->GetBufferSize(), &pInputLayout);
		pContext->IASetInputLayout(pInputLayout.Get());

		pContext->OMSetRenderTargets(1, pTarget.GetAddressOf(), nullptr);

		pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		D3D11_VIEWPORT vp;
		vp.Width = 800;
		vp.Height = 600;
		vp.MinDepth = 0;
		vp.MaxDepth = 1;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		pContext->RSSetViewports(1, &vp);

		pContext->Draw(std::size(v), 0);
	}
private:
	ComPtr<ID3D11Device> pDevice;
	ComPtr<IDXGISwapChain> pSwp;
	ComPtr<ID3D11DeviceContext> pContext;
	ComPtr<ID3D11RenderTargetView> pTarget;
};

#endif // !GRAPHICS_20201021
