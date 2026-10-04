//========= Portal VR ==========================================================//
//
// Purpose: Direct3D 9 / Win32 helpers for Portal VR.
//
// Kept in a separate translation unit (no precompiled header, no Source headers) so
// that windows.h and d3d9.h never meet the Source SDK's macros.
//
//=============================================================================//
#ifndef VR_D3D_H
#define VR_D3D_H
#ifdef _WIN32
#pragma once
#endif

// Loads openvr_api.dll from the same folder as client.dll. Returns false and fills
// pszError if it can't be loaded.
bool VRD3D_LoadOpenVRLibrary( char *pszError, int nErrorSize );

// Full path of the folder containing client.dll (no trailing slash).
void VRD3D_GetClientDllFolder( char *pszOut, int nOutSize );

// Finds the engine's IDirect3DDevice9 (inside shaderapidx9.dll). NULL if not found.
void *VRD3D_FindEngineDevice();

// Patches IDirect3DDevice9::CreateTexture on the engine's device so that the render
// target with exactly (nWidth x nHeight) is created as a D3D9Ex shared texture. The
// texture pointer and its share handle are captured for submission to SteamVR.
// Returns false if the device is not a D3D9Ex device (sharing impossible).
// pD3DDevice may be NULL, in which case the engine's device is looked up.
bool VRD3D_InstallCaptureHook( void *pD3DDevice, int nWidth, int nHeight );

// The captured eye texture's DXGI share handle, or NULL if not created yet.
void *VRD3D_GetEyeTextureShareHandle();

// Blocks until the GPU has finished all work submitted so far on the engine's device,
// so the compositor reads a finished frame. Returns false on timeout.
bool VRD3D_WaitForGPU( void *pD3DDevice );

// Copies the shared eye texture into a D3D11 texture (ID3D11Texture2D*) for submission.
// Call after VRD3D_WaitForGPU. Returns NULL and fills pszError on failure.
void *VRD3D_GetSubmitTexture( char *pszError, int nErrorSize );

// Saves the last submitted frame (both eyes) as a BMP.
bool VRD3D_DumpSubmitTexture( const char *pszPath, char *pszError, int nErrorSize );

// The engine device found by VRD3D_InstallCaptureHook.
void *VRD3D_GetDevice();

// Debug string describing the hook state.
const char *VRD3D_GetStatus();

#endif // VR_D3D_H

// Debugging: logs fatal exceptions (address, module + offset, likely return addresses on
// the stack) to <mod>\vr_crash.txt. Safe to call more than once.
void VRD3D_InstallCrashLogger();
