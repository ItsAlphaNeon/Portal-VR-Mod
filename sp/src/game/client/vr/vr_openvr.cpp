//========= Portal VR ==========================================================//
//
// Purpose: OpenVR (SteamVR) backend for Portal VR. See vr_openvr.h.
//
//=============================================================================//
#include "cbase.h"
#include "vr/vr_openvr.h"
#include "vr/vr_d3d.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "tier0/icommandline.h"
#include "filesystem.h"

#include "openvr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CPortalVR g_PortalVR;

ConVar vr_world_scale( "vr_world_scale", "1.0", FCVAR_ARCHIVE, "Scale of the world relative to you. 1 = real-world scale (1 m = 39.37 units).", true, 0.5f, true, 2.0f );
ConVar vr_debug( "vr_debug", "0", 0, "Print VR debug info to the console and vr_log.txt." );
ConVar vr_timing( "vr_timing", "0", 0, "Log VR frame timing every 2 seconds." );

// Frame timing (milliseconds, accumulated)
static double s_flTimeWait, s_flTimeRender, s_flTimeGPU, s_flTimeSubmit, s_flFrameStart, s_flRenderStart, s_flLastReport;
static int s_nTimedFrames;

static inline vr::IVRSystem *VRSys( void *p ) { return (vr::IVRSystem *)p; }

//-----------------------------------------------------------------------------
// Logging
//-----------------------------------------------------------------------------
void VRLog( const char *pFmt, ... )
{
	char szBuf[2048];
	va_list args;
	va_start( args, pFmt );
	Q_vsnprintf( szBuf, sizeof( szBuf ), pFmt, args );
	va_end( args );

	Msg( "[VR] %s\n", szBuf );

	if ( g_pFullFileSystem )
	{
		FileHandle_t fh = g_pFullFileSystem->Open( "vr_log.txt", "a", "MOD" );
		if ( fh )
		{
			g_pFullFileSystem->FPrintf( fh, "%.3f %s\n", Plat_FloatTime(), szBuf );
			g_pFullFileSystem->Close( fh );
		}
	}
}

//-----------------------------------------------------------------------------
// Coordinate conversion
//
// OpenVR tracking space: right handed, +X right, +Y up, -Z forward, meters.
// Source:                 +X forward, +Y left, +Z up, game units.
//   source.x = -vr.z, source.y = -vr.x, source.z = vr.y
//-----------------------------------------------------------------------------
static const int s_nAxisFromVR[3] = { 2, 0, 1 };
static const float s_flAxisSign[3] = { -1.0f, -1.0f, 1.0f };

static Vector VRVectorToSource( const float v[3], float flScale )
{
	return Vector( s_flAxisSign[0] * v[s_nAxisFromVR[0]] * flScale,
				   s_flAxisSign[1] * v[s_nAxisFromVR[1]] * flScale,
				   s_flAxisSign[2] * v[s_nAxisFromVR[2]] * flScale );
}

static void VRMatrixToSource( const vr::HmdMatrix34_t &m, float flScale, matrix3x4_t &out )
{
	for ( int a = 0; a < 3; a++ )
	{
		for ( int b = 0; b < 3; b++ )
		{
			out[a][b] = s_flAxisSign[a] * s_flAxisSign[b] * m.m[s_nAxisFromVR[a]][s_nAxisFromVR[b]];
		}
		out[a][3] = s_flAxisSign[a] * m.m[s_nAxisFromVR[a]][3] * flScale;
	}
}

static void FillPose( VRTrackedPose_t &pose, const vr::TrackedDevicePose_t &src, float flScale )
{
	pose.bValid = src.bPoseIsValid && src.eTrackingResult == vr::TrackingResult_Running_OK;
	if ( !src.bPoseIsValid )
		return; // keep the last known pose (e.g. a hand briefly losing tracking)

	VRMatrixToSource( src.mDeviceToAbsoluteTracking, flScale, pose.mat );
	pose.vecVelocity = VRVectorToSource( src.vVelocity.v, flScale );
	pose.vecAngVelocity = VRVectorToSource( src.vAngularVelocity.v, 1.0f ) * ( 180.0f / M_PI );
}

//-----------------------------------------------------------------------------
// CPortalVR
//-----------------------------------------------------------------------------
CPortalVR::CPortalVR()
{
	m_bTriedStart = false;
	m_bActive = false;
	m_bLoading = false;
	m_bFrameStarted = false;
	m_pSystem = NULL;
	m_nEyeWidth = 1024;
	m_nEyeHeight = 1024;
	m_pEyeTexture = NULL;
	m_bInputReady = false;
	m_nLastInputFrame = -1;
	m_nSubmitErrors = 0;

	m_HmdPose.bValid = false;
	SetIdentityMatrix( m_HmdPose.mat );
	m_HmdPose.vecVelocity.Init();
	m_HmdPose.vecAngVelocity.Init();
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		m_HandPose[i] = m_HmdPose;
		m_flGripSqueeze[i] = 0.0f;
		for ( int f = 0; f < 5; f++ )
			m_flFingerCurl[i][f] = 0.0f;
	}
	SetIdentityMatrix( m_HeadFromEye[0] );
	SetIdentityMatrix( m_HeadFromEye[1] );
	memset( m_Digital, 0, sizeof( m_Digital ) );
	m_vecMoveStick.Init();
	m_vecTurnStick.Init();
}

float CPortalVR::UnitsPerMeter() const
{
	return 39.3701f * vr_world_scale.GetFloat();
}

bool CPortalVR::StartRuntime()
{
	if ( m_bTriedStart )
		return m_bActive;
	m_bTriedStart = true;

	if ( !CommandLine()->FindParm( "-vr" ) )
		return false;

	char szError[512];
	if ( !VRD3D_LoadOpenVRLibrary( szError, sizeof( szError ) ) )
	{
		VRLog( "%s", szError );
		return false;
	}

	vr::EVRInitError eError = vr::VRInitError_None;
	vr::IVRSystem *pSystem = vr::VR_Init( &eError, vr::VRApplication_Scene );
	if ( !pSystem || eError != vr::VRInitError_None )
	{
		VRLog( "VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription( eError ) );
		return false;
	}
	m_pSystem = pSystem;

	if ( !vr::VRCompositor() )
	{
		VRLog( "SteamVR compositor is not available" );
		vr::VR_Shutdown();
		m_pSystem = NULL;
		return false;
	}
	vr::VRCompositor()->SetTrackingSpace( vr::TrackingUniverseStanding );

	uint32_t nWidth = 0, nHeight = 0;
	pSystem->GetRecommendedRenderTargetSize( &nWidth, &nHeight );
	m_nEyeWidth = MAX( 256, (int)nWidth );
	m_nEyeHeight = MAX( 256, (int)nHeight );

	char szModel[128] = { 0 };
	pSystem->GetStringTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String, szModel, sizeof( szModel ) );
	float flRefresh = pSystem->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float );
	VRLog( "SteamVR started: HMD '%s', %.0f Hz, eye render size %dx%d", szModel, flRefresh, m_nEyeWidth, m_nEyeHeight );

	InitInput();

	m_bActive = true;
	return true;
}

void CPortalVR::Shutdown()
{
	if ( m_pSystem )
	{
		VRLog( "SteamVR shutdown" );
		vr::VR_Shutdown();
		m_pSystem = NULL;
	}
	m_bActive = false;
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------
void CPortalVR::CreateRenderTargets( IMaterialSystem *pMaterialSystem )
{
	if ( !StartRuntime() )
		return;

	// Both eyes are rendered side by side into one texture: a unique size lets the
	// CreateTexture hook find it, and SteamVR takes per-eye bounds of one texture.
	const int nTexWidth = m_nEyeWidth * 2;
	const int nTexHeight = m_nEyeHeight;

	if ( !VRD3D_InstallCaptureHook( NULL, nTexWidth, nTexHeight ) )
		VRLog( "Could not hook texture creation: %s. VR output disabled.", VRD3D_GetStatus() );
	else
		VRLog( "Texture hook: %s", VRD3D_GetStatus() );

	m_pEyeTexture = pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_vr_eyes", nTexWidth, nTexHeight, RT_SIZE_LITERAL,
		IMAGE_FORMAT_BGRA8888, MATERIAL_RT_DEPTH_SEPARATE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD,
		0 );
	if ( m_pEyeTexture )
		m_pEyeTexture->IncrementReferenceCount();

	// The 2D UI (HUD, menus) is painted into this and drawn as a panel in the world.
	ITexture *pGui = pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_gui", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_SHARED,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD,
		0 );
	if ( pGui )
		pGui->IncrementReferenceCount();

	VRLog( "Render targets requested: eyes %dx%d", nTexWidth, nTexHeight );
}

void CPortalVR::ShutdownRenderTargets()
{
	if ( m_pEyeTexture )
	{
		m_pEyeTexture->DecrementReferenceCount();
		m_pEyeTexture = NULL;
	}
}

ITexture *CPortalVR::GetRenderTarget( VREye eEye, EWhichRenderTarget eWhich )
{
	if ( eWhich == RT_Color )
		return m_pEyeTexture;
	return NULL; // the color target has its own (separate) depth buffer
}

void CPortalVR::GetViewportBounds( VREye eEye, int *pnX, int *pnY, int *pnWidth, int *pnHeight )
{
	*pnX = ( eEye == VREye_Left ) ? 0 : m_nEyeWidth;
	*pnY = 0;
	*pnWidth = m_nEyeWidth;
	*pnHeight = m_nEyeHeight;
}

void CPortalVR::GetRenderTargetFrameBufferDimensions( int &nWidth, int &nHeight )
{
	nWidth = m_nEyeWidth;
	nHeight = m_nEyeHeight;
}

bool CPortalVR::GetEyeProjectionMatrix( VMatrix *pResult, VREye eEye, float zNear, float zFar, float fovScale )
{
	if ( !m_pSystem )
		return false;

	vr::HmdMatrix44_t p = VRSys( m_pSystem )->GetProjectionMatrix( eEye == VREye_Left ? vr::Eye_Left : vr::Eye_Right, zNear, zFar );
	for ( int i = 0; i < 4; i++ )
		for ( int j = 0; j < 4; j++ )
			pResult->m[i][j] = p.m[i][j];
	return true;
}

VMatrix CPortalVR::GetMidEyeFromEye( VREye eEye )
{
	return VMatrix( m_HeadFromEye[eEye] );
}

VMatrix CPortalVR::GetMideyePose()
{
	return VMatrix( m_HmdPose.mat );
}

bool CPortalVR::SampleTrackingState( float PlayerGameFov, float fPredictionSeconds )
{
	BeginFrame();
	return m_HmdPose.bValid;
}

void CPortalVR::SetLoading( bool bLoading )
{
	if ( !m_bActive || bLoading == m_bLoading )
		return;

	m_bLoading = bLoading;
	if ( bLoading )
		vr::VRCompositor()->FadeToColor( 0.1f, 0.0f, 0.0f, 0.0f, 1.0f, false );
	if ( vr_debug.GetBool() )
		VRLog( "Loading: %d", bLoading );
}

void CPortalVR::BeginFrame()
{
	if ( !m_bActive || m_bFrameStarted )
		return;

	// Eye offsets can change with the IPD dial and world scale.
	const float flScale = UnitsPerMeter();
	VRMatrixToSource( VRSys( m_pSystem )->GetEyeToHeadTransform( vr::Eye_Left ), flScale, m_HeadFromEye[VREye_Left] );
	VRMatrixToSource( VRSys( m_pSystem )->GetEyeToHeadTransform( vr::Eye_Right ), flScale, m_HeadFromEye[VREye_Right] );

	vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
	double flWaitStart = Plat_FloatTime();
	vr::EVRCompositorError eError = vr::VRCompositor()->WaitGetPoses( poses, vr::k_unMaxTrackedDeviceCount, NULL, 0 );
	s_flRenderStart = Plat_FloatTime();
	s_flTimeWait += s_flRenderStart - flWaitStart;
	if ( eError != vr::VRCompositorError_None && vr_debug.GetBool() )
		VRLog( "WaitGetPoses error %d", (int)eError );

	FillPose( m_HmdPose, poses[vr::k_unTrackedDeviceIndex_Hmd], flScale );

	UpdateInput();

	if ( m_bInputReady )
	{
		for ( int i = 0; i < VR_HAND_COUNT; i++ )
		{
			vr::InputPoseActionData_t data;
			if ( vr::VRInput()->GetPoseActionDataForNextFrame( m_hPose[i], vr::TrackingUniverseStanding, &data, sizeof( data ), vr::k_ulInvalidInputValueHandle ) == vr::VRInputError_None
				 && data.bActive )
			{
				FillPose( m_HandPose[i], data.pose, flScale );
			}
			else
			{
				m_HandPose[i].bValid = false;
			}
		}
	}

	static bool s_bLoggedRenderInfo = false;
	if ( !s_bLoggedRenderInfo && g_pMaterialSystemHardwareConfig )
	{
		s_bLoggedRenderInfo = true;
		int nFBW, nFBH;
		materials->GetBackBufferDimensions( nFBW, nFBH );
		ITexture *pFullFrame = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET, false );
		VRLog( "HDR type %d, backbuffer %dx%d, _rt_FullFrameFB %dx%d", g_pMaterialSystemHardwareConfig->GetHDRType(), nFBW, nFBH,
			pFullFrame ? pFullFrame->GetActualWidth() : -1, pFullFrame ? pFullFrame->GetActualHeight() : -1 );
	}

	m_bFrameStarted = true;
}

void CPortalVR::SubmitFrame()
{
	if ( !m_bActive || !m_bFrameStarted )
		return;
	m_bFrameStarted = false;

	void *hShare = VRD3D_GetEyeTextureShareHandle();
	if ( !hShare )
	{
		if ( m_nSubmitErrors++ < 3 )
			VRLog( "No shared eye texture to submit (%s). Engine texture is %dx%d", VRD3D_GetStatus(),
				m_pEyeTexture ? m_pEyeTexture->GetActualWidth() : -1, m_pEyeTexture ? m_pEyeTexture->GetActualHeight() : -1 );
		return;
	}

	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->Flush( true );
	}
	double flGPUStart = Plat_FloatTime();
	s_flTimeRender += flGPUStart - s_flRenderStart;
	if ( !VRD3D_WaitForGPU( VRD3D_GetDevice() ) && vr_debug.GetBool() )
		VRLog( "GPU wait timed out" );

	char szError[256];
	void *pSubmit = VRD3D_GetSubmitTexture( szError, sizeof( szError ) );
	if ( !pSubmit )
	{
		if ( m_nSubmitErrors++ < 5 )
			VRLog( "Eye texture bridge failed: %s", szError );
		return;
	}
	double flSubmitStart = Plat_FloatTime();
	s_flTimeGPU += flSubmitStart - flGPUStart;
	vr::Texture_t texture = { pSubmit, vr::TextureType_DirectX, vr::ColorSpace_Gamma };
	vr::VRTextureBounds_t boundsLeft = { 0.0f, 0.0f, 0.5f, 1.0f };
	vr::VRTextureBounds_t boundsRight = { 0.5f, 0.0f, 1.0f, 1.0f };

	vr::EVRCompositorError eLeft = vr::VRCompositor()->Submit( vr::Eye_Left, &texture, &boundsLeft );
	vr::EVRCompositorError eRight = vr::VRCompositor()->Submit( vr::Eye_Right, &texture, &boundsRight );
	if ( ( eLeft != vr::VRCompositorError_None || eRight != vr::VRCompositorError_None ) && m_nSubmitErrors++ < 20 )
		VRLog( "Submit failed: left %d right %d", (int)eLeft, (int)eRight );

	vr::VRCompositor()->PostPresentHandoff();

	double flNow = Plat_FloatTime();
	s_flTimeSubmit += flNow - flSubmitStart;
	s_nTimedFrames++;
	if ( vr_timing.GetBool() && flNow - s_flLastReport > 2.0 )
	{
		double n = MAX( 1, s_nTimedFrames );
		VRLog( "timing: %.1f fps | wait %.2f ms, render %.2f ms, gpu sync+copy %.2f ms, submit %.2f ms",
			s_nTimedFrames / ( flNow - s_flLastReport ), 1000 * s_flTimeWait / n, 1000 * s_flTimeRender / n,
			1000 * s_flTimeGPU / n, 1000 * s_flTimeSubmit / n );
		s_flTimeWait = s_flTimeRender = s_flTimeGPU = s_flTimeSubmit = 0;
		s_nTimedFrames = 0;
		s_flLastReport = flNow;
	}

	if ( m_bLoading )
	{
		m_bLoading = false;
		vr::VRCompositor()->FadeToColor( 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, false );
	}
}

//-----------------------------------------------------------------------------
// Input
//-----------------------------------------------------------------------------
static const char *s_pszDigitalActions[VRACTION_DIGITAL_COUNT] =
{
	"/actions/portal/in/FirePortal1",
	"/actions/portal/in/FirePortal2",
	"/actions/portal/in/GunGrab",
	"/actions/portal/in/HandGrab",
	"/actions/portal/in/Use",
	"/actions/portal/in/Jump",
	"/actions/portal/in/Crouch",
	"/actions/portal/in/Recenter",
	"/actions/portal/in/Menu",
	"/actions/portal/in/ToggleHUD",
	"/actions/portal/in/QuickSave",
	"/actions/portal/in/QuickLoad",
};

void CPortalVR::InitInput()
{
	char szFolder[MAX_PATH];
	VRD3D_GetClientDllFolder( szFolder, sizeof( szFolder ) );	// <mod>\bin
	char szManifest[MAX_PATH];
	Q_snprintf( szManifest, sizeof( szManifest ), "%s\\..\\actions\\action_manifest.json", szFolder );
	Q_FixSlashes( szManifest );

	vr::IVRInput *pInput = vr::VRInput();
	vr::EVRInputError eError = pInput->SetActionManifestPath( szManifest );
	if ( eError != vr::VRInputError_None )
	{
		VRLog( "SetActionManifestPath(%s) failed: %d", szManifest, (int)eError );
		return;
	}

	pInput->GetActionSetHandle( "/actions/portal", &m_hActionSet );
	for ( int i = 0; i < VRACTION_DIGITAL_COUNT; i++ )
		pInput->GetActionHandle( s_pszDigitalActions[i], &m_hDigital[i] );
	pInput->GetActionHandle( "/actions/portal/in/Move", &m_hMove );
	pInput->GetActionHandle( "/actions/portal/in/Turn", &m_hTurn );
	pInput->GetActionHandle( "/actions/portal/in/GripSqueeze", &m_hGripSqueeze );
	pInput->GetActionHandle( "/actions/portal/in/PoseLeft", &m_hPose[VR_HAND_LEFT] );
	pInput->GetActionHandle( "/actions/portal/in/PoseRight", &m_hPose[VR_HAND_RIGHT] );
	pInput->GetActionHandle( "/actions/portal/in/SkeletonLeft", &m_hSkeleton[VR_HAND_LEFT] );
	pInput->GetActionHandle( "/actions/portal/in/SkeletonRight", &m_hSkeleton[VR_HAND_RIGHT] );
	pInput->GetActionHandle( "/actions/portal/out/Haptic", &m_hHaptic );
	pInput->GetInputSourceHandle( "/user/hand/left", &m_hHandSource[VR_HAND_LEFT] );
	pInput->GetInputSourceHandle( "/user/hand/right", &m_hHandSource[VR_HAND_RIGHT] );

	m_bInputReady = true;
	VRLog( "SteamVR Input ready (%s)", szManifest );
}

void CPortalVR::UpdateInput()
{
	if ( !m_bInputReady || m_nLastInputFrame == gpGlobals->framecount )
		return;
	m_nLastInputFrame = gpGlobals->framecount;

	vr::VRActiveActionSet_t activeSet = {};
	activeSet.ulActionSet = m_hActionSet;
	if ( vr::VRInput()->UpdateActionState( &activeSet, sizeof( activeSet ), 1 ) != vr::VRInputError_None )
		return;

	for ( int a = 0; a < VRACTION_DIGITAL_COUNT; a++ )
	{
		for ( int h = 0; h < VR_HAND_COUNT; h++ )
		{
			vr::InputDigitalActionData_t data;
			VRDigitalState_t &state = m_Digital[a][h];
			if ( vr::VRInput()->GetDigitalActionData( m_hDigital[a], &data, sizeof( data ), m_hHandSource[h] ) == vr::VRInputError_None && data.bActive )
			{
				state.bDown = data.bState;
				state.bPressed = data.bState && data.bChanged;
				state.bReleased = !data.bState && data.bChanged;
			}
			else
			{
				state.bReleased = state.bDown;
				state.bDown = false;
				state.bPressed = false;
			}
		}
	}

	vr::InputAnalogActionData_t analog;
	m_vecMoveStick.Init();
	if ( vr::VRInput()->GetAnalogActionData( m_hMove, &analog, sizeof( analog ), vr::k_ulInvalidInputValueHandle ) == vr::VRInputError_None && analog.bActive )
		m_vecMoveStick.Init( analog.x, analog.y );
	m_vecTurnStick.Init();
	if ( vr::VRInput()->GetAnalogActionData( m_hTurn, &analog, sizeof( analog ), vr::k_ulInvalidInputValueHandle ) == vr::VRInputError_None && analog.bActive )
		m_vecTurnStick.Init( analog.x, analog.y );

	for ( int h = 0; h < VR_HAND_COUNT; h++ )
	{
		m_flGripSqueeze[h] = 0.0f;
		if ( vr::VRInput()->GetAnalogActionData( m_hGripSqueeze, &analog, sizeof( analog ), m_hHandSource[h] ) == vr::VRInputError_None && analog.bActive )
			m_flGripSqueeze[h] = analog.x;

		vr::VRSkeletalSummaryData_t summary;
		if ( vr::VRInput()->GetSkeletalSummaryData( m_hSkeleton[h], vr::VRSummaryType_FromDevice, &summary ) == vr::VRInputError_None )
		{
			for ( int f = 0; f < 5; f++ )
				m_flFingerCurl[h][f] = summary.flFingerCurl[f];
		}
		else
		{
			// No skeletal data: approximate a fist from the grip.
			for ( int f = 0; f < 5; f++ )
				m_flFingerCurl[h][f] = m_flGripSqueeze[h];
		}
	}
}

VRDigitalState_t CPortalVR::GetDigitalAny( VRAction_t action ) const
{
	VRDigitalState_t result;
	const VRDigitalState_t &l = m_Digital[action][VR_HAND_LEFT];
	const VRDigitalState_t &r = m_Digital[action][VR_HAND_RIGHT];
	result.bDown = l.bDown || r.bDown;
	result.bPressed = l.bPressed || r.bPressed;
	result.bReleased = ( l.bReleased || r.bReleased ) && !result.bDown;
	return result;
}

void CPortalVR::TriggerHaptic( int hand, float flDuration, float flFrequency, float flAmplitude )
{
	if ( !m_bInputReady || hand < 0 || hand >= VR_HAND_COUNT )
		return;
	vr::VRInput()->TriggerHapticVibrationAction( m_hHaptic, 0.0f, flDuration, flFrequency, flAmplitude, m_hHandSource[hand] );
}

void CPortalVR::PrintStatus()
{
	Msg( "VR active: %d  loading: %d  eye size: %dx%d  units/m: %.2f\n", m_bActive, m_bLoading, m_nEyeWidth, m_nEyeHeight, UnitsPerMeter() );
	Msg( "D3D: %s\n", VRD3D_GetStatus() );
	if ( g_pMaterialSystemHardwareConfig )
	{
		int nFBW, nFBH;
		materials->GetBackBufferDimensions( nFBW, nFBH );
		ITexture *pFullFrame = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET, false );
		Msg( "HDR type %d, backbuffer %dx%d, _rt_FullFrameFB %dx%d\n", g_pMaterialSystemHardwareConfig->GetHDRType(), nFBW, nFBH,
			pFullFrame ? pFullFrame->GetActualWidth() : -1, pFullFrame ? pFullFrame->GetActualHeight() : -1 );
	}
	Msg( "HMD valid %d  pos %.1f %.1f %.1f\n", m_HmdPose.bValid, m_HmdPose.mat[0][3], m_HmdPose.mat[1][3], m_HmdPose.mat[2][3] );
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
		Msg( "Hand %d valid %d  pos %.1f %.1f %.1f\n", i, m_HandPose[i].bValid, m_HandPose[i].mat[0][3], m_HandPose[i].mat[1][3], m_HandPose[i].mat[2][3] );
}

CON_COMMAND( vr_dump_eyes, "Save the last frame sent to the headset as vr_eyes.bmp in the mod folder" )
{
	char szFolder[MAX_PATH];
	VRD3D_GetClientDllFolder( szFolder, sizeof( szFolder ) );
	char szPath[MAX_PATH];
	Q_snprintf( szPath, sizeof( szPath ), "%s\\..\\vr_eyes.bmp", szFolder );
	char szError[256];
	if ( VRD3D_DumpSubmitTexture( szPath, szError, sizeof( szError ) ) )
		VRLog( "Wrote %s", szPath );
	else
		VRLog( "vr_dump_eyes failed: %s", szError );
}

CON_COMMAND( vr_status, "Print Portal VR runtime status" )
{
	g_PortalVR.PrintStatus();
}
