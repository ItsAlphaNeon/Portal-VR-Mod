//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The VR "brain" of the client (see client_virtualreality.h).
//
//			Portal VR: rewritten for 6DOF roomscale with OpenVR.
//
//			Spaces:
//			  tracking space - OpenVR standing universe in Source axes and units
//			                   (CPortalVR converts), floor at z = 0.
//			  world          - P + Rz(trackingYaw) * (x - trackingCenter), where P is
//			                   the player's abs origin (center of the feet).
//
//			The client owns the play space placement (trackingYaw / trackingCenter).
//			Physical walking moves trackingCenter toward the head and sends the same
//			displacement in the user command, which game movement applies with
//			collision; if the hull is blocked the view is pushed back with it.
//
//=============================================================================//
#include "cbase.h"

#include "client_virtualreality.h"

#include "materialsystem/itexture.h"
#include "materialsystem/materialsystem_config.h"
#include "view_shared.h"
#include "view.h"
#include "iviewrender.h"
#include "iclientmode.h"
#include "input.h"
#include "in_buttons.h"
#include "usercmd.h"
#include "c_baseplayer.h"
#include "prediction.h"
#include "KeyValues.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Controls.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "sourcevr/isourcevirtualreality.h"
#include "vr/vr_openvr.h"
#include "tier0/vprof.h"
#include "ienginevgui.h"
#include "vgui/IInputInternal.h"

extern vgui::IInputInternal *g_InputInternal;

#ifdef PORTAL
#include "c_portal_player.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CClientVirtualReality g_ClientVirtualReality;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CClientVirtualReality, IClientVirtualReality,
	CLIENTVIRTUALREALITY_INTERFACE_VERSION, g_ClientVirtualReality );

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------

// Kept from the SDK: other client code looks these up by name.
ConVar vr_first_person_uses_world_model( "vr_first_person_uses_world_model", "0", 0, "Draw the third person model in first person in VR." );
ConVar vr_render_hud_in_world( "vr_render_hud_in_world", "1" );

// Hands and gun
ConVar vr_gun_hand( "vr_gun_hand", "1", FCVAR_ARCHIVE, "Hand that holds the portal gun: 0 = left, 1 = right." );
// The gun's handle ("ValveBiped.Base" bone) is placed at the grip, plus this offset in grip space.
ConVar vr_gun_offset_x( "vr_gun_offset_x", "0", FCVAR_ARCHIVE, "Portal gun handle offset from the controller grip, forward (units)." );
ConVar vr_gun_offset_y( "vr_gun_offset_y", "0", FCVAR_ARCHIVE, "Portal gun handle offset from the controller grip, left (units)." );
ConVar vr_gun_offset_z( "vr_gun_offset_z", "0", FCVAR_ARCHIVE, "Portal gun handle offset from the controller grip, up (units)." );
ConVar vr_gun_pitch( "vr_gun_pitch", "0", FCVAR_ARCHIVE, "Extra pitch of the portal gun relative to the controller grip (degrees, + = down)." );
ConVar vr_gun_yaw( "vr_gun_yaw", "0", FCVAR_ARCHIVE, "Extra yaw of the portal gun relative to the controller grip (degrees)." );
ConVar vr_aim_offset_forward( "vr_aim_offset_forward", "25", FCVAR_ARCHIVE, "Distance of the portal gun muzzle in front of the handle (units)." );

// v_portalgun.mdl geometry (model space, idle pose; measured with vr_gun_debug):
// the handle bone and the barrel direction from the handle toward the muzzle.
static const Vector s_vecGunHandleInModel( 7.04f, -8.35f, -11.35f );
static const QAngle s_angGunBarrelInModel( 5.3f, -6.7f, 0.0f );

// Locomotion
ConVar vr_move_hand_relative( "vr_move_hand_relative", "0", FCVAR_ARCHIVE, "0 = stick moves toward where you look, 1 = toward where your free hand points." );
ConVar vr_move_deadzone( "vr_move_deadzone", "0.15", FCVAR_ARCHIVE );
ConVar vr_turn_mode( "vr_turn_mode", "0", FCVAR_ARCHIVE, "0 = snap turn, 1 = smooth turn." );
ConVar vr_snap_turn_angle( "vr_snap_turn_angle", "45", FCVAR_ARCHIVE );
ConVar vr_smooth_turn_speed( "vr_smooth_turn_speed", "180", FCVAR_ARCHIVE, "Smooth turn speed (degrees per second)." );
ConVar vr_roomscale_deadzone( "vr_roomscale_deadzone", "4", FCVAR_ARCHIVE, "How far (units) the head can move from the body before the body follows. Lets you lean." );
ConVar vr_crouch_height( "vr_crouch_height", "44", FCVAR_ARCHIVE, "Head height (units) below which you physically crouch." );

// View
ConVar vr_znear( "vr_znear", "2", FCVAR_ARCHIVE, "Near clip plane in VR (units)." );

// HUD panel
ConVar vr_hud_distance( "vr_hud_distance", "70", FCVAR_ARCHIVE, "Distance of the HUD panel (units)." );
ConVar vr_hud_width( "vr_hud_width", "64", FCVAR_ARCHIVE, "Width of the HUD panel (units)." );
ConVar vr_hud_pitch( "vr_hud_pitch", "8", FCVAR_ARCHIVE, "How far below eye level the HUD panel sits (degrees)." );
ConVar vr_hud_follow_angle( "vr_hud_follow_angle", "30", FCVAR_ARCHIVE, "The HUD panel re-centers when you look this far (degrees) away from it." );
ConVar vr_hud_visible( "vr_hud_visible", "1", FCVAR_ARCHIVE );
ConVar vr_menu_distance( "vr_menu_distance", "60", FCVAR_ARCHIVE, "Distance of the menu panel (units)." );
ConVar vr_menu_width( "vr_menu_width", "80", FCVAR_ARCHIVE, "Width of the menu panel (units)." );

// Mirror
ConVar vr_mirror( "vr_mirror", "1", FCVAR_ARCHIVE, "Show the left eye in the desktop window." );

CON_COMMAND( vr_recenter, "Put your body back under your head" )
{
	g_ClientVirtualReality.Recenter();
}

//-----------------------------------------------------------------------------
// construction/destruction
//-----------------------------------------------------------------------------
CClientVirtualReality::CClientVirtualReality()
{
	m_flTrackingYaw = 0.0f;
	m_vecTrackingCenter.Init();
	m_bTrackingInitialized = false;
	m_WorldFromMidEye.Identity();
	m_WorldFromHud.Identity();
	m_HudProjectionFromWorld.Identity();
	m_vecHudViewer.Init();
	m_fHudHalfWidth = 32.0f;
	m_fHudHalfHeight = 18.0f;
	m_flHudYaw = 0.0f;
	m_vecHeadOffset.Init( 0, 0, 64 );
	m_angHead.Init();
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		SetIdentityMatrix( m_WorldFromHand[i] );
		m_bHandValid[i] = false;
	}
	m_pHudMaterial = NULL;
	m_pHudMaterialOpaque = NULL;
	m_pMirrorMaterial = NULL;
	m_pLaserMaterial = NULL;
	m_bMenuOpen = false;
	m_bPointerHit = false;
	m_vecPointerStart.Init();
	m_vecPointerEnd.Init();
	m_bPointerButtonDown = false;
	m_bCrouchToggled = false;
	m_bSnapTurnReady = true;
	m_bUseLatched = false;
}

CClientVirtualReality::~CClientVirtualReality()
{
}

bool CClientVirtualReality::Connect( CreateInterfaceFn factory )
{
	if ( !factory )
		return false;
	return BaseClass::Connect( factory );
}

void CClientVirtualReality::Disconnect()
{
	BaseClass::Disconnect();
}

void *CClientVirtualReality::QueryInterface( const char *pInterfaceName )
{
	CreateInterfaceFn factory = Sys_GetFactoryThis();	// This silly construction is necessary
	return factory( pInterfaceName, NULL );				// to prevent the LTCG compiler from crashing.
}

InitReturnVal_t CClientVirtualReality::Init()
{
	return BaseClass::Init();
}

void CClientVirtualReality::Shutdown()
{
	g_PortalVR.Shutdown();
	BaseClass::Shutdown();
}

void CClientVirtualReality::StartupComplete()
{
	if ( UseVR() )
	{
		// Our view setup handles the HMD; the engine must not draw a mouse-driven view on top.
		vgui::surface()->SetSoftwareCursor( true );
	}
}

void CClientVirtualReality::DrawMainMenu()
{
	// The engine only calls this when its own (absent) sourcevr.dll is active, so it never
	// happens. Portal's menu runs on a background map, which goes through the normal view.
}

int CClientVirtualReality::GetGunHand() const
{
	return vr_gun_hand.GetInt() == 0 ? VR_HAND_LEFT : VR_HAND_RIGHT;
}

//-----------------------------------------------------------------------------
// Tracking space -> world
//-----------------------------------------------------------------------------
void CClientVirtualReality::TrackingToWorld( const matrix3x4_t &trk, const Vector &vecPlayerOrigin, matrix3x4_t &world ) const
{
	matrix3x4_t playspace;
	AngleMatrix( QAngle( 0, m_flTrackingYaw, 0 ), playspace );
	// world = P + R * ( x - c ): the tracking origin sits at P + R * ( -c )
	Vector vecOrigin = vecPlayerOrigin + TrackingOffsetToWorld( Vector( 0, 0, 0 ) );
	MatrixSetColumn( vecOrigin, 3, playspace );
	ConcatTransforms( playspace, trk, world );
}

Vector CClientVirtualReality::TrackingOffsetToWorld( const Vector &trk ) const
{
	Vector vecLocal( trk.x - m_vecTrackingCenter.x, trk.y - m_vecTrackingCenter.y, trk.z );
	matrix3x4_t yaw;
	AngleMatrix( QAngle( 0, m_flTrackingYaw, 0 ), yaw );
	Vector vecWorld;
	VectorRotate( vecLocal, yaw, vecWorld );
	return vecWorld;
}

void CClientVirtualReality::InitTracking( C_BasePlayer *pPlayer )
{
	const VRTrackedPose_t &hmd = g_PortalVR.GetHmdPose();
	if ( !hmd.bValid || !pPlayer )
		return;

	// Put the hull under the head, and face the way the player is facing in the game.
	QAngle angHmd;
	MatrixAngles( hmd.mat, angHmd );
	m_flTrackingYaw = AngleNormalize( pPlayer->EyeAngles()[YAW] - angHmd[YAW] );
	m_vecTrackingCenter.Init( hmd.mat[0][3], hmd.mat[1][3], 0.0f );
	m_bTrackingInitialized = true;
	VRLog( "Tracking initialized: yaw %.1f center %.1f %.1f", m_flTrackingYaw, m_vecTrackingCenter.x, m_vecTrackingCenter.y );
}

void CClientVirtualReality::Recenter()
{
	const VRTrackedPose_t &hmd = g_PortalVR.GetHmdPose();
	if ( hmd.bValid )
		m_vecTrackingCenter.Init( hmd.mat[0][3], hmd.mat[1][3], 0.0f );
}

void CClientVirtualReality::ApplyTurn( float flDegrees )
{
	// Rotate the play space around the head, not around the tracking origin.
	const VRTrackedPose_t &hmd = g_PortalVR.GetHmdPose();
	Vector vecHead( hmd.mat[0][3], hmd.mat[1][3], 0.0f );
	Vector vecFromCenter = vecHead - m_vecTrackingCenter;

	// world offset of the head must stay the same: R(yaw + d) * (h - c') = R(yaw) * (h - c)
	matrix3x4_t rot;
	AngleMatrix( QAngle( 0, -flDegrees, 0 ), rot );
	Vector vecRotated;
	VectorRotate( vecFromCenter, rot, vecRotated );
	m_vecTrackingCenter = vecHead - vecRotated;
	m_vecTrackingCenter.z = 0.0f;

	m_flTrackingYaw = AngleNormalize( m_flTrackingYaw + flDegrees );
}

void CClientVirtualReality::OnLocalPlayerPortalled( const VMatrix &matPortalTransform )
{
	if ( !UseVR() || !m_bTrackingInitialized )
		return;

	// The flat game rotates the whole view by the portal transform and then rolls it back
	// upright. VR keeps the horizon level, so only yaw is applied: choose the yaw that
	// makes the direction you were looking come out of the exit portal.
	Vector vecLook;
	AngleVectors( m_angHead, &vecLook );
	Vector vecOut = matPortalTransform.ApplyRotation( vecLook );
	if ( vecOut.Length2D() < 0.2f )
	{
		// Looking straight along the exit normal (e.g. down into a floor portal that comes
		// out of a ceiling): use the transformed flat facing instead.
		Vector vecFlat( vecLook.x, vecLook.y, 0.0f );
		if ( vecFlat.Length2D() < 0.01f )
			vecFlat.Init( 1, 0, 0 );
		vecOut = matPortalTransform.ApplyRotation( vecFlat );
		if ( vecOut.Length2D() < 0.01f )
			return;
	}

	float flNewYaw = RAD2DEG( atan2f( vecOut.y, vecOut.x ) );
	float flDelta = AngleNormalize( flNewYaw - m_angHead[YAW] );
	ApplyTurn( flDelta );

	g_PortalVR.TriggerHaptic( VR_HAND_LEFT, 0.05f, 60.0f, 0.4f );
	g_PortalVR.TriggerHaptic( VR_HAND_RIGHT, 0.05f, 60.0f, 0.4f );
}

void CClientVirtualReality::UpdateWorldPoses( C_BasePlayer *pPlayer )
{
	const VRTrackedPose_t &hmd = g_PortalVR.GetHmdPose();
	const Vector vecOrigin = pPlayer->GetAbsOrigin();

	matrix3x4_t worldHead;
	TrackingToWorld( hmd.mat, vecOrigin, worldHead );
	m_WorldFromMidEye = VMatrix( worldHead );
	MatrixAngles( worldHead, m_angHead );
	m_vecHeadOffset = TrackingOffsetToWorld( Vector( hmd.mat[0][3], hmd.mat[1][3], hmd.mat[2][3] ) );

	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		const VRTrackedPose_t &hand = g_PortalVR.GetHandPose( i );
		m_bHandValid[i] = hand.bValid;
		TrackingToWorld( hand.mat, vecOrigin, m_WorldFromHand[i] );
	}

#ifdef PORTAL
	C_Portal_Player *pPortalPlayer = ToPortalPlayer( pPlayer );
	if ( pPortalPlayer )
		pPortalPlayer->SetVRHeadPose( m_vecHeadOffset, m_angHead, hmd.bValid );
#endif
}

bool CClientVirtualReality::GetHandWorldPose( int hand, Vector &origin, QAngle &angles ) const
{
	MatrixGetColumn( m_WorldFromHand[hand], 3, origin );
	MatrixAngles( m_WorldFromHand[hand], angles );
	return m_bHandValid[hand];
}

// World transform of the gun barrel: origin at the handle, x axis along the barrel.
static void GetGunBarrelTransform( const matrix3x4_t &worldFromGrip, matrix3x4_t &worldFromBarrel )
{
	matrix3x4_t gripFromBarrel;
	AngleMatrix( QAngle( vr_gun_pitch.GetFloat(), vr_gun_yaw.GetFloat(), 0 ),
		Vector( vr_gun_offset_x.GetFloat(), vr_gun_offset_y.GetFloat(), vr_gun_offset_z.GetFloat() ), gripFromBarrel );
	ConcatTransforms( worldFromGrip, gripFromBarrel, worldFromBarrel );
}

bool CClientVirtualReality::GetGunAim( Vector &origin, Vector &direction ) const
{
	const int hand = GetGunHand();
	matrix3x4_t barrel;
	GetGunBarrelTransform( m_WorldFromHand[hand], barrel );

	MatrixGetColumn( barrel, 0, direction );
	Vector vecHandle;
	MatrixGetColumn( barrel, 3, vecHandle );
	origin = vecHandle + direction * vr_aim_offset_forward.GetFloat();
	return m_bHandValid[hand];
}

//-----------------------------------------------------------------------------
// Per frame: called from CViewRender::SetUpViews before the player's CalcView
//-----------------------------------------------------------------------------
bool CClientVirtualReality::ProcessCurrentTrackingState( float fGameFOV )
{
	if ( !UseVR() )
		return false;

	VPROF_BUDGET( "CClientVirtualReality::ProcessCurrentTrackingState", "VR" );

	g_PortalVR.BeginFrame();

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return false;

	if ( !m_bTrackingInitialized )
		InitTracking( pPlayer );

	UpdateWorldPoses( pPlayer );
	UpdateMenu();

	// The player's eye angles are the HMD: CalcView, EyeAngles(), sound, and the portal
	// eye-through-portal logic all read them.
	if ( pPlayer->IsAlive() && !pPlayer->GetVehicle() )
	{
		engine->SetViewAngles( m_angHead );
		prediction->SetLocalViewAngles( m_angHead );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Views
//-----------------------------------------------------------------------------
bool CClientVirtualReality::OverrideView( CViewSetup *pViewMiddle, Vector *pViewModelOrigin, QAngle *pViewModelAngles, HeadtrackMovementMode_t hmmMovementOverride )
{
	if ( !UseVR() )
		return false;

	// The middle view already is the head (the player's CalcView reads the HMD pose,
	// including when the head is leaning through a portal).
	pViewMiddle->zNear = vr_znear.GetFloat();
	pViewMiddle->zNearViewmodel = vr_znear.GetFloat();
	m_WorldFromMidEye.SetupMatrixOrgAngles( pViewMiddle->origin, pViewMiddle->angles );
	return true;
}

static float FovFromProjection( const VMatrix &proj )
{
	float xoffset = proj.m[0][2], xscale = proj.m[0][0];
	float yoffset = proj.m[1][2], yscale = proj.m[1][1];
	float fov_px = 2.0f * RAD2DEG( atanf( fabsf( (  1.0f - xoffset ) / xscale ) ) );
	float fov_nx = 2.0f * RAD2DEG( atanf( fabsf( ( -1.0f - xoffset ) / xscale ) ) );
	float fov_py = 2.0f * RAD2DEG( atanf( fabsf( (  1.0f - yoffset ) / yscale ) ) );
	float fov_ny = 2.0f * RAD2DEG( atanf( fabsf( ( -1.0f - yoffset ) / yscale ) ) );
	return MAX( MAX( fov_px, fov_nx ), MAX( fov_py, fov_ny ) );
}

bool CClientVirtualReality::OverrideStereoView( CViewSetup *pViewMiddle, CViewSetup *pViewLeft, CViewSetup *pViewRight )
{
	if ( !UseVR() )
		return false;

	matrix3x4_t worldFromMid;
	AngleMatrix( pViewMiddle->angles, pViewMiddle->origin, worldFromMid );

	CViewSetup *pViews[2] = { pViewLeft, pViewRight };
	for ( int eye = 0; eye < 2; eye++ )
	{
		CViewSetup *pView = pViews[eye];
		ISourceVirtualReality::VREye eEye = eye == 0 ? ISourceVirtualReality::VREye_Left : ISourceVirtualReality::VREye_Right;

		matrix3x4_t worldFromEye;
		ConcatTransforms( worldFromMid, g_PortalVR.GetHeadFromEye( eEye ), worldFromEye );
		MatrixGetColumn( worldFromEye, 3, pView->origin );
		MatrixAngles( worldFromEye, pView->angles );

		pView->m_eStereoEye = eye == 0 ? STEREO_EYE_LEFT : STEREO_EYE_RIGHT;
		pView->m_bViewToProjectionOverride = true;
		g_PortalVR.GetEyeProjectionMatrix( &pView->m_ViewToProjection, eEye, pViewMiddle->zNear, pViewMiddle->zFar, 1.0f );
		pView->fov = FovFromProjection( pView->m_ViewToProjection );
		pView->fovViewmodel = pView->fov;
		pView->m_flAspectRatio = (float)g_PortalVR.GetEyeWidth() / (float)g_PortalVR.GetEyeHeight();
	}

	// HUD panel: body-locked in yaw, re-centers lazily when you look away from it.
	// Menus stay where they opened, at eye level.
	m_vecHudViewer = pViewMiddle->origin;
	if ( !m_bMenuOpen )
	{
		float flYawDelta = AngleDiff( pViewMiddle->angles[YAW], m_flHudYaw );
		if ( fabsf( flYawDelta ) > vr_hud_follow_angle.GetFloat() )
			m_flHudYaw = AngleNormalize( m_flHudYaw + flYawDelta - ( flYawDelta > 0.0f ? 1.0f : -1.0f ) * vr_hud_follow_angle.GetFloat() * 0.5f );
	}

	QAngle angHud( m_bMenuOpen ? 0.0f : vr_hud_pitch.GetFloat(), m_flHudYaw, 0.0f );
	m_WorldFromHud.SetupMatrixOrgAngles( vec3_origin, angHud );

	int nScreenWide, nScreenTall;
	vgui::surface()->GetScreenSize( nScreenWide, nScreenTall );
	m_fHudHalfWidth = ( m_bMenuOpen ? vr_menu_width.GetFloat() : vr_hud_width.GetFloat() ) * 0.5f;
	m_fHudHalfHeight = m_fHudHalfWidth * (float)nScreenTall / (float)MAX( 1, nScreenWide );

	// Projection used by HudTransform() to put world points onto the HUD panel.
	VMatrix matHudView, matHudProj;
	matrix3x4_t worldFromHud;
	AngleMatrix( angHud, m_vecHudViewer, worldFromHud );
	VMatrix viewFromWorld;
	MatrixInverseGeneral( VMatrix( worldFromHud ), viewFromWorld );
	float flHudFov = 2.0f * RAD2DEG( atanf( m_fHudHalfWidth / GetHUDDistance() ) );
	MatrixBuildPerspectiveX( matHudProj, flHudFov, m_fHudHalfWidth / m_fHudHalfHeight, 1.0f, 10000.0f );
	// Source view space: x forward, y left, z up -> projection space x right, y up, -z forward
	VMatrix matViewRotate( 0, -1, 0, 0,
						   0, 0, 1, 0,
						   -1, 0, 0, 0,
						   0, 0, 0, 1 );
	m_HudProjectionFromWorld = matHudProj * matViewRotate * viewFromWorld;

	return true;
}

void CClientVirtualReality::PostProcessFrame( StereoEye_t eEye )
{
	if ( !UseVR() )
		return;

	if ( eEye == STEREO_EYE_RIGHT )
		g_PortalVR.SubmitFrame();
}

void CClientVirtualReality::CreateMaterials()
{
	if ( m_pHudMaterial )
		return;

	KeyValues *pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetString( "$basetexture", "_rt_gui" );
	pKV->SetInt( "$translucent", 1 );
	pKV->SetInt( "$ignorez", 1 );
	pKV->SetInt( "$nocull", 1 );
	pKV->SetInt( "$nofog", 1 );
	m_pHudMaterial = materials->CreateMaterial( "__vr_hud", pKV );
	m_pHudMaterial->IncrementReferenceCount();

	pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetString( "$basetexture", "_rt_gui" );
	pKV->SetInt( "$ignorez", 1 );
	pKV->SetInt( "$nocull", 1 );
	pKV->SetInt( "$nofog", 1 );
	m_pHudMaterialOpaque = materials->CreateMaterial( "__vr_hud_opaque", pKV );
	m_pHudMaterialOpaque->IncrementReferenceCount();

	pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetString( "$basetexture", "_rt_vr_eyes" );
	pKV->SetInt( "$ignorez", 1 );
	pKV->SetInt( "$nofog", 1 );
	m_pMirrorMaterial = materials->CreateMaterial( "__vr_mirror", pKV );
	m_pMirrorMaterial->IncrementReferenceCount();

	pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetString( "$basetexture", "white" );
	pKV->SetInt( "$vertexcolor", 1 );
	pKV->SetInt( "$vertexalpha", 1 );
	pKV->SetInt( "$translucent", 1 );
	pKV->SetInt( "$ignorez", 1 );
	pKV->SetInt( "$nocull", 1 );
	pKV->SetInt( "$nofog", 1 );
	m_pLaserMaterial = materials->CreateMaterial( "__vr_laser", pKV );
	m_pLaserMaterial->IncrementReferenceCount();
}

void CClientVirtualReality::DrawMirror( int nWidth, int nHeight )
{
	if ( !UseVR() || !vr_mirror.GetBool() )
		return;

	CreateMaterials();
	ITexture *pEyes = g_PortalVR.GetEyeTexture();
	if ( !pEyes || !m_pMirrorMaterial )
		return;

	// Center crop of the left eye with the window's aspect ratio.
	int nEyeW = g_PortalVR.GetEyeWidth();
	int nEyeH = g_PortalVR.GetEyeHeight();
	float flWindowAspect = (float)nWidth / (float)MAX( 1, nHeight );
	int nSrcW = nEyeW, nSrcH = (int)( nEyeW / flWindowAspect );
	if ( nSrcH > nEyeH )
	{
		nSrcH = nEyeH;
		nSrcW = (int)( nEyeH * flWindowAspect );
	}
	int nSrcX = ( nEyeW - nSrcW ) / 2;
	int nSrcY = ( nEyeH - nSrcH ) / 2;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->DrawScreenSpaceRectangle( m_pMirrorMaterial, 0, 0, nWidth, nHeight,
		nSrcX, nSrcY, nSrcX + nSrcW - 1, nSrcY + nSrcH - 1, pEyes->GetActualWidth(), pEyes->GetActualHeight() );
}

void CClientVirtualReality::LevelShutdown()
{
	g_PortalVR.SetLoading( true );
	m_bTrackingInitialized = false;	// re-align with the player's facing in the next map
}

//-----------------------------------------------------------------------------
// HUD panel
//-----------------------------------------------------------------------------
float CClientVirtualReality::GetHUDDistance()
{
	return m_bMenuOpen ? vr_menu_distance.GetFloat() : vr_hud_distance.GetFloat();
}

bool CClientVirtualReality::ShouldRenderHUDInWorld()
{
	return UseVR() && vr_render_hud_in_world.GetBool();
}

void CClientVirtualReality::GetHUDBounds( Vector *pViewer, Vector *pUL, Vector *pUR, Vector *pLL, Vector *pLR )
{
	Vector vHalfWidth = m_WorldFromHud.GetLeft() * -m_fHudHalfWidth;
	Vector vHalfHeight = m_WorldFromHud.GetUp() * m_fHudHalfHeight;
	Vector vHUDOrigin = m_vecHudViewer + m_WorldFromHud.GetForward() * GetHUDDistance();

	*pViewer = m_vecHudViewer;
	*pUL = vHUDOrigin - vHalfWidth + vHalfHeight;
	*pUR = vHUDOrigin + vHalfWidth + vHalfHeight;
	*pLL = vHUDOrigin - vHalfWidth - vHalfHeight;
	*pLR = vHUDOrigin + vHalfWidth - vHalfHeight;
}

void CClientVirtualReality::RenderHUDQuad( bool bBlackout, bool bTranslucent )
{
	bool bMenuOpen = g_pMatSystemSurface && g_pMatSystemSurface->IsCursorVisible();
	if ( !vr_hud_visible.GetBool() && !bMenuOpen )
		return;

	CreateMaterials();

	Vector vHead, vUL, vUR, vLL, vLR;
	GetHUDBounds( &vHead, &vUL, &vUR, &vLL, &vLR );

	CMatRenderContextPtr pRenderContext( materials );
	IMaterial *pMaterial = bMenuOpen ? m_pHudMaterialOpaque : m_pHudMaterial;
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMaterial );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLE_STRIP, 2 );

	meshBuilder.Position3fv( vLR.Base() );
	meshBuilder.TexCoord2f( 0, 1, 1 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 1>();

	meshBuilder.Position3fv( vLL.Base() );
	meshBuilder.TexCoord2f( 0, 0, 1 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 1>();

	meshBuilder.Position3fv( vUR.Base() );
	meshBuilder.TexCoord2f( 0, 1, 0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 1>();

	meshBuilder.Position3fv( vUL.Base() );
	meshBuilder.TexCoord2f( 0, 0, 0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 1>();

	meshBuilder.End();
	pMesh->Draw();

	if ( bMenuOpen )
		DrawLaser();
}

void CClientVirtualReality::DrawLaser()
{
	if ( !m_pLaserMaterial )
		return;

	Vector vecEnd = m_vecPointerEnd;
	unsigned char r = m_bPointerHit ? 255 : 140, g = m_bPointerHit ? 200 : 140, b = m_bPointerHit ? 60 : 140;

	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_pLaserMaterial );
	CMeshBuilder meshBuilder;

	// Beam: a thin quad turned toward the viewer.
	Vector vecDir = vecEnd - m_vecPointerStart;
	Vector vecToEye = m_vecHudViewer - m_vecPointerStart;
	Vector vecSide = CrossProduct( vecDir, vecToEye );
	VectorNormalize( vecSide );
	vecSide *= 0.15f;

	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );
	meshBuilder.Position3fv( ( m_vecPointerStart - vecSide ).Base() ); meshBuilder.Color4ub( r, g, b, 200 ); meshBuilder.TexCoord2f( 0, 0, 0 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( m_vecPointerStart + vecSide ).Base() ); meshBuilder.Color4ub( r, g, b, 200 ); meshBuilder.TexCoord2f( 0, 1, 0 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( vecEnd + vecSide ).Base() ); meshBuilder.Color4ub( r, g, b, 80 ); meshBuilder.TexCoord2f( 0, 1, 1 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( vecEnd - vecSide ).Base() ); meshBuilder.Color4ub( r, g, b, 80 ); meshBuilder.TexCoord2f( 0, 0, 1 ); meshBuilder.AdvanceVertex();
	meshBuilder.End();
	pMesh->Draw();
}

//-----------------------------------------------------------------------------
// Menus: the menu button opens/closes the pause menu; the gun hand is a laser
// pointer on the menu panel and its trigger clicks.
//-----------------------------------------------------------------------------
void CClientVirtualReality::UpdateMenu()
{
	if ( g_PortalVR.GetDigitalAny( VRACTION_MENU ).bPressed )
		engine->ClientCmd_Unrestricted( enginevgui->IsGameUIVisible() ? "gameui_hide" : "gameui_activate" );

	bool bMenuOpen = enginevgui->IsGameUIVisible() || ( g_pMatSystemSurface && g_pMatSystemSurface->IsCursorVisible() );
	if ( bMenuOpen && !m_bMenuOpen )
		m_flHudYaw = m_angHead[YAW];	// open the panel straight ahead
	m_bMenuOpen = bMenuOpen;
	m_bPointerHit = false;
	if ( !bMenuOpen )
	{
		if ( m_bPointerButtonDown && g_InputInternal )
			g_InputInternal->InternalMouseReleased( MOUSE_LEFT );
		m_bPointerButtonDown = false;
		return;
	}

	const int hand = GetGunHand();
	matrix3x4_t barrel;
	GetGunBarrelTransform( m_WorldFromHand[hand], barrel );
	Vector vecDir, vecStart;
	MatrixGetColumn( barrel, 0, vecDir );
	MatrixGetColumn( barrel, 3, vecStart );
	m_vecPointerStart = vecStart;
	m_vecPointerEnd = vecStart + vecDir * 200.0f;

	// Intersect with the panel plane.
	Vector vecPanelCenter = m_vecHudViewer + m_WorldFromHud.GetForward() * GetHUDDistance();
	Vector vecNormal = m_WorldFromHud.GetForward();
	float flDenom = DotProduct( vecDir, vecNormal );
	if ( flDenom > 1e-3f )
	{
		float t = DotProduct( vecPanelCenter - vecStart, vecNormal ) / flDenom;
		if ( t > 0.0f )
		{
			Vector vecHit = vecStart + vecDir * t;
			Vector vecLocal = vecHit - vecPanelCenter;
			float u = DotProduct( vecLocal, -m_WorldFromHud.GetLeft() ) / ( 2.0f * m_fHudHalfWidth ) + 0.5f;
			float v = 0.5f - DotProduct( vecLocal, m_WorldFromHud.GetUp() ) / ( 2.0f * m_fHudHalfHeight );
			if ( u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f )
			{
				m_bPointerHit = true;
				m_vecPointerEnd = vecHit;
				int nScreenWide, nScreenTall;
				vgui::surface()->GetScreenSize( nScreenWide, nScreenTall );
				if ( g_InputInternal )
					g_InputInternal->InternalCursorMoved( (int)( u * nScreenWide ), (int)( v * nScreenTall ) );
			}
		}
	}

	if ( !g_InputInternal )
		return;
	const VRDigitalState_t &trigger = g_PortalVR.GetDigital( VRACTION_FIRE_PORTAL1, hand );
	if ( trigger.bPressed && m_bPointerHit )
	{
		g_InputInternal->SetMouseCodeState( MOUSE_LEFT, vgui::BUTTON_PRESSED );
		g_InputInternal->InternalMousePressed( MOUSE_LEFT );
		m_bPointerButtonDown = true;
		g_PortalVR.TriggerHaptic( hand, 0.02f, 150.0f, 0.3f );
	}
	else if ( !trigger.bDown && m_bPointerButtonDown )
	{
		g_InputInternal->SetMouseCodeState( MOUSE_LEFT, vgui::BUTTON_RELEASED );
		g_InputInternal->InternalMouseReleased( MOUSE_LEFT );
		m_bPointerButtonDown = false;
	}
}

//-----------------------------------------------------------------------------
// Weapon
//-----------------------------------------------------------------------------
void CClientVirtualReality::OverrideViewModelTransform( Vector &vmorigin, QAngle &vmangles, bool bUseLargeOverride )
{
	if ( !UseVR() )
		return;

	// Place the model so its handle bone is at the grip and its barrel points along it:
	// worldFromModel = worldFromBarrel * inverse( modelFromBarrel )
	matrix3x4_t barrel;
	GetGunBarrelTransform( m_WorldFromHand[GetGunHand()], barrel );

	matrix3x4_t modelFromBarrel, barrelFromModel, worldFromModel;
	AngleMatrix( s_angGunBarrelInModel, s_vecGunHandleInModel, modelFromBarrel );
	MatrixInvert( modelFromBarrel, barrelFromModel );
	ConcatTransforms( barrel, barrelFromModel, worldFromModel );

	MatrixGetColumn( worldFromModel, 3, vmorigin );
	MatrixAngles( worldFromModel, vmangles );
}

bool CClientVirtualReality::OverrideWeaponHudAimVectors( Vector *pAimOrigin, Vector *pAimDirection )
{
	if ( !UseVR() )
		return false;
	return GetGunAim( *pAimOrigin, *pAimDirection );
}

void CClientVirtualReality::GetTorsoRelativeAim( Vector *pPosition, QAngle *pAngles )
{
	*pPosition = m_WorldFromMidEye.GetTranslation();
	*pAngles = m_angHead;
}

//-----------------------------------------------------------------------------
// Input
//-----------------------------------------------------------------------------
bool CClientVirtualReality::OverridePlayerMotion( float flInputSampleFrametime, const QAngle &oldAngles, const QAngle &curAngles, const Vector &curMotion, QAngle *pNewAngles, Vector *pNewMotion )
{
	// Superseded by CreateMove().
	*pNewAngles = curAngles;
	*pNewMotion = curMotion;
	return false;
}

void CClientVirtualReality::ExtraMouseSample( float flFrametime, QAngle &viewangles )
{
	if ( !UseVR() || !m_bTrackingInitialized )
		return;
	viewangles = m_angHead;
}

static void ApplyStickDeadzone( Vector2D &v, float flDeadzone )
{
	float flLen = v.Length();
	if ( flLen < flDeadzone )
	{
		v.Init();
		return;
	}
	float flScaled = MIN( 1.0f, ( flLen - flDeadzone ) / ( 1.0f - flDeadzone ) );
	v *= flScaled / flLen;
}

void CClientVirtualReality::CreateMove( float flFrametime, CUserCmd *cmd )
{
	if ( !UseVR() )
		return;

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer || !m_bTrackingInitialized )
		return;

	g_PortalVR.UpdateInput();

	const VRTrackedPose_t &hmd = g_PortalVR.GetHmdPose();
	const int gunHand = GetGunHand();
	const int freeHand = GetFreeHand();
	const bool bAlive = pPlayer->IsAlive();

	//
	// Turning
	//
	Vector2D vecTurn = g_PortalVR.GetTurnStick();
	if ( vr_turn_mode.GetInt() == 0 )
	{
		if ( fabsf( vecTurn.x ) > 0.7f )
		{
			if ( m_bSnapTurnReady && bAlive )
			{
				ApplyTurn( vecTurn.x > 0 ? -vr_snap_turn_angle.GetFloat() : vr_snap_turn_angle.GetFloat() );
				m_bSnapTurnReady = false;
			}
		}
		else if ( fabsf( vecTurn.x ) < 0.3f )
		{
			m_bSnapTurnReady = true;
		}
	}
	else if ( fabsf( vecTurn.x ) > 0.15f && bAlive )
	{
		ApplyTurn( -vecTurn.x * vr_smooth_turn_speed.GetFloat() * flFrametime );
	}

	if ( g_PortalVR.GetDigitalAny( VRACTION_RECENTER ).bPressed )
		Recenter();

	//
	// Roomscale: let the hull follow the head once it is outside the lean deadzone.
	//
	Vector vecRoomscaleWorld( 0, 0, 0 );
	if ( hmd.bValid && bAlive )
	{
		Vector vecHead( hmd.mat[0][3], hmd.mat[1][3], 0.0f );
		Vector vecDelta = vecHead - m_vecTrackingCenter;
		float flDist = vecDelta.Length2D();
		float flDeadzone = vr_roomscale_deadzone.GetFloat();
		if ( flDist > flDeadzone )
		{
			// Move only the part beyond the deadzone, so the body trails the head smoothly.
			Vector vecMove = vecDelta * ( ( flDist - flDeadzone ) / flDist );
			vecRoomscaleWorld = TrackingOffsetToWorld( m_vecTrackingCenter + vecMove ) - TrackingOffsetToWorld( m_vecTrackingCenter );
			vecRoomscaleWorld.z = 0.0f;
			m_vecTrackingCenter += vecMove;
			m_vecTrackingCenter.z = 0.0f;
		}
	}

	// Recompute poses relative to the hull after this command's roomscale step.
	UpdateWorldPoses( pPlayer );

	VRUserCmd_t &vr = cmd->vr;
	vr.Reset();
	vr.flags = VRCMD_ACTIVE;
	if ( hmd.bValid )
		vr.flags |= VRCMD_HMD_VALID;
	if ( gunHand == VR_HAND_LEFT )
		vr.flags |= VRCMD_LEFT_HANDED;
	vr.hmdOffset = m_vecHeadOffset;
	vr.hmdAngles = m_angHead;
	vr.roomscaleMove = vecRoomscaleWorld;

	Vector vecAimOrigin, vecAimDir;
	GetGunAim( vecAimOrigin, vecAimDir );
	vr.aimOffset = vecAimOrigin - pPlayer->GetAbsOrigin();
	VectorAngles( vecAimDir, vr.aimAngles );

	matrix3x4_t yaw;
	AngleMatrix( QAngle( 0, m_flTrackingYaw, 0 ), yaw );
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		const VRTrackedPose_t &hand = g_PortalVR.GetHandPose( i );
		if ( hand.bValid )
			vr.flags |= ( i == VR_HAND_LEFT ) ? VRCMD_LEFT_VALID : VRCMD_RIGHT_VALID;
		vr.handOffset[i] = TrackingOffsetToWorld( Vector( hand.mat[0][3], hand.mat[1][3], hand.mat[2][3] ) );
		MatrixAngles( m_WorldFromHand[i], vr.handAngles[i] );
		VectorRotate( hand.vecVelocity, yaw, vr.handVelocity[i] );
		VectorRotate( hand.vecAngVelocity, yaw, vr.handAngVelocity[i] );
	}

	//
	// View angles are the head.
	//
	cmd->viewangles = m_angHead;
	engine->SetViewAngles( m_angHead );

	//
	// Smooth locomotion (added to any keyboard movement)
	//
	Vector2D vecMove = g_PortalVR.GetMoveStick();
	ApplyStickDeadzone( vecMove, vr_move_deadzone.GetFloat() );
	if ( vecMove.LengthSqr() > 0.0f )
	{
		static ConVarRef cl_forwardspeed( "cl_forwardspeed" );
		static ConVarRef cl_sidespeed( "cl_sidespeed" );
		float flForward = vecMove.y;
		float flSide = vecMove.x;

		if ( vr_move_hand_relative.GetBool() && m_bHandValid[freeHand] )
		{
			// Rotate the stick from the hand's yaw into the head's yaw (movement is head-relative).
			QAngle angHand;
			MatrixAngles( m_WorldFromHand[freeHand], angHand );
			float flRad = DEG2RAD( AngleDiff( angHand[YAW], m_angHead[YAW] ) );
			float c = cosf( flRad ), s = sinf( flRad );
			float flF = flForward * c - flSide * s;	// side is to the right, yaw is to the left
			float flS = flForward * s + flSide * c;
			flForward = flF;
			flSide = flS;
		}

		cmd->forwardmove += flForward * cl_forwardspeed.GetFloat();
		cmd->sidemove += flSide * cl_sidespeed.GetFloat();
		cmd->forwardmove = clamp( cmd->forwardmove, -cl_forwardspeed.GetFloat(), cl_forwardspeed.GetFloat() );
		cmd->sidemove = clamp( cmd->sidemove, -cl_sidespeed.GetFloat(), cl_sidespeed.GetFloat() );
	}

	//
	// Buttons
	//
	if ( g_PortalVR.GetDigital( VRACTION_FIRE_PORTAL1, gunHand ).bDown )
		cmd->buttons |= IN_ATTACK;
	if ( g_PortalVR.GetDigital( VRACTION_FIRE_PORTAL2, gunHand ).bDown )
		cmd->buttons |= IN_ATTACK2;
	if ( g_PortalVR.GetDigitalAny( VRACTION_JUMP ).bDown )
		cmd->buttons |= IN_JUMP;

	if ( g_PortalVR.GetDigitalAny( VRACTION_CROUCH ).bPressed )
		m_bCrouchToggled = !m_bCrouchToggled;
	bool bPhysicalCrouch = hmd.bValid && hmd.mat[2][3] < vr_crouch_height.GetFloat();
	if ( m_bCrouchToggled || bPhysicalCrouch )
		cmd->buttons |= IN_DUCK;

	const bool bHandGrab = g_PortalVR.GetDigital( VRACTION_HAND_GRAB, freeHand ).bDown;
	const bool bGunGrab = g_PortalVR.GetDigital( VRACTION_GUN_GRAB, gunHand ).bDown;
	const bool bUse = g_PortalVR.GetDigital( VRACTION_USE, freeHand ).bDown;
	if ( bHandGrab )
		vr.buttons |= VRBTN_HAND_GRAB;
	if ( bGunGrab )
		vr.buttons |= VRBTN_GUN_GRAB;
	if ( bUse )
		vr.buttons |= VRBTN_USE;


	//
	// Menu / save shortcuts
	//
	if ( g_PortalVR.GetDigitalAny( VRACTION_TOGGLE_HUD ).bPressed )
		vr_hud_visible.SetValue( !vr_hud_visible.GetBool() );
	if ( g_PortalVR.GetDigitalAny( VRACTION_QUICKSAVE ).bPressed )
		engine->ClientCmd_Unrestricted( "save quick" );
	if ( g_PortalVR.GetDigitalAny( VRACTION_QUICKLOAD ).bPressed )
		engine->ClientCmd_Unrestricted( "load quick" );
}

//-----------------------------------------------------------------------------
// Debug: prints the view model's bones and attachments in model space, used to work
// out where the portal gun's handle is relative to the view model origin.
//-----------------------------------------------------------------------------
CON_COMMAND( vr_gun_debug, "Print the portal gun view model's bones and attachments (model space)" )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseViewModel *pVM = pPlayer ? pPlayer->GetViewModel( 0 ) : NULL;
	if ( !pVM || !pVM->GetModelPtr() )
	{
		Msg( "No view model\n" );
		return;
	}

	CStudioHdr *pHdr = pVM->GetModelPtr();
	matrix3x4_t worldToModel;
	MatrixInvert( pVM->EntityToWorldTransform(), worldToModel );
	Msg( "View model %s\n", modelinfo->GetModelName( pVM->GetModel() ) );

	C_BaseAnimating::PushAllowBoneAccess( true, true, "vr_gun_debug" );
	pVM->SetupBones( NULL, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime );
	for ( int i = 0; i < pHdr->numbones(); i++ )
	{
		matrix3x4_t boneToWorld;
		pVM->GetBoneTransform( i, boneToWorld );
		Vector vecWorld, vecModel;
		MatrixGetColumn( boneToWorld, 3, vecWorld );
		VectorTransform( vecWorld, worldToModel, vecModel );
		Msg( "  bone %2d %-32s %7.2f %7.2f %7.2f\n", i, pHdr->pBone( i )->pszName(), vecModel.x, vecModel.y, vecModel.z );
	}
	for ( int i = 1; i <= pHdr->GetNumAttachments(); i++ )
	{
		Vector vecWorld, vecModel;
		QAngle ang;
		if ( pVM->GetAttachment( i, vecWorld, ang ) )
		{
			VectorTransform( vecWorld, worldToModel, vecModel );
			Msg( "  attachment %d %-24s %7.2f %7.2f %7.2f\n", i, pHdr->pAttachment( i - 1 ).pszName(), vecModel.x, vecModel.y, vecModel.z );
		}
	}
	C_BaseAnimating::PopBoneAccess( "vr_gun_debug" );
}
