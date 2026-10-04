//========= Portal VR ==========================================================//
//
// Purpose: Direct3D 9 / Win32 helpers for Portal VR (see vr_d3d.h).
//
// NOTE: this file is compiled WITHOUT the client precompiled header and must not
// include any Source SDK header.
//
//=============================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>

#include "vr_d3d.h"

#pragma comment( lib, "d3d9.lib" )
#pragma comment( lib, "d3d11.lib" )

// Index of CreateTexture in the IDirect3DDevice9 vtable (QueryInterface, AddRef, Release,
// TestCooperativeLevel, GetAvailableTextureMem, EvictManagedResources, GetDirect3D,
// GetDeviceCaps, GetDisplayMode, GetCreationParameters, SetCursorProperties,
// SetCursorPosition, ShowCursor, CreateAdditionalSwapChain, GetSwapChain,
// GetNumberOfSwapChains, Reset, Present, GetBackBuffer, GetRasterStatus,
// SetDialogBoxMode, SetGammaRamp, GetGammaRamp, CreateTexture).
static const int VTABLE_INDEX_CREATETEXTURE = 23;

typedef HRESULT ( STDMETHODCALLTYPE *CreateTextureFn )( IDirect3DDevice9 *pThis, UINT Width, UINT Height, UINT Levels,
	DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle );

static CreateTextureFn s_pfnOrigCreateTexture = NULL;
static void **s_pVTable = NULL;
static UINT s_nCaptureWidth = 0;
static UINT s_nCaptureHeight = 0;
static IDirect3DTexture9 *s_pEyeTexture = NULL;
static HANDLE s_hEyeShareHandle = NULL;
static IDirect3DQuery9 *s_pEventQuery = NULL;
static IDirect3DDevice9 *s_pDevice = NULL;
static char s_szStatus[256] = "not installed";
static int s_nHookCalls = 0;
static int s_nRTCalls = 0;
static char s_szLastRTs[512] = "";

static HRESULT STDMETHODCALLTYPE Hooked_CreateTexture( IDirect3DDevice9 *pThis, UINT Width, UINT Height, UINT Levels,
	DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle )
{
	s_nHookCalls++;
	if ( Usage & D3DUSAGE_RENDERTARGET )
	{
		s_nRTCalls++;
		char szEntry[48];
		_snprintf_s( szEntry, sizeof( szEntry ), _TRUNCATE, "%ux%u/%d ", Width, Height, (int)Pool );
		if ( strlen( s_szLastRTs ) + strlen( szEntry ) < sizeof( s_szLastRTs ) - 1 )
			strcat_s( s_szLastRTs, sizeof( s_szLastRTs ), szEntry );
	}

	if ( Width == s_nCaptureWidth && Height == s_nCaptureHeight && ( Usage & D3DUSAGE_RENDERTARGET ) &&
		 Pool == D3DPOOL_DEFAULT && pSharedHandle == NULL )
	{
		HANDLE hShare = NULL;
		HRESULT hr = s_pfnOrigCreateTexture( pThis, Width, Height, Levels, Usage, Format, Pool, ppTexture, &hShare );
		if ( SUCCEEDED( hr ) && ppTexture && *ppTexture )
		{
			// Keep our own reference: if the material system ever releases and recreates its
			// render targets, the new texture simply replaces this one.
			if ( s_pEyeTexture )
				s_pEyeTexture->Release();
			s_pEyeTexture = *ppTexture;
			s_pEyeTexture->AddRef();
			s_hEyeShareHandle = hShare;
			s_pDevice = pThis;
			_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "eye texture %ux%u fmt %d shared handle %p",
				Width, Height, (int)Format, hShare );
			return hr;
		}

		// Sharing failed (not a D3D9Ex device?): fall back to a normal texture so the game still runs.
		_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "shared CreateTexture failed (hr=0x%08lx)", (unsigned long)hr );
		return s_pfnOrigCreateTexture( pThis, Width, Height, Levels, Usage, Format, Pool, ppTexture, NULL );
	}

	return s_pfnOrigCreateTexture( pThis, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle );
}

//-----------------------------------------------------------------------------
// The SDK only exposes the D3D device on Xbox 360, so find the engine's device by
// scanning shaderapidx9.dll's data for a pointer to a COM object whose vtable lives in
// d3d9.dll and that answers QueryInterface for IDirect3DDevice9.
//-----------------------------------------------------------------------------
static bool IsReadable( const void *p, size_t nSize )
{
	MEMORY_BASIC_INFORMATION mbi;
	if ( !p || !VirtualQuery( p, &mbi, sizeof( mbi ) ) )
		return false;
	if ( mbi.State != MEM_COMMIT || ( mbi.Protect & ( PAGE_NOACCESS | PAGE_GUARD ) ) )
		return false;
	const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	if ( !( mbi.Protect & readable ) )
		return false;
	return (const char *)p + nSize <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

static bool InModule( const void *p, HMODULE hModule )
{
	const IMAGE_DOS_HEADER *pDos = (const IMAGE_DOS_HEADER *)hModule;
	const IMAGE_NT_HEADERS *pNt = (const IMAGE_NT_HEADERS *)( (const char *)hModule + pDos->e_lfanew );
	const char *pBase = (const char *)hModule;
	return (const char *)p >= pBase && (const char *)p < pBase + pNt->OptionalHeader.SizeOfImage;
}

// Exception-safe pointer read.
static bool SafeRead( const void *p, void **pOut )
{
	__try
	{
		*pOut = *(void *const *)p;
		return true;
	}
	__except ( EXCEPTION_EXECUTE_HANDLER )
	{
		return false;
	}
}

// A device of our own, used to learn what a d3d9 device's IUnknown methods look like.
static IDirect3DDevice9Ex *s_pDummyDevice = NULL;

static IDirect3DDevice9Ex *GetDummyDevice()
{
	if ( s_pDummyDevice )
		return s_pDummyDevice;
	IDirect3D9Ex *pD3D = NULL;
	if ( FAILED( Direct3DCreate9Ex( D3D_SDK_VERSION, &pD3D ) ) || !pD3D )
		return NULL;
	HWND hWnd = CreateWindowExA( 0, "STATIC", "portalvr_d3d", WS_POPUP, 0, 0, 16, 16, NULL, NULL, NULL, NULL );
	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_UNKNOWN;
	pp.BackBufferWidth = 16;
	pp.BackBufferHeight = 16;
	pp.hDeviceWindow = hWnd;
	if ( FAILED( pD3D->CreateDeviceEx( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, NULL, &s_pDummyDevice ) ) )
		s_pDummyDevice = NULL;
	// The dummy device and window are intentionally kept alive.
	return s_pDummyDevice;
}

// Only accepts objects whose IUnknown methods are the same functions as a real device's,
// so no unknown code is ever called during the scan.
static IDirect3DDevice9 *TryDevice( void *pCandidate, void *const *pKnownVTable )
{
	if ( (uintptr_t)pCandidate < 0x10000 || ( (uintptr_t)pCandidate & 3 ) || pCandidate == s_pDummyDevice )
		return NULL;
	void *pVTable = NULL;
	if ( !SafeRead( pCandidate, &pVTable ) || (uintptr_t)pVTable < 0x10000 )
		return NULL;
	void *pQI = NULL, *pAddRef = NULL, *pRelease = NULL;
	if ( !SafeRead( (void **)pVTable + 0, &pQI ) || !SafeRead( (void **)pVTable + 1, &pAddRef ) || !SafeRead( (void **)pVTable + 2, &pRelease ) )
		return NULL;
	if ( pQI != pKnownVTable[0] || pAddRef != pKnownVTable[1] || pRelease != pKnownVTable[2] )
		return NULL;

	IDirect3DDevice9 *pDevice = NULL;
	if ( FAILED( ( (IUnknown *)pCandidate )->QueryInterface( __uuidof( IDirect3DDevice9 ), (void **)&pDevice ) ) )
		return NULL;
	pDevice->Release(); // the engine holds the real reference
	return pDevice;
}

void *VRD3D_FindEngineDevice()
{
	HMODULE hShaderAPI = GetModuleHandleA( "shaderapidx9.dll" );
	IDirect3DDevice9Ex *pDummy = GetDummyDevice();
	if ( !hShaderAPI || !pDummy )
		return NULL;
	void *const *pKnownVTable = *(void *const **)pDummy;

	const IMAGE_DOS_HEADER *pDos = (const IMAGE_DOS_HEADER *)hShaderAPI;
	const IMAGE_NT_HEADERS *pNt = (const IMAGE_NT_HEADERS *)( (const char *)hShaderAPI + pDos->e_lfanew );

	// Pass 0: globals that point at the device. Pass 1: globals that point at an object
	// (e.g. the shader device) holding the device in its first 1 KB.
	for ( int nPass = 0; nPass < 2; nPass++ )
	{
		const IMAGE_SECTION_HEADER *pSection = IMAGE_FIRST_SECTION( pNt );
		for ( int i = 0; i < pNt->FileHeader.NumberOfSections; i++, pSection++ )
		{
			if ( !( pSection->Characteristics & IMAGE_SCN_MEM_WRITE ) )
				continue;
			void **pStart = (void **)( (char *)hShaderAPI + pSection->VirtualAddress );
			size_t nCount = pSection->Misc.VirtualSize / sizeof( void * );
			for ( size_t j = 0; j < nCount; j++ )
			{
				void *pValue = pStart[j];
				if ( nPass == 0 )
				{
					IDirect3DDevice9 *pDevice = TryDevice( pValue, pKnownVTable );
					if ( pDevice )
						return pDevice;
				}
				else
				{
					if ( (uintptr_t)pValue < 0x10000 || ( (uintptr_t)pValue & 3 ) || InModule( pValue, hShaderAPI ) )
						continue;
					for ( int k = 0; k < 256; k++ )
					{
						void *pInner = NULL;
						if ( !SafeRead( (void **)pValue + k, &pInner ) )
							break;
						IDirect3DDevice9 *pDevice = TryDevice( pInner, pKnownVTable );
						if ( pDevice )
							return pDevice;
					}
				}
			}
		}
	}
	return NULL;
}

static bool PatchVTable( void **pVTable, int nIndex, void *pHook, void **ppOriginal )
{
	DWORD dwOldProtect;
	if ( !VirtualProtect( &pVTable[nIndex], sizeof( void * ), PAGE_READWRITE, &dwOldProtect ) )
		return false;
	if ( ppOriginal )
		*ppOriginal = pVTable[nIndex];
	pVTable[nIndex] = pHook;
	VirtualProtect( &pVTable[nIndex], sizeof( void * ), dwOldProtect, &dwOldProtect );
	FlushInstructionCache( GetCurrentProcess(), &pVTable[nIndex], sizeof( void * ) );
	return true;
}

// Hooks CreateTexture on the vtable shared by the engine's device.
static bool HookDevice( IDirect3DDevice9 *pDevice )
{
	s_pDevice = pDevice;

	IDirect3DDevice9Ex *pDeviceEx = NULL;
	if ( FAILED( pDevice->QueryInterface( __uuidof( IDirect3DDevice9Ex ), (void **)&pDeviceEx ) ) || !pDeviceEx )
	{
		_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "device is not D3D9Ex (launched with -nod3d9ex?)" );
		return false;
	}
	pDeviceEx->Release();

	if ( s_pVTable )
		return true;

	void **pVTable = *(void ***)pDevice;
	if ( !PatchVTable( pVTable, VTABLE_INDEX_CREATETEXTURE, (void *)&Hooked_CreateTexture, (void **)&s_pfnOrigCreateTexture ) )
	{
		_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "VirtualProtect failed (%lu)", GetLastError() );
		return false;
	}
	s_pVTable = pVTable;
	_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "hook installed, waiting for %ux%u render target", s_nCaptureWidth, s_nCaptureHeight );
	return true;
}

//-----------------------------------------------------------------------------
// If the engine has not created its device yet, catch it being created.
// IDirect3D9 vtable: ... GetAdapterMonitor (15), CreateDevice (16), ...
// IDirect3D9Ex adds GetAdapterModeCountEx (17), EnumAdapterModesEx (18),
// GetAdapterDisplayModeEx (19), CreateDeviceEx (20).
//-----------------------------------------------------------------------------
typedef HRESULT ( STDMETHODCALLTYPE *CreateDeviceFn )( IDirect3D9 *pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
	DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DDevice9 **ppReturnedDeviceInterface );
typedef HRESULT ( STDMETHODCALLTYPE *CreateDeviceExFn )( IDirect3D9Ex *pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
	DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode, IDirect3DDevice9Ex **ppReturnedDeviceInterface );
static CreateDeviceFn s_pfnOrigCreateDevice = NULL;
static CreateDeviceExFn s_pfnOrigCreateDeviceEx = NULL;

static HRESULT STDMETHODCALLTYPE Hooked_CreateDevice( IDirect3D9 *pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
	DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DDevice9 **ppDevice )
{
	HRESULT hr = s_pfnOrigCreateDevice( pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppDevice );
	if ( SUCCEEDED( hr ) && ppDevice && *ppDevice )
		HookDevice( *ppDevice );
	return hr;
}

static HRESULT STDMETHODCALLTYPE Hooked_CreateDeviceEx( IDirect3D9Ex *pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
	DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode, IDirect3DDevice9Ex **ppDevice )
{
	HRESULT hr = s_pfnOrigCreateDeviceEx( pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode, ppDevice );
	if ( SUCCEEDED( hr ) && ppDevice && *ppDevice )
		HookDevice( *ppDevice );
	return hr;
}

static bool HookDeviceCreation()
{
	IDirect3D9Ex *pD3D = NULL;
	if ( FAILED( Direct3DCreate9Ex( D3D_SDK_VERSION, &pD3D ) ) || !pD3D )
		return false;
	void **pVTable = *(void ***)pD3D;
	bool bOk = PatchVTable( pVTable, 16, (void *)&Hooked_CreateDevice, (void **)&s_pfnOrigCreateDevice ) &&
			   PatchVTable( pVTable, 20, (void *)&Hooked_CreateDeviceEx, (void **)&s_pfnOrigCreateDeviceEx );
	// Keep pD3D alive: releasing the last IDirect3D9 could unload state we patched.
	return bOk;
}

bool VRD3D_InstallCaptureHook( void *pD3DDevice, int nWidth, int nHeight )
{
	s_nCaptureWidth = (UINT)nWidth;
	s_nCaptureHeight = (UINT)nHeight;

	if ( s_pVTable )
		return true; // already hooked; just updated the size

	if ( !pD3DDevice )
		pD3DDevice = VRD3D_FindEngineDevice();
	if ( pD3DDevice )
		return HookDevice( (IDirect3DDevice9 *)pD3DDevice );

	// Last resort: no device yet, hook its creation.
	if ( !HookDeviceCreation() )
	{
		_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "engine D3D device not found and device creation could not be hooked" );
		return false;
	}
	_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "waiting for the engine to create its D3D device" );
	return true;
}

void *VRD3D_GetEyeTextureShareHandle()
{
	return s_hEyeShareHandle;
}

bool VRD3D_WaitForGPU( void *pD3DDevice )
{
	IDirect3DDevice9 *pDevice = (IDirect3DDevice9 *)pD3DDevice;
	if ( !pDevice )
		return false;

	if ( !s_pEventQuery )
	{
		if ( FAILED( pDevice->CreateQuery( D3DQUERYTYPE_EVENT, &s_pEventQuery ) ) )
		{
			s_pEventQuery = NULL;
			return false;
		}
	}

	s_pEventQuery->Issue( D3DISSUE_END );

	// The compositor reads the texture from another device, so make sure our rendering is
	// done. This normally takes well under a millisecond after the frame's draw calls.
	LARGE_INTEGER freq, start, now;
	QueryPerformanceFrequency( &freq );
	QueryPerformanceCounter( &start );
	for ( ;; )
	{
		HRESULT hr = s_pEventQuery->GetData( NULL, 0, D3DGETDATA_FLUSH );
		if ( hr == S_OK )
			return true;
		if ( hr != S_FALSE )
			return false; // device lost or similar

		QueryPerformanceCounter( &now );
		if ( ( now.QuadPart - start.QuadPart ) * 1000 / freq.QuadPart > 50 )
			return false;
		YieldProcessor();
	}
}

void VRD3D_GetClientDllFolder( char *pszOut, int nOutSize )
{
	HMODULE hModule = NULL;
	GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&VRD3D_GetClientDllFolder, &hModule );
	char szPath[MAX_PATH] = { 0 };
	GetModuleFileNameA( hModule, szPath, sizeof( szPath ) );
	char *pSlash = strrchr( szPath, '\\' );
	if ( pSlash )
		*pSlash = '\0';
	strncpy_s( pszOut, nOutSize, szPath, _TRUNCATE );
}

bool VRD3D_LoadOpenVRLibrary( char *pszError, int nErrorSize )
{
	if ( GetModuleHandleA( "openvr_api.dll" ) )
		return true;

	char szFolder[MAX_PATH];
	VRD3D_GetClientDllFolder( szFolder, sizeof( szFolder ) );

	char szDll[MAX_PATH];
	_snprintf_s( szDll, sizeof( szDll ), _TRUNCATE, "%s\\openvr_api.dll", szFolder );

	// openvr_api.dll is delay-loaded by client.dll; loading it here by full path means the
	// delay-load helper later resolves to this module.
	if ( !LoadLibraryExA( szDll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH ) )
	{
		_snprintf_s( pszError, nErrorSize, _TRUNCATE, "could not load %s (error %lu)", szDll, GetLastError() );
		return false;
	}
	return true;
}

void *VRD3D_GetDevice()
{
	return s_pDevice;
}

//-----------------------------------------------------------------------------
// D3D11 bridge: SteamVR does not accept the D3D9Ex share handle directly, so open it on a
// D3D11 device of our own and copy it into a texture the compositor understands.
//-----------------------------------------------------------------------------
static ID3D11Device *s_pD3D11Device = NULL;
static ID3D11DeviceContext *s_pD3D11Context = NULL;
static ID3D11Texture2D *s_pShared11 = NULL;
static HANDLE s_hShared11Handle = NULL;
static ID3D11Texture2D *s_pSubmit11 = NULL;

void *VRD3D_GetSubmitTexture( char *pszError, int nErrorSize )
{
	HANDLE hShare = s_hEyeShareHandle;
	if ( !hShare )
	{
		_snprintf_s( pszError, nErrorSize, _TRUNCATE, "no shared eye texture" );
		return NULL;
	}

	if ( !s_pD3D11Device )
	{
		D3D_FEATURE_LEVEL level;
		HRESULT hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
			&s_pD3D11Device, &level, &s_pD3D11Context );
		if ( FAILED( hr ) )
		{
			_snprintf_s( pszError, nErrorSize, _TRUNCATE, "D3D11CreateDevice failed (0x%08lx)", (unsigned long)hr );
			return NULL;
		}
	}

	if ( s_hShared11Handle != hShare )
	{
		if ( s_pShared11 ) { s_pShared11->Release(); s_pShared11 = NULL; }
		if ( s_pSubmit11 ) { s_pSubmit11->Release(); s_pSubmit11 = NULL; }
		HRESULT hr = s_pD3D11Device->OpenSharedResource( hShare, __uuidof( ID3D11Texture2D ), (void **)&s_pShared11 );
		if ( FAILED( hr ) || !s_pShared11 )
		{
			_snprintf_s( pszError, nErrorSize, _TRUNCATE, "OpenSharedResource failed (0x%08lx)", (unsigned long)hr );
			return NULL;
		}
		D3D11_TEXTURE2D_DESC desc;
		s_pShared11->GetDesc( &desc );
		desc.MiscFlags = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		hr = s_pD3D11Device->CreateTexture2D( &desc, NULL, &s_pSubmit11 );
		if ( FAILED( hr ) )
		{
			_snprintf_s( pszError, nErrorSize, _TRUNCATE, "CreateTexture2D failed (0x%08lx)", (unsigned long)hr );
			return NULL;
		}
		s_hShared11Handle = hShare;
		_snprintf_s( s_szStatus, sizeof( s_szStatus ), _TRUNCATE, "D3D11 bridge %ux%u format %d", desc.Width, desc.Height, (int)desc.Format );
	}

	s_pD3D11Context->CopyResource( s_pSubmit11, s_pShared11 );
	return s_pSubmit11;
}

bool VRD3D_DumpSubmitTexture( const char *pszPath, char *pszError, int nErrorSize )
{
	if ( !s_pSubmit11 )
	{
		_snprintf_s( pszError, nErrorSize, _TRUNCATE, "nothing submitted yet" );
		return false;
	}
	D3D11_TEXTURE2D_DESC desc;
	s_pSubmit11->GetDesc( &desc );
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	ID3D11Texture2D *pStaging = NULL;
	if ( FAILED( s_pD3D11Device->CreateTexture2D( &desc, NULL, &pStaging ) ) )
	{
		_snprintf_s( pszError, nErrorSize, _TRUNCATE, "staging texture failed" );
		return false;
	}
	s_pD3D11Context->CopyResource( pStaging, s_pSubmit11 );
	D3D11_MAPPED_SUBRESOURCE mapped;
	if ( FAILED( s_pD3D11Context->Map( pStaging, 0, D3D11_MAP_READ, 0, &mapped ) ) )
	{
		pStaging->Release();
		_snprintf_s( pszError, nErrorSize, _TRUNCATE, "map failed" );
		return false;
	}

	FILE *fp = NULL;
	if ( fopen_s( &fp, pszPath, "wb" ) != 0 || !fp )
	{
		s_pD3D11Context->Unmap( pStaging, 0 );
		pStaging->Release();
		_snprintf_s( pszError, nErrorSize, _TRUNCATE, "cannot write %s", pszPath );
		return false;
	}
	// 32-bit top-down BMP; the texture is BGRA, which is BMP's byte order.
	const int w = (int)desc.Width, h = (int)desc.Height;
	BITMAPFILEHEADER fh = {};
	BITMAPINFOHEADER ih = {};
	fh.bfType = 0x4D42;
	fh.bfOffBits = sizeof( fh ) + sizeof( ih );
	fh.bfSize = fh.bfOffBits + w * h * 4;
	ih.biSize = sizeof( ih );
	ih.biWidth = w;
	ih.biHeight = -h;
	ih.biPlanes = 1;
	ih.biBitCount = 32;
	ih.biCompression = BI_RGB;
	fwrite( &fh, sizeof( fh ), 1, fp );
	fwrite( &ih, sizeof( ih ), 1, fp );
	for ( int y = 0; y < h; y++ )
		fwrite( (const char *)mapped.pData + y * mapped.RowPitch, 4, w, fp );
	fclose( fp );
	s_pD3D11Context->Unmap( pStaging, 0 );
	pStaging->Release();
	return true;
}

const char *VRD3D_GetStatus()
{
	static char szFull[1024];
	_snprintf_s( szFull, sizeof( szFull ), _TRUNCATE, "%s | CreateTexture calls %d, render targets %d: %s",
		s_szStatus, s_nHookCalls, s_nRTCalls, s_szLastRTs );
	return szFull;
}

//-----------------------------------------------------------------------------
// Crash logger
//-----------------------------------------------------------------------------
#include <psapi.h>
#pragma comment( lib, "psapi.lib" )

static int s_nLoggedCrashes = 0;

static void DescribeAddress( DWORD_PTR addr, char *pszOut, int nOutSize )
{
	HMODULE hMod = NULL;
	if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hMod ) && hMod )
	{
		char szPath[MAX_PATH];
		GetModuleFileNameA( hMod, szPath, sizeof( szPath ) );
		const char *pszName = strrchr( szPath, '\\' );
		_snprintf_s( pszOut, nOutSize, _TRUNCATE, "%s+0x%X", pszName ? pszName + 1 : szPath, (unsigned)( addr - (DWORD_PTR)hMod ) );
	}
	else
	{
		_snprintf_s( pszOut, nOutSize, _TRUNCATE, "0x%08X", (unsigned)addr );
	}
	pszOut[nOutSize - 1] = 0;
}

static bool IsCodeAddress( DWORD_PTR addr )
{
	MEMORY_BASIC_INFORMATION mbi;
	if ( !VirtualQuery( (LPCVOID)addr, &mbi, sizeof( mbi ) ) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE )
		return false;
	return ( mbi.Protect & ( PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) ) != 0;
}

struct CrashFile_t { HANDLE h; };
static void CrashPrintf( CrashFile_t *fp, const char *pszFmt, ... )
{
	char szBuf[1024];
	va_list args;
	va_start( args, pszFmt );
	int n = _vsnprintf_s( szBuf, sizeof( szBuf ), _TRUNCATE, pszFmt, args );
	va_end( args );
	if ( n < 0 )
		n = (int)strlen( szBuf );
	DWORD dwWritten;
	WriteFile( fp->h, szBuf, n, &dwWritten, NULL );
}

static LONG CALLBACK VRCrashHandler( PEXCEPTION_POINTERS pInfo )
{
	DWORD code = pInfo->ExceptionRecord->ExceptionCode;
	if ( code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO
		 && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION && code != 0xC0000409 /* stack buffer overrun */ )
		return EXCEPTION_CONTINUE_SEARCH;
	if ( s_nLoggedCrashes >= 4 )
		return EXCEPTION_CONTINUE_SEARCH;
	s_nLoggedCrashes++;

	char szFolder[MAX_PATH], szPath[MAX_PATH];
	VRD3D_GetClientDllFolder( szFolder, sizeof( szFolder ) );
	_snprintf_s( szPath, sizeof( szPath ), _TRUNCATE, "%s\\..\\vr_crash.txt", szFolder );
	HANDLE hFile = CreateFileA( szPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return EXCEPTION_CONTINUE_SEARCH;
	CrashFile_t file = { hFile };
	CrashFile_t *fp = &file;

	char szWhere[MAX_PATH + 32];
	DescribeAddress( (DWORD_PTR)pInfo->ExceptionRecord->ExceptionAddress, szWhere, sizeof( szWhere ) );
	CrashPrintf( fp, "=== exception 0x%08X at %s", (unsigned)code, szWhere );
	if ( code == EXCEPTION_ACCESS_VIOLATION && pInfo->ExceptionRecord->NumberParameters >= 2 )
		CrashPrintf( fp, " (%s 0x%08X)", pInfo->ExceptionRecord->ExceptionInformation[0] ? "write" : "read", (unsigned)pInfo->ExceptionRecord->ExceptionInformation[1] );
	CrashPrintf( fp, "\n" );
#ifdef _M_IX86
	const CONTEXT *ctx = pInfo->ContextRecord;
	CrashPrintf( fp, "eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X\n",
		ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp );
	// Likely return addresses on the stack.
	const DWORD_PTR *pStack = (const DWORD_PTR *)ctx->Esp;
	int nFound = 0;
	for ( int i = 0; i < 2048 && nFound < 40; i++ )
	{
		DWORD_PTR value;
		__try { value = pStack[i]; }
		__except ( EXCEPTION_EXECUTE_HANDLER ) { break; }
		if ( IsCodeAddress( value ) )
		{
			DescribeAddress( value, szWhere, sizeof( szWhere ) );
			CrashPrintf( fp, "  [esp+%04X] %s\n", i * 4, szWhere );
			nFound++;
		}
	}
#endif
	CloseHandle( hFile );
	return EXCEPTION_CONTINUE_SEARCH;
}

void VRD3D_InstallCrashLogger()
{
	static bool s_bInstalled = false;
	if ( s_bInstalled )
		return;
	s_bInstalled = true;
	// Leave room on this (the main) thread's stack to log a stack overflow.
	ULONG ulGuarantee = 128 * 1024;
	SetThreadStackGuarantee( &ulGuarantee );
	AddVectoredExceptionHandler( 1, VRCrashHandler );
}

