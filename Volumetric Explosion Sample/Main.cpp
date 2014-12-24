//--------------------------------------------------------------------------------------
// Realistic Volumetric Explosions in Games.
// GPU Pro 6
// Alex Dunn
//
// This sample demonstrates how to render volumetric explosions in games 
// using DirectX 11.  The source code presented below forms the basis of
// the matching chapter in the book GPU Pro 6.
//--------------------------------------------------------------------------------------
#include <windows.h>
#include <windowsx.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <DirectXPackedVector.h>
#include <directxcolors.h>
#include <fstream>
#include <DDSTextureLoader.h>
#include <AntTweakBar.h>

#include "Common.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

//--------------------------------------------------------------------------------------
// Global Variables
//--------------------------------------------------------------------------------------
HINSTANCE               g_hInst = nullptr;
HWND                    g_hWnd = nullptr;
D3D_DRIVER_TYPE         g_driverType = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL       g_featureLevel = D3D_FEATURE_LEVEL_11_0;
ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11Device1*          g_pd3dDevice1 = nullptr;
ID3D11DeviceContext*    g_pImmediateContext = nullptr;
ID3D11DeviceContext1*   g_pImmediateContext1 = nullptr;
IDXGISwapChain*         g_pSwapChain = nullptr;
IDXGISwapChain1*        g_pSwapChain1 = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

ID3D11Buffer*               g_pExplosionParamsCB = nullptr;
ID3D11ShaderResourceView*   g_pNoiseVolumeSRV = nullptr;
ID3D11ShaderResourceView*   g_pGradientSRV = nullptr;
ID3D11VertexShader*         g_pRenderExplosionVS = nullptr;
ID3D11HullShader*           g_pRenderExplosionHS = nullptr;
ID3D11DomainShader*         g_pRenderExplosionDS = nullptr;
ID3D11PixelShader*          g_pRenderExplosionPS = nullptr;
ID3D11InputLayout*          g_pExplosionLayout = nullptr;
ID3D11SamplerState*         g_pSamplerClampedLinear = nullptr;
ID3D11SamplerState*         g_pSamplerWrappedLinear = nullptr;

ID3D11DepthStencilState*    g_pTestWriteDepth = nullptr;
ID3D11BlendState*           g_pOverBlendState = nullptr;
 
// Explosion parameters. 
const XMFLOAT3 kNoiseAnimationSpeed(0.0f, 0.02f, 0.0f);
const float kNoiseInitialAmplitude = 3.0f;
const UINT kMaxNumSteps = 256;
const UINT kNumHullSteps = 2;
const float kStepSize = 0.04f;
const UINT kNumOctaves = 4;
const UINT kNumHullOctaves = 2;
const float kSkinThicknessBias = 0.6f;
const float kTessellationFactor = 16;
float g_MaxSkinThickness;
float g_MaxNoiseDisplacement;
static bool g_EnableHullShrinking = true;
static float g_EdgeSoftness = 0.05f;
static float g_NoiseScale = 0.04f;
static float g_ExplosionRadius = 4.0f;
static float g_DisplacementAmount = 1.75f;
static XMFLOAT2 g_UvScaleBias(2.1f, 0.35f);
static float g_NoiseAmplitudeFactor = 0.4f;
static float g_NoiseFrequencyFactor = 3.0f;

// Camera variables.
const UINT kResolutionX = 800;
const UINT kResolutionY = 640;
const float kNearClip = 0.01f;
const float kFarClip = 20.0f;
const XMFLOAT3 kEyeLookAtWS = XMFLOAT3(0.0f, 0.0f, 0.0f);
XMFLOAT3 g_EyePositionWS;
XMFLOAT3 g_EyeForwardWS;
XMMATRIX g_WorldToProjectionMatrix, g_ProjectionToWorldMatrix, g_InvProjMatrix, g_ViewToWorldMatrix;
XMFLOAT4X4 g_ViewMatrix, g_ProjMatrix;
XMFLOAT4 g_ProjectionParams;
POINT g_LastMousePos;
float g_CameraTheta = 0, g_CameraPhi = 0, g_CameraRadius = 10;

// Timer Variables
__int64 g_CounterStart = 0;
double g_CountsPerSecond = 0.0;
double g_ElapsedTime = 0.0;

TwBar* g_pUI;

enum PrimitiveType
{
    kPrimitiveSphere,
    kPrimitiveCylinder,
    kPrimitiveCone,
    kPrimitiveTorus,
    kPrimitiveBox
} g_Primitive = kPrimitiveSphere;

//--------------------------------------------------------------------------------------
// Forward declarations
//--------------------------------------------------------------------------------------
HRESULT InitWindow( HINSTANCE hInstance, int nCmdShow );
HRESULT InitDevice();
void InitUI();
void CleanupDevice();
LRESULT CALLBACK    WndProc( HWND, UINT, WPARAM, LPARAM );
void Render();
void StartTimer();
double GetTime();
void OnMouseDown(int x, int y);
void OnMouseUp();
void OnMouseMove(WPARAM btnState, int x, int y);
void UpdateViewMatrix();

//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    if( FAILED( InitWindow( hInstance, nCmdShow ) ) )
        return 0;

    if( FAILED( InitDevice() ) )
    {
        CleanupDevice();
        return 0;
    }

    StartTimer();

    TwInit(TW_DIRECT3D11, g_pd3dDevice);
    TwWindowSize(kResolutionX, kResolutionY);
    
    InitUI();

    // Main message loop
    MSG msg = {0};
    while( WM_QUIT != msg.message )
    {
        if( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            // Update scene timer
            g_ElapsedTime = GetTime();

            Render();
        }
    }

    TwTerminate();

    CleanupDevice();

    return ( int )msg.wParam;
}


//--------------------------------------------------------------------------------------
// Register class and create window
//--------------------------------------------------------------------------------------
HRESULT InitWindow( HINSTANCE hInstance, int nCmdShow )
{
    // Register class
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof( WNDCLASSEX );
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon( hInstance, "directx.ico" );
    wcex.hCursor = LoadCursor( nullptr, IDC_ARROW );
    wcex.hbrBackground = ( HBRUSH )( COLOR_WINDOW + 1 );
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = "VolumetricExplosionSample";
    wcex.hIconSm = LoadIcon( wcex.hInstance, "directx.ico" );
    if( !RegisterClassEx( &wcex ) ) return E_FAIL;

    // Create window
    g_hInst = hInstance;
    RECT rc = { 0, 0, kResolutionX, kResolutionY };
    AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );
    g_hWnd = CreateWindow( "VolumetricExplosionSample", "GPU Pro 6 - Volumetric Explosions",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance,
                           nullptr );

    if( !g_hWnd ) return E_FAIL;

    ShowWindow( g_hWnd, nCmdShow );

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT InitDevice()
{
    HRESULT hr = S_OK;

    RECT rc;
    GetClientRect( g_hWnd, &rc );
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT numDriverTypes = ARRAYSIZE( driverTypes );

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    UINT numFeatureLevels = ARRAYSIZE( featureLevels );

    for( UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++ )
    {
        g_driverType = driverTypes[driverTypeIndex];
        hr = D3D11CreateDevice( nullptr, g_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
                                D3D11_SDK_VERSION, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext );

        if ( hr == E_INVALIDARG )
        {
            // DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
            hr = D3D11CreateDevice( nullptr, g_driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
                                    D3D11_SDK_VERSION, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext );
        }

        if( SUCCEEDED( hr ) )
            break;
    }
    if( FAILED( hr ) )
        return hr;

    // Obtain DXGI factory from device (since we used nullptr for pAdapter above)
    IDXGIFactory1* dxgiFactory = nullptr;
    {
        IDXGIDevice* dxgiDevice = nullptr;
        hr = g_pd3dDevice->QueryInterface( __uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice) );
        if (SUCCEEDED(hr))
        {
            IDXGIAdapter* adapter = nullptr;
            hr = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(hr))
            {
                hr = adapter->GetParent( __uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory) );
                adapter->Release();
            }
            dxgiDevice->Release();
        }
    }
    if (FAILED(hr))
        return hr;

    // Create swap chain
    IDXGIFactory2* dxgiFactory2 = nullptr;
    hr = dxgiFactory->QueryInterface( __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2) );
    if ( dxgiFactory2 )
    {
        // DirectX 11.1 or later
        hr = g_pd3dDevice->QueryInterface( __uuidof(ID3D11Device1), reinterpret_cast<void**>(&g_pd3dDevice1) );
        if (SUCCEEDED(hr))
        {
            (void) g_pImmediateContext->QueryInterface( __uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&g_pImmediateContext1) );
        }

        DXGI_SWAP_CHAIN_DESC1 sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.Width = width;
        sd.Height = height;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 1;

        hr = dxgiFactory2->CreateSwapChainForHwnd( g_pd3dDevice, g_hWnd, &sd, nullptr, nullptr, &g_pSwapChain1 );
        if (SUCCEEDED(hr))
        {
            hr = g_pSwapChain1->QueryInterface( __uuidof(IDXGISwapChain), reinterpret_cast<void**>(&g_pSwapChain) );
        }

        dxgiFactory2->Release();
    }
    else
    {
        // DirectX 11.0 systems
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 1;
        sd.BufferDesc.Width = width;
        sd.BufferDesc.Height = height;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = g_hWnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;

        hr = dxgiFactory->CreateSwapChain( g_pd3dDevice, &sd, &g_pSwapChain );
    }

    // Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
    dxgiFactory->MakeWindowAssociation( g_hWnd, DXGI_MWA_NO_ALT_ENTER );

    dxgiFactory->Release();

    if (FAILED(hr))
        return hr;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast<void**>( &pBackBuffer ) );
    if( FAILED( hr ) ) return hr;

    hr = g_pd3dDevice->CreateRenderTargetView( pBackBuffer, nullptr, &g_pRenderTargetView );
    pBackBuffer->Release();
    if( FAILED( hr ) ) return hr;

    ID3DBlob* pVSBlob = NULL;
    if( FAILED( hr = D3DReadFileToBlob( L"RenderExplosionVS.cso", &pVSBlob ) ) ) return hr;
    if( FAILED( hr = g_pd3dDevice->CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &g_pRenderExplosionVS ) ) )
    {	
        pVSBlob->Release();
        return hr;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE( layout );

    hr = g_pd3dDevice->CreateInputLayout( layout, numElements, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &g_pExplosionLayout );
    pVSBlob->Release();
    if( FAILED( hr ) ) return hr;

    ID3DBlob* pHSBlob = nullptr;
    if( FAILED( hr = D3DReadFileToBlob( L"RenderExplosionHS.cso", &pHSBlob ) ) ) return hr;
    hr = g_pd3dDevice->CreateHullShader( pHSBlob->GetBufferPointer(), pHSBlob->GetBufferSize(), nullptr, &g_pRenderExplosionHS );
    pHSBlob->Release();
    if( FAILED( hr ) ) return hr;

    ID3DBlob* pDSBlob = nullptr;
    if( FAILED( hr = D3DReadFileToBlob( L"RenderExplosionDS.cso", &pDSBlob ) ) ) return hr;
    hr = g_pd3dDevice->CreateDomainShader( pDSBlob->GetBufferPointer(), pDSBlob->GetBufferSize(), nullptr, &g_pRenderExplosionDS );
    pDSBlob->Release();
    if( FAILED( hr ) ) return hr;

    ID3DBlob* pPSBlob = nullptr;
    if( FAILED( hr = D3DReadFileToBlob( L"RenderExplosionPS.cso", &pPSBlob ) ) ) return hr;
    hr = g_pd3dDevice->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &g_pRenderExplosionPS );
    pPSBlob->Release();
    if( FAILED( hr ) ) return hr;

    D3D11_BUFFER_DESC bd;
    ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof( ExplosionParams );
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pd3dDevice->CreateBuffer( &bd, nullptr, &g_pExplosionParamsCB );
    if( FAILED( hr ) ) return hr;

    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory( &sampDesc, sizeof(sampDesc) );
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = g_pd3dDevice->CreateSamplerState( &sampDesc, &g_pSamplerClampedLinear );
    if( FAILED( hr ) ) return hr;

    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    hr = g_pd3dDevice->CreateSamplerState( &sampDesc, &g_pSamplerWrappedLinear );
    if( FAILED( hr ) ) return hr;

    HALF maxNoiseValue = 0, minNoiseValue = 0xFFFF;
    HALF noiseValues[32*32*32] = { 0 };
    std::fstream f;
    f.open("noise_32x32x32.dat", std::ios::in);
    if(f.is_open())
    {
        for(UINT i=0 ; i<32*32*32 ; i++)
        {
            HALF noiseValue;
            f >> noiseValue;

            maxNoiseValue = max(maxNoiseValue, noiseValue);
            minNoiseValue = min(minNoiseValue, noiseValue);

            noiseValues[i] = noiseValue;
        }
        f.close();
    }

    D3D11_TEXTURE3D_DESC texDesc;
    ZeroMemory( &texDesc, sizeof(texDesc) );
    texDesc.BindFlags =  D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.Depth = 32;
    texDesc.Format = DXGI_FORMAT_R16_FLOAT;
    texDesc.Height = 32;
    texDesc.MipLevels = 1;
    texDesc.MiscFlags = 0;
    texDesc.Usage =  D3D11_USAGE_DEFAULT;
    texDesc.Width = 32; 

    ID3D11Texture3D* pNoiseVolume;
    D3D11_SUBRESOURCE_DATA initialData;
    initialData.pSysMem = noiseValues;
    initialData.SysMemPitch = 32 * 2;
    initialData.SysMemSlicePitch = 32 * 32 * 2;

    hr = g_pd3dDevice->CreateTexture3D( &texDesc, &initialData, &pNoiseVolume );
    if( FAILED( hr ) ) return hr;

    hr = g_pd3dDevice->CreateShaderResourceView( pNoiseVolume, nullptr, &g_pNoiseVolumeSRV );
    if( FAILED( hr ) ) return hr;

    hr = CreateDDSTextureFromFile( g_pd3dDevice, L"gradient.dds", nullptr, &g_pGradientSRV );
    if( FAILED( hr ) ) return hr;

    D3D11_DEPTH_STENCIL_DESC dsDesc;
    ZeroMemory( &dsDesc, sizeof(dsDesc) );
    dsDesc.DepthEnable = true;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.StencilEnable = false;

    hr = g_pd3dDevice->CreateDepthStencilState( &dsDesc, &g_pTestWriteDepth );
    if( FAILED( hr ) ) return hr;

    D3D11_BLEND_DESC bsDesc;
    ZeroMemory( &bsDesc, sizeof(bsDesc) );
    bsDesc.AlphaToCoverageEnable = false;
    bsDesc.RenderTarget[0].BlendEnable = true;
    bsDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bsDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bsDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bsDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    bsDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bsDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bsDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = g_pd3dDevice->CreateBlendState( &bsDesc, &g_pOverBlendState );
    if( FAILED( hr ) ) return hr;


    // Calculate the maximum possible displacement from noise based on our 
    //  fractal noise parameters.  This is used to ensure our explosion primitive 
    //  fits in our base sphere.
    float largestAbsoluteNoiseValue = max( abs( XMConvertHalfToFloat( maxNoiseValue ) ), abs( XMConvertHalfToFloat( minNoiseValue ) ) );
    g_MaxNoiseDisplacement = 0;
    for(UINT i=0 ; i<kNumOctaves ; i++)
    {
        g_MaxNoiseDisplacement += largestAbsoluteNoiseValue * kNoiseInitialAmplitude * powf(g_NoiseAmplitudeFactor, (float)i);
    }

    // Calculate the skin thickness, which is amount of displacement to add 
    //  to the geometry hull after shrinking it around the explosion primitive.
    g_MaxSkinThickness = 0;
    for(UINT i=kNumHullOctaves ; i<kNumOctaves ; i++)
    {
        g_MaxSkinThickness += largestAbsoluteNoiseValue * kNoiseInitialAmplitude * powf(g_NoiseAmplitudeFactor, (float)i);
    }
    // Add a little bit extra to account for under-tessellation.  This should be
    //  fine tuned on a per use basis for best performance.
    g_MaxSkinThickness += kSkinThicknessBias;

    XMStoreFloat4x4( &g_ProjMatrix, XMMatrixPerspectiveFovLH( 60*PI/180, (float)kResolutionX/kResolutionY, kNearClip, kFarClip ) );
    XMVECTOR det;
    g_InvProjMatrix = XMMatrixInverse( &det, XMLoadFloat4x4 (&g_ProjMatrix ) );

    const float A = kFarClip / (kFarClip - kNearClip);
    const float B = (-kFarClip * kNearClip) / (kFarClip - kNearClip);
    const float C = (kFarClip - kNearClip);
    const float D = kNearClip;
    g_ProjectionParams = XMFLOAT4( A, B, C, D );

    UpdateViewMatrix();

    return S_OK;
}

void InitUI()
{
    g_pUI = TwNewBar("Controls");
    TwDefine(" GLOBAL help='Realistic Volumetric Explosions in Games\nGPU Pro 6\nAlex Dunn - 23/12/2014 \n\nHold the LMB and move the mouse to rotate the scene.\n\n' "); // Message added to the help bar.
    int barSize[2] = {210, 180};
    TwSetParam(g_pUI, NULL, "size", TW_PARAM_INT32, 2, barSize);
    TwAddVarRW(g_pUI, "Use Tight Hull", TW_TYPE_BOOL8, &g_EnableHullShrinking, "");
    TwAddVarRW(g_pUI, "Edge Softness", TW_TYPE_FLOAT, &g_EdgeSoftness, "min=0 max=1 step=0.001");
    TwAddVarRW(g_pUI, "Radius", TW_TYPE_FLOAT, &g_ExplosionRadius, "min=0 max=8 step=0.01");
    TwAddVarRW(g_pUI, "Displacement", TW_TYPE_FLOAT, &g_DisplacementAmount, "min=0 max=8 step=0.01");
    TwAddVarRW(g_pUI, "Amplitude Factor", TW_TYPE_FLOAT, &g_NoiseAmplitudeFactor, "min=0 max=10 step=0.01");
    TwAddVarRW(g_pUI, "Frequency Factor", TW_TYPE_FLOAT, &g_NoiseFrequencyFactor, "min=0 max=10 step=0.01");
    TwAddVarRW(g_pUI, "Noise Scale", TW_TYPE_FLOAT, &g_NoiseScale, "min=0 max=1 step=0.001");
    TwAddVarRW(g_pUI, "UV Scale", TW_TYPE_FLOAT, &g_UvScaleBias.x, "min=-10 max=10 step=0.01");
    TwAddVarRW(g_pUI, "UV Bias", TW_TYPE_FLOAT, &g_UvScaleBias.y, "min=-10 max=10 step=0.01");
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void CleanupDevice()
{
    if( g_pImmediateContext ) g_pImmediateContext->ClearState();

    if( g_pExplosionParamsCB ) g_pExplosionParamsCB->Release();
    if( g_pNoiseVolumeSRV ) g_pNoiseVolumeSRV->Release();
    if( g_pGradientSRV ) g_pGradientSRV->Release();
    if( g_pRenderExplosionVS ) g_pRenderExplosionVS->Release();
    if( g_pRenderExplosionHS ) g_pRenderExplosionHS->Release();
    if( g_pRenderExplosionDS ) g_pRenderExplosionDS->Release();
    if( g_pRenderExplosionPS ) g_pRenderExplosionPS->Release();
    if( g_pExplosionLayout ) g_pExplosionLayout->Release();
    if( g_pSamplerClampedLinear ) g_pSamplerClampedLinear->Release();
    if( g_pSamplerWrappedLinear ) g_pSamplerWrappedLinear->Release();
    if( g_pTestWriteDepth ) g_pTestWriteDepth->Release();
    if( g_pOverBlendState ) g_pOverBlendState->Release();

    if( g_pRenderTargetView ) g_pRenderTargetView->Release();
    if( g_pSwapChain1 ) g_pSwapChain1->Release();
    if( g_pSwapChain ) g_pSwapChain->Release();
    if( g_pImmediateContext1 ) g_pImmediateContext1->Release();
    if( g_pImmediateContext ) g_pImmediateContext->Release();
    if( g_pd3dDevice1 ) g_pd3dDevice1->Release();
    if( g_pd3dDevice ) g_pd3dDevice->Release();
}


//--------------------------------------------------------------------------------------
// Called every time the application receives a message
//--------------------------------------------------------------------------------------
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    if( TwEventWin(hWnd, message, wParam, lParam) ) return 0;

    PAINTSTRUCT ps;
    HDC hdc;

    switch( message )
    {
    case WM_PAINT:
        hdc = BeginPaint( hWnd, &ps );
        EndPaint( hWnd, &ps );
        break;

    case WM_DESTROY:
        PostQuitMessage( 0 );
        break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        OnMouseUp();
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

        // Note that this tutorial does not handle resizing (WM_SIZE) requests,
        // so we created the window without the resize border.
    default:
        return DefWindowProc( hWnd, message, wParam, lParam );
    }

    return 0;
}

void StartTimer()
{
    LARGE_INTEGER frequencyCount;
    QueryPerformanceFrequency(&frequencyCount);

    g_CountsPerSecond = double(frequencyCount.QuadPart);

    QueryPerformanceCounter(&frequencyCount);
    g_CounterStart = frequencyCount.QuadPart;
}

double GetTime()
{
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    return double(currentTime.QuadPart-g_CounterStart)/g_CountsPerSecond;
}

void OnMouseDown(int x, int y)
{
    g_LastMousePos.x = x;
    g_LastMousePos.y = y;

    SetCapture(g_hWnd);
}

void OnMouseUp()
{
    ReleaseCapture();
}

void OnMouseMove(WPARAM btnState, int x, int y)
{
    bool updateMatrices = false;

    if( (btnState & MK_LBUTTON) != 0 )
    {
        float dx = XMConvertToRadians(0.25f * (float)(x - g_LastMousePos.x));
        float dy = XMConvertToRadians(0.25f * (float)(y - g_LastMousePos.y));

        g_CameraTheta -= dx;
        g_CameraPhi   -= dy;

        updateMatrices = true;
    }
    else if( (btnState & MK_RBUTTON) != 0 )
    {
        float dx = 0.1f * (float)(x - g_LastMousePos.x);
        float dy = 0.1f * (float)(y - g_LastMousePos.y);

        g_CameraRadius += dx - dy;

        updateMatrices = true;
    }

    g_LastMousePos.x = x;
    g_LastMousePos.y = y;

    if(updateMatrices)
    {
        UpdateViewMatrix();
    }
}

void UpdateViewMatrix()
{
    g_CameraRadius = max(g_CameraRadius, 1.0f);
    g_CameraRadius = min(g_CameraRadius, 20.0f);

    g_CameraPhi = max(g_CameraPhi, 0.1f);
    g_CameraPhi = min(g_CameraPhi, PI - 0.1f);

    float x = g_CameraRadius * sinf(g_CameraPhi) * cosf(g_CameraTheta);
    float y = g_CameraRadius * cosf(g_CameraPhi);
    float z = g_CameraRadius * sinf(g_CameraPhi) * sinf(g_CameraTheta);

    g_EyePositionWS = XMFLOAT3(x, y, z);

    XMStoreFloat4x4(&g_ViewMatrix, XMMatrixLookAtLH(XMLoadFloat3(&g_EyePositionWS), XMLoadFloat3(&kEyeLookAtWS), XMLoadFloat3(&XMFLOAT3(0, 1, 0))));
    XMStoreFloat3( &g_EyeForwardWS, XMVector3Normalize( XMVectorSubtract( XMLoadFloat3(&kEyeLookAtWS), XMLoadFloat3(&g_EyePositionWS) ) ) );

    g_WorldToProjectionMatrix = XMMatrixMultiply( XMLoadFloat4x4(&g_ViewMatrix), XMLoadFloat4x4(&g_ProjMatrix) );

    XMVECTOR det;
    g_ProjectionToWorldMatrix = XMMatrixInverse( &det, g_WorldToProjectionMatrix);
    g_ViewToWorldMatrix = XMMatrixInverse( &det, XMLoadFloat4x4( &g_ViewMatrix ) );
}

void UpdateExplosionParams(ID3D11DeviceContext* const pContext)
{
    D3D11_MAPPED_SUBRESOURCE MappedSubResource;
    pContext->Map( g_pExplosionParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource );
    {
        ((ExplosionParams *)MappedSubResource.pData)->g_WorldToViewMatrix = g_ViewMatrix;
        ((ExplosionParams *)MappedSubResource.pData)->g_ViewToProjectionMatrix = g_ProjMatrix;
        XMStoreFloat4x4( &((ExplosionParams *)MappedSubResource.pData)->g_ProjectionToViewMatrix, g_InvProjMatrix );
        XMStoreFloat4x4( &((ExplosionParams *)MappedSubResource.pData)->g_WorldToProjectionMatrix, g_WorldToProjectionMatrix );
        XMStoreFloat4x4( &((ExplosionParams *)MappedSubResource.pData)->g_ProjectionToWorldMatrix, g_ProjectionToWorldMatrix );
        XMStoreFloat4x4( &((ExplosionParams *)MappedSubResource.pData)->g_ViewToWorldMatrix, g_ViewToWorldMatrix );
        ((ExplosionParams *)MappedSubResource.pData)->g_EyePositionWS = g_EyePositionWS;
        ((ExplosionParams *)MappedSubResource.pData)->g_NoiseAmplitudeFactor = g_NoiseAmplitudeFactor;
        ((ExplosionParams *)MappedSubResource.pData)->g_EyeForwardWS = g_EyeForwardWS;
        ((ExplosionParams *)MappedSubResource.pData)->g_NoiseScale = g_NoiseScale;
        ((ExplosionParams *)MappedSubResource.pData)->g_ProjectionParams = g_ProjectionParams;
        ((ExplosionParams *)MappedSubResource.pData)->g_ScreenParams = XMFLOAT4((FLOAT)kResolutionX, (FLOAT)kResolutionY, 1.f/kResolutionX, 1.f/kResolutionY);
        ((ExplosionParams *)MappedSubResource.pData)->g_ExplosionPositionWS = kEyeLookAtWS;
        ((ExplosionParams *)MappedSubResource.pData)->g_ExplosionRadiusWS = g_ExplosionRadius;
        ((ExplosionParams *)MappedSubResource.pData)->g_NoiseAnimationSpeed = kNoiseAnimationSpeed;
        ((ExplosionParams *)MappedSubResource.pData)->g_Time = (float)g_ElapsedTime;
        ((ExplosionParams *)MappedSubResource.pData)->g_EdgeSoftness = g_EdgeSoftness;
        ((ExplosionParams *)MappedSubResource.pData)->g_NoiseFrequencyFactor = g_NoiseFrequencyFactor;
        ((ExplosionParams *)MappedSubResource.pData)->g_PrimitiveIdx = g_Primitive;
        ((ExplosionParams *)MappedSubResource.pData)->g_Opacity = 1.0f;
        ((ExplosionParams *)MappedSubResource.pData)->g_DisplacementWS = g_DisplacementAmount;
        ((ExplosionParams *)MappedSubResource.pData)->g_StepSizeWS = kStepSize;
        ((ExplosionParams *)MappedSubResource.pData)->g_MaxNumSteps = kMaxNumSteps;
        ((ExplosionParams *)MappedSubResource.pData)->g_UvScaleBias = g_UvScaleBias;
        ((ExplosionParams *)MappedSubResource.pData)->g_NoiseInitialAmplitude = kNoiseInitialAmplitude;
        ((ExplosionParams *)MappedSubResource.pData)->g_InvMaxNoiseDisplacement = 1.0f/g_MaxNoiseDisplacement;
        ((ExplosionParams *)MappedSubResource.pData)->g_NumOctaves = kNumOctaves;
        ((ExplosionParams *)MappedSubResource.pData)->g_SkinThickness = g_MaxSkinThickness;
        ((ExplosionParams *)MappedSubResource.pData)->g_NumHullOctaves = kNumHullOctaves;
        ((ExplosionParams *)MappedSubResource.pData)->g_NumHullSteps = g_EnableHullShrinking ? kNumHullSteps : 0;
        ((ExplosionParams *)MappedSubResource.pData)->g_TessellationFactor = kTessellationFactor;
    }
    pContext->Unmap( g_pExplosionParamsCB, 0 );
}


//--------------------------------------------------------------------------------------
// Render a frame
//--------------------------------------------------------------------------------------
void Render()
{
    g_pImmediateContext->ClearRenderTargetView( g_pRenderTargetView, Colors::Black );

    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)kResolutionX;
    vp.Height = (FLOAT)kResolutionY;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports( 1, &vp );

    const float blendFactor[4] = { 1, 1, 1, 1 };
    g_pImmediateContext->OMSetBlendState(g_pOverBlendState, blendFactor, 0xFFFFFFFF);
    g_pImmediateContext->OMSetDepthStencilState(g_pTestWriteDepth, 0);
    g_pImmediateContext->OMSetRenderTargets( 1, &g_pRenderTargetView, nullptr );

    g_pImmediateContext->IASetInputLayout( g_pExplosionLayout );
    g_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST );

    g_pImmediateContext->VSSetShader( g_pRenderExplosionVS, nullptr, 0 );
    g_pImmediateContext->HSSetShader( g_pRenderExplosionHS, nullptr, 0 );
    g_pImmediateContext->DSSetShader( g_pRenderExplosionDS, nullptr, 0 );
    g_pImmediateContext->PSSetShader( g_pRenderExplosionPS, nullptr, 0 );

    UpdateExplosionParams( g_pImmediateContext );

    ID3D11SamplerState* const pSamplers[] = { g_pSamplerClampedLinear, g_pSamplerWrappedLinear };
    g_pImmediateContext->DSSetSamplers( S_BILINEAR_CLAMPED_SAMPLER, 2, pSamplers );
    g_pImmediateContext->PSSetSamplers( S_BILINEAR_CLAMPED_SAMPLER, 2, pSamplers );

    g_pImmediateContext->HSSetConstantBuffers( B_EXPLOSION_PARAMS, 1, &g_pExplosionParamsCB );
    g_pImmediateContext->DSSetConstantBuffers( B_EXPLOSION_PARAMS, 1, &g_pExplosionParamsCB );
    g_pImmediateContext->PSSetConstantBuffers( B_EXPLOSION_PARAMS, 1, &g_pExplosionParamsCB );

    g_pImmediateContext->DSSetShaderResources( T_NOISE_VOLUME, 1, &g_pNoiseVolumeSRV );
    g_pImmediateContext->PSSetShaderResources( T_NOISE_VOLUME, 1, &g_pNoiseVolumeSRV );
    g_pImmediateContext->PSSetShaderResources( T_GRADIENT_TEX, 1, &g_pGradientSRV );

    g_pImmediateContext->Draw( 1, 0 );

    TwDraw();

    g_pSwapChain->Present( 0, 0 );
}