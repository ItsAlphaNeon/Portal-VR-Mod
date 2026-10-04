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
#include "materialsystem/imaterialvar.h"
#include "materialsystem/materialsystem_config.h"
#include "view_shared.h"
#include "view.h"
#include "iviewrender.h"
#include "iclientmode.h"
#include "input.h"
#include "in_buttons.h"
#include "usercmd.h"
#include "c_baseplayer.h"
#include "gamerules.h"
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

#include "filesystem.h"
#include "vguicenterprint.h"
#include "clientleafsystem.h"
#include "c_baseanimating.h"
#ifdef PORTAL
extern bool g_bPortalRollingCredits;	// portal_credits.cpp: the credits HUD element is up
#endif
#include "beamdraw.h"
#include "iviewrender_beams.h"

#ifdef PORTAL
#include "c_portal_player.h"
#include "c_weapon_portalgun.h"
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
ConVar vr_gun_model( "vr_gun_model", "models/vr/portalgun_rtx.mdl", FCVAR_ARCHIVE, "Model of the hand-held portal gun (+X along the barrel)." );
ConVar vr_gun_scale( "vr_gun_scale", "1", FCVAR_ARCHIVE, "Size of the hand-held portal gun model." );
// Pose of the gun model in the gun controller's grip space. Defaults: calibrated on the Steam
// Frame right controller (2026-10-03). Set by vr_gun_calibrate (saved to
// cfg/vr_gun_calibration.cfg), or from the SteamVR hand skeleton while vr_gun_calibrated is 0.
ConVar vr_gun_x( "vr_gun_x", "3.682", FCVAR_ARCHIVE, "Gun model position in the controller grip: forward (units)." );
ConVar vr_gun_y( "vr_gun_y", "1.191", FCVAR_ARCHIVE, "Gun model position in the controller grip: left (units)." );
ConVar vr_gun_z( "vr_gun_z", "-5.874", FCVAR_ARCHIVE, "Gun model position in the controller grip: up (units)." );
ConVar vr_gun_pitch( "vr_gun_pitch", "60.82", FCVAR_ARCHIVE, "Gun model rotation in the controller grip: pitch (degrees, + = down)." );
ConVar vr_gun_yaw( "vr_gun_yaw", "-33.81", FCVAR_ARCHIVE, "Gun model rotation in the controller grip: yaw (degrees, + = left)." );
ConVar vr_gun_roll( "vr_gun_roll", "-33.26", FCVAR_ARCHIVE, "Gun model rotation in the controller grip: roll (degrees)." );
ConVar vr_gun_calibrated( "vr_gun_calibrated", "1", FCVAR_ARCHIVE, "1 = use the saved gun pose (vr_gun_x...), 0 = place the gun from the SteamVR hand skeleton." );
ConVar vr_show_controllers( "vr_show_controllers", "1", FCVAR_ARCHIVE, "Draw the SteamVR controller models: 0 = never, 1 = always, 2 = only while calibrating the gun." );
ConVar vr_show_skeleton( "vr_debug_skeleton", "0", 0, "Debug: draw the SteamVR hand skeletons: 0 = never, 1 = always, 2 = only while calibrating the gun." );

// Muzzle of the gun model (model space, before vr_gun_scale). See tools/gunmodel.
static const Vector s_vecGunMuzzleInModel( 16.94f, 0.0f, 0.0f );

// Aim direction relative to the gun model's barrel axis (set in calibration with the right stick).
ConVar vr_gun_aim_pitch( "vr_gun_aim_pitch", "2.97", FCVAR_ARCHIVE, "Aim direction relative to the gun model: pitch (degrees, + = down)." );
ConVar vr_gun_aim_yaw( "vr_gun_aim_yaw", "6.84", FCVAR_ARCHIVE, "Aim direction relative to the gun model: yaw (degrees, + = left)." );
ConVar vr_gun_aim_adjust_speed( "vr_gun_aim_adjust_speed", "15", FCVAR_ARCHIVE, "Degrees per second the right stick turns the aim while calibrating." );

// Core glow sprite, in gun model space (units; +X = barrel forward, +Y = left, +Z = up).
// Defaults tuned by the user in the headset (2026-10-04).
ConVar vr_gun_glow_x( "vr_gun_glow_x", "4.0", FCVAR_ARCHIVE, "Gun core glow position: forward (model units)." );
ConVar vr_gun_glow_y( "vr_gun_glow_y", "-1.08", FCVAR_ARCHIVE, "Gun core glow position: left (model units)." );
ConVar vr_gun_glow_z( "vr_gun_glow_z", "-0.148", FCVAR_ARCHIVE, "Gun core glow position: up (model units)." );
ConVar vr_gun_glow_size( "vr_gun_glow_size", "3.24", FCVAR_ARCHIVE, "Gun core glow sprite size (model units)." );

// Grab electricity (the beams the gun shows while holding an object), in gun model space.
// Each beam starts on a claw (offset from its hinge: along the barrel, and outward along the
// claw, so it follows the claw animation) and ends at one point in front of the barrel.
// Tune in the headset with vr_gun_beam_edit (saved to cfg/vr_gun_beam.cfg). Defaults: the
// user's in-headset tuning (2026-10-04).
ConVar vr_gun_beam_claw1( "vr_gun_beam_claw1", "9.159 1.186 0.403", FCVAR_ARCHIVE, "Grab electricity start on the top claw: offset from its hinge, model axes (x y z), swings with the claw." );
ConVar vr_gun_beam_claw2( "vr_gun_beam_claw2", "8.190 2.404 -1.228", FCVAR_ARCHIVE, "Grab electricity start on the left claw: offset from its hinge, model axes (x y z), swings with the claw." );
ConVar vr_gun_beam_claw3( "vr_gun_beam_claw3", "8.490 -0.547 -1.301", FCVAR_ARCHIVE, "Grab electricity start on the right claw: offset from its hinge, model axes (x y z), swings with the claw." );
static ConVar *s_pBeamClawVars[3] = { &vr_gun_beam_claw1, &vr_gun_beam_claw2, &vr_gun_beam_claw3 };
static Vector GetVectorCvar( const ConVar &var )
{
	Vector v( 0, 0, 0 );
	sscanf( var.GetString(), "%f %f %f", &v.x, &v.y, &v.z );
	return v;
}
static void SetVectorCvar( ConVar &var, const Vector &v )
{
	char sz[64];
	Q_snprintf( sz, sizeof( sz ), "%.3f %.3f %.3f", v.x, v.y, v.z );
	var.SetValue( sz );
}
ConVar vr_gun_beam_end_x( "vr_gun_beam_end_x", "14.436", FCVAR_ARCHIVE, "Grab electricity end point: forward (model units)." );
ConVar vr_gun_beam_end_y( "vr_gun_beam_end_y", "0.210", FCVAR_ARCHIVE, "Grab electricity end point: left (model units)." );
ConVar vr_gun_beam_end_z( "vr_gun_beam_end_z", "-1.621", FCVAR_ARCHIVE, "Grab electricity end point: up (model units)." );
ConVar vr_gun_beam_edit_speed( "vr_gun_beam_edit_speed", "10", FCVAR_ARCHIVE, "Units per second the sticks move the points in vr_gun_beam_edit." );

// Locomotion
ConVar vr_move_hand_relative( "vr_move_hand_relative", "0", FCVAR_ARCHIVE, "0 = stick moves toward where you look, 1 = toward where your free hand points." );
ConVar vr_move_deadzone( "vr_move_deadzone", "0.15", FCVAR_ARCHIVE );
ConVar vr_turn_mode( "vr_turn_mode", "0", FCVAR_ARCHIVE, "0 = snap turn, 1 = smooth turn." );
ConVar vr_snap_turn_angle( "vr_snap_turn_angle", "45", FCVAR_ARCHIVE );
ConVar vr_smooth_turn_speed( "vr_smooth_turn_speed", "180", FCVAR_ARCHIVE, "Smooth turn speed (degrees per second)." );
ConVar vr_roomscale_deadzone( "vr_roomscale_deadzone", "4", FCVAR_ARCHIVE, "How far (units) the head can move from the body before the body follows. Lets you lean." );
ConVar vr_crouch_height( "vr_crouch_height", "44", FCVAR_ARCHIVE, "Head height (units) below which you physically crouch." );

// Comfort / play style
ConVar vr_portal_view_mode( "vr_portal_view_mode", "0", FCVAR_ARCHIVE, "Going through floor/ceiling portals: 0 = instant, horizon stays level; 1 = original game: the view turns with the portal, then rolls back level." );
ConVar vr_portal_lerp_time( "vr_portal_lerp_time", "0.8", FCVAR_ARCHIVE, "Seconds the view takes to roll back level with vr_portal_view_mode 1." );
static void SeatedChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar vr_seated( "vr_seated", "0", FCVAR_ARCHIVE, "Seated play: your current head height becomes Chell's standing eye height (re-measured by recenter).", SeatedChanged );
ConVar vr_seated_offset( "vr_seated_offset", "0", FCVAR_ARCHIVE, "Height (units) added to the head while seated. Set by vr_seated / recenter." );
ConVar vr_eye_height( "vr_eye_height", "64", FCVAR_ARCHIVE, "Standing eye height (units) seated mode lifts you to." );

static void SeatedChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	if ( vr_seated.GetBool() )
		g_ClientVirtualReality.MeasureSeatedHeight();
}

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

// Debug: skip parts of the VR frame (bitmask) to bisect problems.
ConVar vr_dbg_skip( "vr_dbg_skip", "0", 0, "1 calibration, 2 gun model, 4 overlays, 8 menu, 16 hand devices/skeleton, 32 CreateMove VR block" );

// Automated testing: exec a cfg a few seconds after spawning in a map, log the eye every frame.
ConVar vr_test_cfg( "vr_test_cfg", "", 0, "Debug: cfg to exec vr_test_delay seconds after spawning in each map." );
ConVar vr_test_delay( "vr_test_delay", "4", 0 );
ConVar vr_log_eye( "vr_log_eye", "0", 0, "Debug: log the eye position every frame to vr_log.txt." );

// vr_after <seconds> <command>: run a command later ("wait" does nothing in this engine).
struct VRDelayedCommand_t
{
	float flTime;
	char szCommand[256];
};
static CUtlVector<VRDelayedCommand_t> s_DelayedCommands;

CON_COMMAND( vr_after, "vr_after <seconds> <command...>: run a command after a delay" )
{
	if ( args.ArgC() < 3 )
		return;
	VRDelayedCommand_t cmd;
	cmd.flTime = gpGlobals->realtime + atof( args[1] );
	const char *pszRest = args.ArgS() + Q_strlen( args[1] );
	while ( *pszRest == ' ' || *pszRest == '\t' )
		pszRest++;
	Q_strncpy( cmd.szCommand, pszRest, sizeof( cmd.szCommand ) );
	// Strip surrounding quotes.
	int nLen = Q_strlen( cmd.szCommand );
	if ( nLen >= 2 && cmd.szCommand[0] == '"' && cmd.szCommand[nLen - 1] == '"' )
	{
		cmd.szCommand[nLen - 1] = 0;
		memmove( cmd.szCommand, cmd.szCommand + 1, nLen - 1 );
	}
	s_DelayedCommands.AddToTail( cmd );
}

static void RunDelayedCommands()
{
	for ( int i = s_DelayedCommands.Count() - 1; i >= 0; i-- )
	{
		if ( gpGlobals->realtime < s_DelayedCommands[i].flTime )
			continue;
		char szCommand[260];
		Q_snprintf( szCommand, sizeof( szCommand ), "%s\n", s_DelayedCommands[i].szCommand );
		s_DelayedCommands.Remove( i );
		engine->ClientCmd_Unrestricted( szCommand );
	}
}

// Mirror
ConVar vr_mirror( "vr_mirror", "1", FCVAR_ARCHIVE, "Show the left eye in the desktop window." );

CON_COMMAND( vr_recenter, "Put your body back under your head" )
{
	g_ClientVirtualReality.Recenter();
}

CON_COMMAND( vr_debug_turn, "Debug: turn the play space by <degrees>" )
{
	if ( args.ArgC() > 1 )
		g_ClientVirtualReality.DebugTurn( atof( args[1] ) );
}

CON_COMMAND( vr_gun_calibrate, "Toggle gun placement calibration (free-hand grip moves the gun, A saves, B resets)" )
{
	g_ClientVirtualReality.SetGunCalibration( !g_ClientVirtualReality.IsCalibratingGun() );
}

CON_COMMAND( vr_gun_autoplace, "Place the gun on the gun hand from the SteamVR hand skeleton (forgets the calibration)" )
{
	vr_gun_calibrated.SetValue( 0 );
	g_ClientVirtualReality.AutoPlaceGun( true );
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
	m_vecPoseOrigin.Init();
	m_flTestStartTime = -1.0f;
	m_flDuckJumpOffset = 0.0f;
	m_bDuckRequested = false;
	m_bPortalLerpActive = false;
	SetIdentityMatrix( m_matPortalLerp );
	m_qPortalLerpStart.Init( 0, 0, 0, 1 );
	m_flPortalLerpStartTime = 0.0f;
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		SetIdentityMatrix( m_WorldFromHand[i] );
		m_bHandValid[i] = false;
		m_ControllerModel[i].szName[0] = 0;
		m_ControllerModel[i].nState = 0;
		m_ControllerModel[i].pMesh = NULL;
	}
	SetIdentityMatrix( m_WorldFromGunModel );
	SetIdentityMatrix( m_FreeFromGunModel );
	m_bGunAutoPlaced = false;
	m_bCalibrating = false;
	m_bBeamEdit = false;
	m_flLastSmoothTurnTime = 0.0;
	m_nBeamEditTarget = 0;
	m_bBeamPointsValid = false;
	for ( int i = 0; i < 3; i++ )
		m_pGunBeam[i] = NULL;
	m_bCalibGrabbing = false;
	m_flCalibHintTime = 0.0f;
	m_pControllerMaterial = NULL;
	m_pOverlayMaterial = NULL;
	m_pGlowMaterial = NULL;
	m_pGunMaterial = NULL;
	m_pGunCoreMaterial = NULL;
	m_flGunFireTime = -100.0f;
	m_flGunLastNextAttack = 0.0f;
	m_flGunHoldBlend = 0.0f;
	m_bGunWasHolding = false;
	m_vecGunGlow.Init( 1, 1, 1 );
	m_pHudMaterial = NULL;
	m_pHudMaterialOpaque = NULL;
	m_pMirrorMaterial = NULL;
	m_pLaserMaterial = NULL;
	m_bMenuOpen = false;
	m_bCreditsShown = false;
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
	ReleaseControllerModels();
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

	// vr_portal_view_mode 1: the whole play space is still turned by the last portal,
	// rotating around the head, and settles back level.
	if ( m_bPortalLerpActive )
		ConcatTransforms( m_matPortalLerp, world, world );
}

Vector CClientVirtualReality::TrackingOffsetToWorld( const Vector &trk ) const
{
	Vector vecLocal( trk.x - m_vecTrackingCenter.x, trk.y - m_vecTrackingCenter.y, trk.z + GetHeightOffset() );
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
	if ( vr_seated.GetBool() )
		MeasureSeatedHeight();
}

float CClientVirtualReality::GetHeightOffset() const
{
	return ( vr_seated.GetBool() ? vr_seated_offset.GetFloat() : 0.0f ) + m_flDuckJumpOffset;
}

float CClientVirtualReality::GetHeadHeight() const
{
	// Without the duck-jump compensation: that one follows the hull, not your head.
	return g_PortalVR.GetHmdPose().mat[2][3] + ( vr_seated.GetBool() ? vr_seated_offset.GetFloat() : 0.0f );
}

//-----------------------------------------------------------------------------
// Single-player Source "duck-jumps": a jump tucks the hull in the air, which lifts the
// origin by the duck height, and the flat game hides that by lowering the view offset in
// the same tick. The HMD sets our eye height, so do the same thing to the play space:
// follow the view offset whenever the duck wasn't asked for (crouch button / real crouch).
//-----------------------------------------------------------------------------
void CClientVirtualReality::UpdateDuckJumpOffset( C_BasePlayer *pPlayer )
{
	m_flDuckJumpOffset = 0.0f;
	if ( m_bDuckRequested || !pPlayer->IsAlive() || !g_pGameRules )
		return;
	m_flDuckJumpOffset = MIN( 0.0f, pPlayer->GetViewOffset().z - g_pGameRules->GetViewVectors()->m_vView.z );
}

void CClientVirtualReality::MeasureSeatedHeight()
{
	const VRTrackedPose_t &hmd = g_PortalVR.GetHmdPose();
	if ( !hmd.bValid )
		return;
	vr_seated_offset.SetValue( MAX( 0.0f, vr_eye_height.GetFloat() - hmd.mat[2][3] ) );
	VRLog( "Seated: head %.1f, offset %.1f", hmd.mat[2][3], vr_seated_offset.GetFloat() );
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

//-----------------------------------------------------------------------------
// Smooth turning runs once per rendered frame with the real frame time. In CreateMove it
// only advanced on game ticks (66/s), so at 90+ fps frames got zero, one or two steps:
// a juddering rotation.
//-----------------------------------------------------------------------------
void CClientVirtualReality::UpdateSmoothTurn( C_BasePlayer *pPlayer )
{
	const double flNow = Plat_FloatTime();
	const float flDelta = clamp( (float)( flNow - m_flLastSmoothTurnTime ), 0.0f, 0.1f );
	m_flLastSmoothTurnTime = flNow;

	if ( vr_turn_mode.GetInt() == 0 || m_bCalibrating || m_bBeamEdit || !pPlayer->IsAlive()
		 || engine->IsPaused() || enginevgui->IsGameUIVisible() )
		return;

	const Vector2D vecTurn = g_PortalVR.GetTurnStick();
	if ( fabsf( vecTurn.x ) > 0.15f )
		ApplyTurn( -vecTurn.x * vr_smooth_turn_speed.GetFloat() * flDelta );
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

	if ( vr_portal_view_mode.GetInt() == 1 )
	{
		// Original-game feel: start with the full portal rotation (the view comes out of the
		// exit portal exactly as it went in) and let it settle back to level. The yaw part is
		// already applied above; the rest is extra = portal * inverse( yaw ).
		matrix3x4_t matPortal, matYaw, matYawInv, matExtra;
		matPortal = matPortalTransform.As3x4();
		MatrixSetColumn( vec3_origin, 3, matPortal );
		AngleMatrix( QAngle( 0, flDelta, 0 ), matYaw );
		MatrixInvert( matYaw, matYawInv );
		ConcatTransforms( matPortal, matYawInv, matExtra );
		MatrixQuaternion( matExtra, m_qPortalLerpStart );
		// If we're mid-lerp from a previous portal, fold the remaining rotation in.
		if ( m_bPortalLerpActive )
		{
			Quaternion qRemaining, qCombined;
			GetPortalLerpRotation( qRemaining );
			QuaternionMult( m_qPortalLerpStart, qRemaining, qCombined );
			m_qPortalLerpStart = qCombined;
		}
		m_flPortalLerpStartTime = gpGlobals->realtime;
		m_bPortalLerpActive = true;
	}

	g_PortalVR.TriggerHaptic( VR_HAND_LEFT, 0.05f, 60.0f, 0.4f );
	g_PortalVR.TriggerHaptic( VR_HAND_RIGHT, 0.05f, 60.0f, 0.4f );
}

// Remaining extra rotation of a vr_portal_view_mode 1 transition.
bool CClientVirtualReality::GetPortalLerpRotation( Quaternion &q ) const
{
	float flTime = MAX( 0.01f, vr_portal_lerp_time.GetFloat() );
	float t = ( gpGlobals->realtime - m_flPortalLerpStartTime ) / flTime;
	if ( t >= 1.0f )
		return false;
	t = SimpleSpline( clamp( t, 0.0f, 1.0f ) );
	Quaternion qIdentity( 0, 0, 0, 1 );
	QuaternionSlerp( m_qPortalLerpStart, qIdentity, t, q );
	return true;
}

void CClientVirtualReality::UpdateWorldPoses( C_BasePlayer *pPlayer )
{
	const VRTrackedPose_t &hmd = g_PortalVR.GetHmdPose();
	const Vector vecOrigin = pPlayer->GetAbsOrigin();
	UpdateDuckJumpOffset( pPlayer );

	// Portal view lerp: a rotation of the whole play space around the (unrotated) head.
	m_bPortalLerpActive = false;
	Quaternion qLerp;
	if ( vr_portal_view_mode.GetInt() == 1 && m_flPortalLerpStartTime > 0.0f && GetPortalLerpRotation( qLerp ) )
	{
		matrix3x4_t worldHead, rot;
		TrackingToWorld( hmd.mat, vecOrigin, worldHead );
		Vector vecPivot;
		MatrixGetColumn( worldHead, 3, vecPivot );
		QuaternionMatrix( qLerp, rot );
		Vector vecRotatedPivot;
		VectorRotate( vecPivot, rot, vecRotatedPivot );
		MatrixSetColumn( vecPivot - vecRotatedPivot, 3, rot );	// x' = pivot + R * ( x - pivot )
		m_matPortalLerp = rot;
		m_bPortalLerpActive = true;
	}

	matrix3x4_t worldHead;
	TrackingToWorld( hmd.mat, vecOrigin, worldHead );
	m_WorldFromMidEye = VMatrix( worldHead );
	MatrixAngles( worldHead, m_angHead );
	m_vecHeadOffset = TrackingOffsetToWorld( Vector( hmd.mat[0][3], hmd.mat[1][3], hmd.mat[2][3] ) );

	m_vecPoseOrigin = vecOrigin;
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		const VRTrackedPose_t &hand = g_PortalVR.GetHandPose( i );
		m_bHandValid[i] = hand.bValid;
		TrackingToWorld( hand.mat, vecOrigin, m_WorldFromHand[i] );
	}
	UpdateGunTransform();

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

//-----------------------------------------------------------------------------
// Portal gun placement
//-----------------------------------------------------------------------------
static void GetGripFromGunModel( matrix3x4_t &gripFromModel )
{
	AngleMatrix( QAngle( vr_gun_pitch.GetFloat(), vr_gun_yaw.GetFloat(), vr_gun_roll.GetFloat() ),
		Vector( vr_gun_x.GetFloat(), vr_gun_y.GetFloat(), vr_gun_z.GetFloat() ), gripFromModel );
}

static void SetGripFromGunModel( const matrix3x4_t &gripFromModel )
{
	QAngle ang;
	Vector pos;
	MatrixAngles( gripFromModel, ang, pos );
	vr_gun_x.SetValue( pos.x );
	vr_gun_y.SetValue( pos.y );
	vr_gun_z.SetValue( pos.z );
	vr_gun_pitch.SetValue( ang[PITCH] );
	vr_gun_yaw.SetValue( ang[YAW] );
	vr_gun_roll.SetValue( ang[ROLL] );
}

void CClientVirtualReality::UpdateGunTransform()
{
	if ( m_bCalibGrabbing )
	{
		// Calibrating: the free hand carries the gun model.
		ConcatTransforms( m_WorldFromHand[GetFreeHand()], m_FreeFromGunModel, m_WorldFromGunModel );
		return;
	}
	matrix3x4_t gripFromModel;
	GetGripFromGunModel( gripFromModel );
	ConcatTransforms( m_WorldFromHand[GetGunHand()], gripFromModel, m_WorldFromGunModel );
}

bool CClientVirtualReality::GetGunAim( Vector &origin, Vector &direction ) const
{
	Vector vecAimInModel;
	AngleVectors( QAngle( vr_gun_aim_pitch.GetFloat(), vr_gun_aim_yaw.GetFloat(), 0.0f ), &vecAimInModel );
	VectorRotate( vecAimInModel, m_WorldFromGunModel, direction );
	VectorTransform( s_vecGunMuzzleInModel * vr_gun_scale.GetFloat(), m_WorldFromGunModel, origin );
	return m_bHandValid[GetGunHand()];
}

//-----------------------------------------------------------------------------
// Places the gun from SteamVR's "grip limit" skeleton: a fist closed around the
// controller. The barrel runs along the back of the hand (wrist -> knuckles), the
// handle down through the fist (index -> pinky), and the gun sits on top of the fist.
//-----------------------------------------------------------------------------
bool CClientVirtualReality::AutoPlaceGun( bool bVerbose )
{
	const int hand = GetGunHand();
	matrix3x4_t bones[VR_SKELETON_BONE_COUNT];
	if ( !g_PortalVR.GetGripFromFistSkeleton( hand, bones ) )
	{
		if ( bVerbose )
			VRLog( "vr_gun_autoplace: no hand skeleton for the gun hand yet" );
		return false;
	}

	Vector pos[VR_SKELETON_BONE_COUNT];
	for ( int b = 0; b < VR_SKELETON_BONE_COUNT; b++ )
		MatrixGetColumn( bones[b], 3, pos[b] );

	const Vector &vecWrist = pos[1];
	const Vector &vecIndexKnuckle = pos[7];
	const Vector &vecPinkyKnuckle = pos[22];
	Vector vecKnuckles = ( pos[7] + pos[12] + pos[17] + pos[22] ) * 0.25f;
	Vector vecFistCenter = ( pos[7] + pos[12] + pos[17] + pos[22] + pos[9] + pos[14] + pos[19] + pos[24] ) * 0.125f;

	Vector vecUp = vecIndexKnuckle - vecPinkyKnuckle;
	if ( VectorNormalize( vecUp ) < 0.1f )
		return false;
	Vector vecForward = vecKnuckles - vecWrist;
	vecForward -= vecUp * DotProduct( vecForward, vecUp );
	if ( VectorNormalize( vecForward ) < 0.1f )
		return false;
	Vector vecLeft = CrossProduct( vecUp, vecForward );

	// Barrel center line above the fist: the gun body's radius is about 6 units.
	Vector vecOrigin = vecFistCenter + vecUp * ( DotProduct( vecIndexKnuckle - vecFistCenter, vecUp ) + 5.8f );

	matrix3x4_t gripFromModel;
	MatrixInitialize( gripFromModel, vecOrigin, vecForward, vecLeft, vecUp );
	SetGripFromGunModel( gripFromModel );
	m_bGunAutoPlaced = true;
	VRLog( "Gun placed from the hand skeleton: vr_gun_x %.2f vr_gun_y %.2f vr_gun_z %.2f vr_gun_pitch %.1f vr_gun_yaw %.1f vr_gun_roll %.1f",
		vr_gun_x.GetFloat(), vr_gun_y.GetFloat(), vr_gun_z.GetFloat(), vr_gun_pitch.GetFloat(), vr_gun_yaw.GetFloat(), vr_gun_roll.GetFloat() );
	return true;
}

void CClientVirtualReality::SetGunCalibration( bool bOn )
{
	if ( bOn == m_bCalibrating )
		return;
	m_bCalibrating = bOn;
	m_bCalibGrabbing = false;
	m_flCalibHintTime = 0.0f;
	VRLog( "Gun calibration %s", bOn ? "started" : "ended" );
	if ( !bOn && internalCenterPrint )
		internalCenterPrint->Clear();
}

void CClientVirtualReality::SaveGunCalibration()
{
	vr_gun_calibrated.SetValue( 1 );
	char szCfg[1024];
	Q_snprintf( szCfg, sizeof( szCfg ),
		"// Portal VR gun placement, saved by vr_gun_calibrate (render model: %s)\n"
		"vr_gun_x %.3f\nvr_gun_y %.3f\nvr_gun_z %.3f\nvr_gun_pitch %.2f\nvr_gun_yaw %.2f\nvr_gun_roll %.2f\n"
		"vr_gun_aim_pitch %.2f\nvr_gun_aim_yaw %.2f\nvr_gun_calibrated 1\n",
		g_PortalVR.GetRenderModelName( GetGunHand() ),
		vr_gun_x.GetFloat(), vr_gun_y.GetFloat(), vr_gun_z.GetFloat(), vr_gun_pitch.GetFloat(), vr_gun_yaw.GetFloat(), vr_gun_roll.GetFloat(),
		vr_gun_aim_pitch.GetFloat(), vr_gun_aim_yaw.GetFloat() );
	FileHandle_t fh = g_pFullFileSystem->Open( "cfg/vr_gun_calibration.cfg", "w", "MOD" );
	if ( fh )
	{
		g_pFullFileSystem->Write( szCfg, Q_strlen( szCfg ), fh );
		g_pFullFileSystem->Close( fh );
	}
	VRLog( "Gun calibration saved: %s", szCfg );
	engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
}

void CClientVirtualReality::SetBeamEdit( bool bOn )
{
	if ( bOn == m_bBeamEdit )
		return;
	if ( bOn )
		SetGunCalibration( false );
	m_bBeamEdit = bOn;
	m_flCalibHintTime = 0.0f;
	VRLog( "Grab electricity edit %s", bOn ? "started" : "ended" );
	if ( !bOn && internalCenterPrint )
		internalCenterPrint->Clear();
}

void CClientVirtualReality::SaveBeamPositions()
{
	char szCfg[512];
	Q_snprintf( szCfg, sizeof( szCfg ),
		"// Portal VR grab electricity, saved by vr_gun_beam_edit\n"
		"vr_gun_beam_claw1 \"%s\"\nvr_gun_beam_claw2 \"%s\"\nvr_gun_beam_claw3 \"%s\"\nvr_gun_beam_end_x %.3f\nvr_gun_beam_end_y %.3f\nvr_gun_beam_end_z %.3f\n",
		vr_gun_beam_claw1.GetString(), vr_gun_beam_claw2.GetString(), vr_gun_beam_claw3.GetString(),
		vr_gun_beam_end_x.GetFloat(), vr_gun_beam_end_y.GetFloat(), vr_gun_beam_end_z.GetFloat() );
	FileHandle_t fh = g_pFullFileSystem->Open( "cfg/vr_gun_beam.cfg", "w", "MOD" );
	if ( fh )
	{
		g_pFullFileSystem->Write( szCfg, Q_strlen( szCfg ), fh );
		g_pFullFileSystem->Close( fh );
	}
	VRLog( "Grab electricity saved: %s", szCfg );
	engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
}

//-----------------------------------------------------------------------------
// Grab electricity editor (vr_gun_beam_edit); the beams stay on and the game ignores the
// controllers meanwhile. Moves one point at a time, shown with axes on the gun (red =
// forward, green = left, blue = up); the other points get small white markers.
//   right grip (click) - next point: barrel end point, top claw, left claw, right claw
//   left stick         - forward/back (Y) and left/right (X)
//   right stick (Y)    - up/down
//   A                  - save and finish
//   B                  - reset the selected point
//-----------------------------------------------------------------------------
void CClientVirtualReality::UpdateBeamEdit()
{
	if ( !m_bBeamEdit )
		return;

	static const wchar_t *s_pwszTarget[4] = { L"BARREL END POINT", L"TOP CLAW", L"LEFT CLAW", L"RIGHT CLAW" };
	if ( g_PortalVR.GetDigital( VRACTION_HAND_GRAB, VR_HAND_RIGHT ).bPressed )
	{
		m_nBeamEditTarget = ( m_nBeamEditTarget + 1 ) % 4;
		m_flCalibHintTime = 0.0f;
		g_PortalVR.TriggerHaptic( GetGunHand(), 0.03f, 120.0f, 0.4f );
	}

	const float flStep = vr_gun_beam_edit_speed.GetFloat() * gpGlobals->frametime;
	Vector2D vecLeft = g_PortalVR.GetMoveStick();
	Vector2D vecRight = g_PortalVR.GetTurnStick();
	Vector vecDelta( 0, 0, 0 );
	if ( fabsf( vecLeft.y ) > 0.2f )
		vecDelta.x = vecLeft.y * flStep;
	if ( fabsf( vecLeft.x ) > 0.2f )
		vecDelta.y = -vecLeft.x * flStep;
	if ( fabsf( vecRight.y ) > 0.2f )
		vecDelta.z = vecRight.y * flStep;

	Vector vecPoint;
	if ( m_nBeamEditTarget == 0 )
	{
		vecPoint.Init( vr_gun_beam_end_x.GetFloat(), vr_gun_beam_end_y.GetFloat(), vr_gun_beam_end_z.GetFloat() );
		if ( !vecDelta.IsZero() )
		{
			vecPoint += vecDelta;
			vr_gun_beam_end_x.SetValue( vecPoint.x );
			vr_gun_beam_end_y.SetValue( vecPoint.y );
			vr_gun_beam_end_z.SetValue( vecPoint.z );
		}
	}
	else
	{
		ConVar &var = *s_pBeamClawVars[m_nBeamEditTarget - 1];
		vecPoint = GetVectorCvar( var );
		if ( !vecDelta.IsZero() )
		{
			vecPoint += vecDelta;
			SetVectorCvar( var, vecPoint );
		}
	}

	if ( internalCenterPrint && gpGlobals->realtime > m_flCalibHintTime )
	{
		m_flCalibHintTime = gpGlobals->realtime + 0.25f;
		wchar_t wszHint[384];
		V_snwprintf( wszHint, ARRAYSIZE( wszHint ), L"GRAB ELECTRICITY EDIT: %ls (%d/4)\nRight grip: next point    Left stick: forward/back, left/right    Right stick: up/down\nA = save    B = reset this point\n%.2f  %.2f  %.2f",
			s_pwszTarget[m_nBeamEditTarget], m_nBeamEditTarget + 1, vecPoint.x, vecPoint.y, vecPoint.z );
		internalCenterPrint->Print( wszHint );
	}

	if ( g_PortalVR.GetDigitalAny( VRACTION_JUMP ).bPressed )
	{
		SaveBeamPositions();
		g_PortalVR.TriggerHaptic( GetGunHand(), 0.1f, 80.0f, 0.6f );
		SetBeamEdit( false );
		if ( internalCenterPrint )
			internalCenterPrint->Print( (wchar_t *)L"Grab electricity saved" );
	}
	else if ( g_PortalVR.GetDigital( VRACTION_CROUCH, VR_HAND_RIGHT ).bPressed )
	{
		if ( m_nBeamEditTarget == 0 )
		{
			vr_gun_beam_end_x.Revert(); vr_gun_beam_end_y.Revert(); vr_gun_beam_end_z.Revert();
		}
		else
		{
			s_pBeamClawVars[m_nBeamEditTarget - 1]->Revert();
		}
	}
}

static void AddSegment( CMeshBuilder &meshBuilder, const Vector &a, const Vector &b, float flWidth, const unsigned char *color );

// Editor markers: axes on the selected point, small white crosses on the others.
void CClientVirtualReality::DrawBeamEditMarkers()
{
	if ( !m_bBeamPointsValid )
		return;
	for ( int i = 0; i < 4; i++ )
	{
		matrix3x4_t marker;
		MatrixCopy( m_WorldFromGunModel, marker );
		MatrixSetColumn( m_vecBeamPoint[i], 3, marker );
		if ( i == m_nBeamEditTarget )
		{
			DrawAxes( marker, 2.5f );
			continue;
		}
		static const unsigned char s_White[4] = { 255, 255, 255, 255 };
		CMatRenderContextPtr pRenderContext( materials );
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_pOverlayMaterial );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 3 );
		for ( int a = 0; a < 3; a++ )
		{
			Vector vecAxis;
			MatrixGetColumn( marker, a, vecAxis );
			AddSegment( meshBuilder, m_vecBeamPoint[i] - vecAxis * 0.4f, m_vecBeamPoint[i] + vecAxis * 0.4f, 0.15f, s_White );
		}
		meshBuilder.End();
		pMesh->Draw();
	}
}

//-----------------------------------------------------------------------------
// Calibration controls (both hands):
//   free-hand grip (hold) - pick the gun model up with the free hand; let go to put it
//                           back on the gun hand at that spot
//   A                     - save and finish
//   B                     - reset to the skeleton placement
//   both grips + right stick click - start/stop calibrating
//-----------------------------------------------------------------------------
void CClientVirtualReality::UpdateGunCalibration()
{
	const int gunHand = GetGunHand();
	const int freeHand = GetFreeHand();

	// Combo to start/stop without the console.
	if ( g_PortalVR.GetDigital( VRACTION_HAND_GRAB, VR_HAND_LEFT ).bDown && g_PortalVR.GetDigital( VRACTION_HAND_GRAB, VR_HAND_RIGHT ).bDown
		 && g_PortalVR.GetDigitalAny( VRACTION_RECENTER ).bPressed )
	{
		SetGunCalibration( !m_bCalibrating );
		return;
	}

	// Without a calibration, place the gun from the hand skeleton as soon as there is one.
	if ( !m_bCalibrating && !vr_gun_calibrated.GetBool() && !m_bGunAutoPlaced )
		AutoPlaceGun( false );

	if ( !m_bCalibrating )
		return;

	if ( internalCenterPrint && gpGlobals->realtime > m_flCalibHintTime )
	{
		m_flCalibHintTime = gpGlobals->realtime + 1.5f;
		internalCenterPrint->Print( (wchar_t *)L"GUN CALIBRATION\nHold your gun hand naturally.\nFree hand: hold GRIP to grab the gun, release to drop it on your gun hand.\nRight stick: turn the aim laser    A = save    B = reset" );
	}

	const VRDigitalState_t &grab = g_PortalVR.GetDigital( VRACTION_HAND_GRAB, freeHand );
	if ( grab.bPressed && !m_bCalibGrabbing )
	{
		matrix3x4_t freeFromWorld;
		MatrixInvert( m_WorldFromHand[freeHand], freeFromWorld );
		ConcatTransforms( freeFromWorld, m_WorldFromGunModel, m_FreeFromGunModel );
		m_bCalibGrabbing = true;
		g_PortalVR.TriggerHaptic( freeHand, 0.03f, 120.0f, 0.4f );
	}
	else if ( !grab.bDown && m_bCalibGrabbing )
	{
		// Put it on the gun hand where it was let go.
		matrix3x4_t gunFromWorld, gripFromModel;
		MatrixInvert( m_WorldFromHand[gunHand], gunFromWorld );
		ConcatTransforms( gunFromWorld, m_WorldFromGunModel, gripFromModel );
		SetGripFromGunModel( gripFromModel );
		m_bCalibGrabbing = false;
		g_PortalVR.TriggerHaptic( gunHand, 0.03f, 120.0f, 0.4f );
		VRLog( "Gun moved: vr_gun_x %.2f vr_gun_y %.2f vr_gun_z %.2f vr_gun_pitch %.1f vr_gun_yaw %.1f vr_gun_roll %.1f",
			vr_gun_x.GetFloat(), vr_gun_y.GetFloat(), vr_gun_z.GetFloat(), vr_gun_pitch.GetFloat(), vr_gun_yaw.GetFloat(), vr_gun_roll.GetFloat() );
	}

	if ( m_bCalibGrabbing )
		return;

	// Right stick turns the aim direction (shown by the aim laser) relative to the model.
	Vector2D vecStick = g_PortalVR.GetTurnStick();
	const float flRate = vr_gun_aim_adjust_speed.GetFloat() * gpGlobals->frametime;
	if ( fabsf( vecStick.x ) > 0.3f )
		vr_gun_aim_yaw.SetValue( AngleNormalize( vr_gun_aim_yaw.GetFloat() - vecStick.x * flRate ) );
	if ( fabsf( vecStick.y ) > 0.3f )
		vr_gun_aim_pitch.SetValue( clamp( vr_gun_aim_pitch.GetFloat() - vecStick.y * flRate, -45.0f, 45.0f ) );

	if ( g_PortalVR.GetDigitalAny( VRACTION_JUMP ).bPressed )
	{
		SaveGunCalibration();
		g_PortalVR.TriggerHaptic( gunHand, 0.1f, 80.0f, 0.6f );
		SetGunCalibration( false );
		if ( internalCenterPrint )
			internalCenterPrint->Print( (wchar_t *)L"Gun placement saved" );
	}
	else if ( g_PortalVR.GetDigital( VRACTION_CROUCH, VR_HAND_RIGHT ).bPressed )
	{
		vr_gun_aim_pitch.Revert();
		vr_gun_aim_yaw.Revert();
		if ( !AutoPlaceGun( true ) )
		{
			vr_gun_x.Revert(); vr_gun_y.Revert(); vr_gun_z.Revert();
			vr_gun_pitch.Revert(); vr_gun_yaw.Revert(); vr_gun_roll.Revert();
		}
	}
}

//-----------------------------------------------------------------------------
// Hand-held gun model (models/vr/portalgun_rtx.mdl, built by tools/gunmodel).
// Its three claws are bones ("prong_top", "prong_left", "prong_right", pivots at
// their bases); they are opened procedurally here after the normal bone setup.
//-----------------------------------------------------------------------------
CON_COMMAND( vr_gun_beam_edit, "Toggle the grab electricity editor (right grip = next point, left stick = forward/left, right stick = up, A saves, B resets)" )
{
	g_ClientVirtualReality.SetBeamEdit( !g_ClientVirtualReality.IsEditingBeams() );
}

ConVar vr_gun_anim( "vr_gun_anim", "1", FCVAR_ARCHIVE, "Animate the hand-held gun (claws, recoil) and make it glow in the portal colors." );

static const char *s_pszProngBones[3] = { "prong_top", "prong_left", "prong_right" };
// Direction of each claw away from the barrel (model space): it opens away from the axis.
static const Vector s_vecProngRadial[3] = { Vector( 0.0f, -0.165f, 0.986f ), Vector( 0.0f, 0.716f, -0.698f ), Vector( 0.0f, -0.787f, -0.617f ) };

class C_VRGunModel : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_VRGunModel, C_BaseAnimating );

	C_VRGunModel()
	{
		for ( int i = 0; i < 3; i++ )
		{
			m_flProngAngle[i] = 0.0f;
			m_nProngBone[i] = -2;
		}
	}

	virtual void BuildTransformations( CStudioHdr *pStudioHdr, Vector *pos, Quaternion q[], const matrix3x4_t &cameraTransform, int boneMask, CBoneBitList &boneComputed )
	{
		BaseClass::BuildTransformations( pStudioHdr, pos, q, cameraTransform, boneMask, boneComputed );

		for ( int i = 0; i < 3; i++ )
		{
			if ( m_nProngBone[i] == -2 )
				m_nProngBone[i] = LookupBone( s_pszProngBones[i] );
			const int nBone = m_nProngBone[i];
			if ( nBone < 0 || m_flProngAngle[i] == 0.0f )
				continue;

			// Rotate the bone around its own origin (the claw's hinge) about the axis that
			// swings the claw away from the barrel: barrel x radial.
			Vector vecAxis;
			VectorRotate( CrossProduct( Vector( 1, 0, 0 ), s_vecProngRadial[i] ), cameraTransform, vecAxis );
			VectorNormalize( vecAxis );

			matrix3x4_t &bone = GetBoneForWrite( nBone );
			Vector vecPivot, vecRotatedPivot;
			MatrixGetColumn( bone, 3, vecPivot );
			matrix3x4_t rot, result;
			MatrixBuildRotationAboutAxis( vecAxis, m_flProngAngle[i], rot );
			VectorRotate( vecPivot, rot, vecRotatedPivot );
			MatrixSetColumn( vecPivot - vecRotatedPivot, 3, rot );
			ConcatTransforms( rot, bone, result );
			MatrixCopy( result, bone );
		}
	}

	float	m_flProngAngle[3];	// degrees, + = open
	int		m_nProngBone[3];	// -2 = not looked up yet
};

// Portal colors (the in-game portal tints).
static const Vector s_vecGlowBlue( 0.25f, 0.62f, 1.0f );
static const Vector s_vecGlowOrange( 1.0f, 0.6f, 0.12f );
static const Vector s_vecGlowIdle( 0.45f, 0.5f, 0.55f );

//-----------------------------------------------------------------------------
// Gun animation state: fire kick (detected from the weapon's next-attack time
// jumping forward), claws open while holding something, portal-colored glow.
//-----------------------------------------------------------------------------
void CClientVirtualReality::UpdateGunAnimation( C_BasePlayer *pPlayer, C_BaseAnimating *pGunEntity, matrix3x4_t &worldFromModel )
{
	C_VRGunModel *pGun = dynamic_cast<C_VRGunModel *>( pGunEntity );
	if ( !pGun )
		return;

	float flKick = 0.0f;
	bool bHolding = false;
	Vector vecColor = s_vecGlowIdle;
#ifdef PORTAL
	C_WeaponPortalgun *pWeapon = dynamic_cast<C_WeaponPortalgun *>( pPlayer->GetActiveWeapon() );
	if ( pWeapon )
	{
		const float flNextAttack = pWeapon->m_flNextPrimaryAttack;
		if ( flNextAttack > m_flGunLastNextAttack + 0.05f && flNextAttack > gpGlobals->curtime )
			m_flGunFireTime = gpGlobals->realtime;
		m_flGunLastNextAttack = flNextAttack;

		bHolding = pWeapon->IsHoldingObject();
		if ( bHolding && !m_bGunWasHolding )
			m_flGunFireTime = gpGlobals->realtime - 0.1f;	// a smaller kick when grabbing
		m_bGunWasHolding = bHolding;

		const int nLast = pWeapon->GetLastFiredPortal();
		if ( nLast == 1 )
			vecColor = s_vecGlowBlue;
		else if ( nLast == 2 )
			vecColor = s_vecGlowOrange;
		else if ( pWeapon->CanFirePortal1() )
			vecColor = s_vecGlowBlue * 0.6f;
	}
#endif
	if ( !vr_gun_anim.GetBool() )
	{
		for ( int i = 0; i < 3; i++ )
			pGun->m_flProngAngle[i] = 0.0f;
		return;
	}

	const float flSinceFire = gpGlobals->realtime - m_flGunFireTime;
	if ( flSinceFire >= 0.0f && flSinceFire < 0.3f )
	{
		const float t = 1.0f - flSinceFire / 0.3f;
		flKick = t * t;
	}

	// Claws: open while holding (with the jitter of the real gun), flick on every shot.
	const float flTarget = bHolding ? 1.0f : 0.0f;
	m_flGunHoldBlend = Approach( flTarget, m_flGunHoldBlend, gpGlobals->frametime * 6.0f );
	for ( int i = 0; i < 3; i++ )
	{
		float flJitter = m_flGunHoldBlend * 1.5f * sinf( gpGlobals->realtime * 31.0f + i * 2.1f );
		pGun->m_flProngAngle[i] = m_flGunHoldBlend * 18.0f + flKick * 14.0f + flJitter;
	}

	// Recoil: back and up a little, around the grip.
	if ( flKick > 0.0f )
	{
		matrix3x4_t recoil, result;
		AngleMatrix( QAngle( -6.0f * flKick, 0.0f, 0.0f ), Vector( -1.5f * flKick, 0.0f, 0.0f ), recoil );
		ConcatTransforms( worldFromModel, recoil, result );
		MatrixCopy( result, worldFromModel );
	}

	// Glow: the indicator lights (self-illum mask) and the core in the glass tube.
	const float flBright = 1.0f + 1.5f * flKick + 0.4f * m_flGunHoldBlend;
	m_vecGunGlow = vecColor * flBright;
	if ( !m_pGunMaterial )
	{
		m_pGunMaterial = materials->FindMaterial( "models/vr/portalgun_rtx/portalgun_rtx", TEXTURE_GROUP_MODEL, false );
		m_pGunCoreMaterial = materials->FindMaterial( "models/vr/portalgun_rtx/portalgun_rtx_core", TEXTURE_GROUP_MODEL, false );
		if ( m_pGunMaterial )
			m_pGunMaterial->IncrementReferenceCount();
		if ( m_pGunCoreMaterial )
			m_pGunCoreMaterial->IncrementReferenceCount();
	}
	bool bFound;
	if ( m_pGunMaterial )
	{
		IMaterialVar *pVar = m_pGunMaterial->FindVar( "$selfillumtint", &bFound, false );
		if ( bFound )
			pVar->SetVecValue( m_vecGunGlow.x * 1.6f, m_vecGunGlow.y * 1.6f, m_vecGunGlow.z * 1.6f );
	}
	if ( m_pGunCoreMaterial )
	{
		// The core inside the (clear) glass tube glows in the portal color, as bright as it goes.
		IMaterialVar *pVar = m_pGunCoreMaterial->FindVar( "$color2", &bFound, false );
		if ( bFound )
			pVar->SetVecValue( m_vecGunGlow.x * 2.0f, m_vecGunGlow.y * 2.0f, m_vecGunGlow.z * 2.0f );
	}
}

// Soft glow in the gun's core, drawn with the world overlays.
void CClientVirtualReality::DrawGunGlow()
{
	C_BaseEntity *pGun = m_hGunModel.Get();
	if ( !pGun || pGun->IsEffectActive( EF_NODRAW ) || !vr_gun_anim.GetBool() || !m_pGlowMaterial )
		return;

	Vector vecCore;
	const Vector vecGlow( vr_gun_glow_x.GetFloat(), vr_gun_glow_y.GetFloat(), vr_gun_glow_z.GetFloat() );
	VectorTransform( vecGlow * vr_gun_scale.GetFloat(), m_WorldFromGunModel, vecCore );
	const float flSize = vr_gun_glow_size.GetFloat() * vr_gun_scale.GetFloat() * ( 1.0f + 0.6f * ( m_vecGunGlow.Length() - 1.0f ) );
	Vector vecToEye = CurrentViewOrigin() - vecCore;
	VectorNormalize( vecToEye );
	Vector vecRight = CrossProduct( vecToEye, Vector( 0, 0, 1 ) );
	if ( VectorNormalize( vecRight ) < 0.01f )
		vecRight.Init( 0, 1, 0 );
	Vector vecUp = CrossProduct( vecRight, vecToEye );
	vecRight *= flSize;
	vecUp *= flSize;

	unsigned char color[4] = { (unsigned char)clamp( (int)( m_vecGunGlow.x * 200 ), 0, 255 ), (unsigned char)clamp( (int)( m_vecGunGlow.y * 200 ), 0, 255 ),
							   (unsigned char)clamp( (int)( m_vecGunGlow.z * 200 ), 0, 255 ), 255 };
	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_pGlowMaterial );
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );
	meshBuilder.Position3fv( ( vecCore - vecRight - vecUp ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 0, 1 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( vecCore - vecRight + vecUp ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 0, 0 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( vecCore + vecRight + vecUp ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 1, 0 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( vecCore + vecRight - vecUp ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 1, 1 ); meshBuilder.AdvanceVertex();
	meshBuilder.End();
	pMesh->Draw();
}

//-----------------------------------------------------------------------------
// Grab electricity: the three beams from the claws to the front of the barrel that the
// portal gun shows while holding an object (the stock ones hang off the hidden view model).
//-----------------------------------------------------------------------------
void CClientVirtualReality::FreeGunBeams()
{
	for ( int i = 0; i < 3; i++ )
	{
		// Let the beam system retire it (like CPortalgunEffectBeam::Release). FreeBeam would put
		// it on the free list while it is still in the active list, and ClearBeams at the
		// level change would delete it twice (crash on death / map change).
		if ( m_pGunBeam[i] )
		{
			m_pGunBeam[i]->flags = 0;
			m_pGunBeam[i]->die = gpGlobals->curtime - 1.0f;
			m_pGunBeam[i]->brightness = 0.0f;
		}
		m_pGunBeam[i] = NULL;
	}
}

void CClientVirtualReality::UpdateGunBeams( C_BaseAnimating *pGunEntity, const matrix3x4_t &worldFromModel )
{
	C_VRGunModel *pGun = dynamic_cast<C_VRGunModel *>( pGunEntity );
	const bool bOn = pGun && ( m_bBeamEdit || ( vr_gun_anim.GetBool() && m_flGunHoldBlend > 0.01f ) );
	const float flBrightness = m_bBeamEdit ? 128.0f : 128.0f * m_flGunHoldBlend;
	CStudioHdr *pHdr = pGun ? pGun->GetModelPtr() : NULL;
	const float flScale = vr_gun_scale.GetFloat();

	Vector vecEnd;
	VectorTransform( Vector( vr_gun_beam_end_x.GetFloat(), vr_gun_beam_end_y.GetFloat(), vr_gun_beam_end_z.GetFloat() ) * flScale, worldFromModel, vecEnd );

	m_vecBeamPoint[0] = vecEnd;
	m_bBeamPointsValid = false;
	for ( int i = 0; i < 3; i++ )
	{
		const int nBone = pGun ? pGun->LookupBone( s_pszProngBones[i] ) : -1;
		if ( !bOn || !pHdr || nBone < 0 )
		{
			if ( m_pGunBeam[i] )
				m_pGunBeam[i]->brightness = 0.0f;
			continue;
		}

		// Start point on the claw, in model space, swung open with the claw like the bone is.
		// Bone positions are stored in studiomdl's frame, turned 90 degrees about Z from model
		// space (see build_gun_model.py, pre_rot): model = ( -y, x, z ).
		const Vector &vecBonePos = pHdr->pBone( nBone )->pos;
		const Vector vecHinge( -vecBonePos.y, vecBonePos.x, vecBonePos.z );
		Vector vecLocal = GetVectorCvar( *s_pBeamClawVars[i] );
		if ( pGun->m_flProngAngle[i] != 0.0f )
		{
			Vector vecAxis = CrossProduct( Vector( 1, 0, 0 ), s_vecProngRadial[i] );
			VectorNormalize( vecAxis );
			matrix3x4_t rot;
			MatrixBuildRotationAboutAxis( vecAxis, pGun->m_flProngAngle[i], rot );
			Vector vecRotated;
			VectorRotate( vecLocal, rot, vecRotated );
			vecLocal = vecRotated;
		}
		Vector vecStart;
		VectorTransform( ( vecHinge + vecLocal ) * flScale, worldFromModel, vecStart );
		m_vecBeamPoint[i + 1] = vecStart;
		m_bBeamPointsValid = true;

		BeamInfo_t info;
		info.m_nType = TE_BEAMPOINTS;
		info.m_vecStart = vecStart;
		info.m_vecEnd = vecEnd;
		info.m_pszModelName = "sprites/grav_beam.vmt";	// PORTALGUN_BEAM_SPRITE
		info.m_flHaloScale = 0.0f;
		info.m_flLife = 0.0f;
		info.m_flWidth = 0.0f;
		info.m_flEndWidth = 2.0f * flScale;
		info.m_flFadeLength = 0.0f;
		info.m_flAmplitude = 16.0f;
		info.m_flBrightness = flBrightness;
		info.m_flSpeed = 150.0f;
		info.m_nStartFrame = 0;
		info.m_flFrameRate = 30.0f;
		info.m_flRed = 255.0f;
		info.m_flGreen = 255.0f;
		info.m_flBlue = 255.0f;
		info.m_nSegments = 8;
		info.m_bRenderable = true;
		info.m_nFlags = FBEAM_FOREVER;
		if ( !m_pGunBeam[i] )
			m_pGunBeam[i] = beams->CreateBeamPoints( info );
		else
			beams->UpdateBeamInfo( m_pGunBeam[i], info );
		if ( m_pGunBeam[i] )
		{
			m_pGunBeam[i]->brightness = flBrightness;
			m_pGunBeam[i]->m_bDrawInMainRender = true;
			m_pGunBeam[i]->m_bDrawInPortalRender = true;
		}
	}
}

C_BaseEntity *CClientVirtualReality::GetGunModelEntity() const
{
	C_BaseEntity *pGun = m_hGunModel.Get();
	return ( pGun && !pGun->IsEffectActive( EF_NODRAW ) ) ? pGun : NULL;
}

//-----------------------------------------------------------------------------
// The gun is a client-side model in the world (not the view model), so it sits
// exactly where the hand is in both eyes and through portals.
//-----------------------------------------------------------------------------
void CClientVirtualReality::UpdateGunModel( C_BasePlayer *pPlayer )
{
	bool bShow = pPlayer->IsAlive() && !pPlayer->GetVehicle() && m_bHandValid[GetGunHand()];
#ifdef PORTAL
	bShow = bShow && ( m_bCalibrating || dynamic_cast<C_WeaponPortalgun *>( pPlayer->GetActiveWeapon() ) != NULL );
#endif

	C_BaseAnimating *pGun = dynamic_cast<C_BaseAnimating *>( m_hGunModel.Get() );
	if ( !pGun )
	{
		if ( !bShow )
			return;
		static bool s_bWarned = false;
		if ( modelinfo->GetModelIndex( vr_gun_model.GetString() ) < 0 )
		{
			if ( !s_bWarned )
				VRLog( "Gun model %s is not precached", vr_gun_model.GetString() );
			s_bWarned = true;
			return;
		}
		pGun = new C_VRGunModel;
		if ( !pGun->InitializeAsClientEntity( vr_gun_model.GetString(), RENDER_GROUP_OPAQUE_ENTITY ) )
		{
			pGun->Release();
			return;
		}
		m_hGunModel = pGun;
	}

	if ( !bShow )
	{
		pGun->AddEffects( EF_NODRAW );
		UpdateGunBeams( NULL, m_WorldFromGunModel );
		return;
	}
	pGun->RemoveEffects( EF_NODRAW );

	matrix3x4_t worldFromModel;
	MatrixCopy( m_WorldFromGunModel, worldFromModel );
	UpdateGunAnimation( pPlayer, pGun, worldFromModel );	// may add recoil to the drawn pose
	UpdateGunBeams( pGun, worldFromModel );

	Vector vecOrigin;
	QAngle angles;
	MatrixAngles( worldFromModel, angles, vecOrigin );
	pGun->SetAbsOrigin( vecOrigin );
	pGun->SetAbsAngles( angles );
	if ( pGun->GetModelScale() != vr_gun_scale.GetFloat() )
		pGun->SetModelScale( vr_gun_scale.GetFloat() );
	pGun->InvalidateBoneCache();
	ClientLeafSystem()->RenderableChanged( pGun->RenderHandle() );
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
	RunDelayedCommands();

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return false;

	if ( !m_bTrackingInitialized )
		InitTracking( pPlayer );

	if ( vr_test_cfg.GetString()[0] && pPlayer->IsAlive() )
	{
		if ( m_flTestStartTime < 0.0f )
			m_flTestStartTime = gpGlobals->realtime;
		else if ( m_flTestStartTime > 0.0f && gpGlobals->realtime - m_flTestStartTime > vr_test_delay.GetFloat() )
		{
			m_flTestStartTime = 0.0f;	// once per map
			engine->ClientCmd_Unrestricted( VarArgs( "exec %s\n", vr_test_cfg.GetString() ) );
		}
	}

	UpdateSmoothTurn( pPlayer );
	UpdateWorldPoses( pPlayer );
	if ( !( vr_dbg_skip.GetInt() & 8 ) )
		UpdateMenu();
	if ( !( vr_dbg_skip.GetInt() & 1 ) )
		UpdateGunCalibration();
	UpdateBeamEdit();
	UpdateGunTransform();
	if ( !( vr_dbg_skip.GetInt() & 2 ) )
		UpdateGunModel( pPlayer );

	if ( vr_log_eye.GetBool() )
	{
		Vector vecEye = pPlayer->GetAbsOrigin() + m_vecHeadOffset;
		VRLog( "eye %.3f %.3f %.3f origin z %.3f frametime %.4f", vecEye.x, vecEye.y, vecEye.z, pPlayer->GetAbsOrigin().z, gpGlobals->frametime );
	}

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
	const bool bScreen = m_bMenuOpen || m_bCreditsShown;
	if ( !bScreen )
	{
		float flYawDelta = AngleDiff( pViewMiddle->angles[YAW], m_flHudYaw );
		if ( fabsf( flYawDelta ) > vr_hud_follow_angle.GetFloat() )
			m_flHudYaw = AngleNormalize( m_flHudYaw + flYawDelta - ( flYawDelta > 0.0f ? 1.0f : -1.0f ) * vr_hud_follow_angle.GetFloat() * 0.5f );
	}

	QAngle angHud( bScreen ? 0.0f : vr_hud_pitch.GetFloat(), m_flHudYaw, 0.0f );
	m_WorldFromHud.SetupMatrixOrgAngles( vec3_origin, angHud );

	int nScreenWide, nScreenTall;
	vgui::surface()->GetScreenSize( nScreenWide, nScreenTall );
	m_fHudHalfWidth = ( bScreen ? vr_menu_width.GetFloat() : vr_hud_width.GetFloat() ) * 0.5f;
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

	g_PortalVR.ResolveEye( eEye == STEREO_EYE_LEFT ? ISourceVirtualReality::VREye_Left : ISourceVirtualReality::VREye_Right );
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

	pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetInt( "$vertexcolor", 1 );
	pKV->SetInt( "$nocull", 1 );
	m_pControllerMaterial = materials->CreateMaterial( "__vr_controller", pKV );
	m_pControllerMaterial->IncrementReferenceCount();

	pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetInt( "$vertexcolor", 1 );
	pKV->SetInt( "$nocull", 1 );
	pKV->SetInt( "$nofog", 1 );
	m_pOverlayMaterial = materials->CreateMaterial( "__vr_overlay", pKV );
	m_pOverlayMaterial->IncrementReferenceCount();

	pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetString( "$basetexture", "sprites/glow01" );
	pKV->SetInt( "$vertexcolor", 1 );
	pKV->SetInt( "$additive", 1 );
	pKV->SetInt( "$nocull", 1 );
	pKV->SetInt( "$nofog", 1 );
	m_pGlowMaterial = materials->CreateMaterial( "__vr_gunglow", pKV );
	m_pGlowMaterial->IncrementReferenceCount();
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
	FreeGunBeams();	// before the engine clears all beams for the level change
	if ( m_hGunModel.Get() )
		m_hGunModel->Release();
	m_hGunModel = NULL;
	m_bCalibGrabbing = false;
	m_flTestStartTime = -1.0f;
	g_PortalVR.SetLoading( true );
	m_bTrackingInitialized = false;	// re-align with the player's facing in the next map
}

//-----------------------------------------------------------------------------
// HUD panel
//-----------------------------------------------------------------------------
float CClientVirtualReality::GetHUDDistance()
{
	return ( m_bMenuOpen || m_bCreditsShown ) ? vr_menu_distance.GetFloat() : vr_hud_distance.GetFloat();
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
	if ( !vr_hud_visible.GetBool() && !bMenuOpen && !m_bCreditsShown )
		return;

	CreateMaterials();

	Vector vHead, vUL, vUR, vLL, vLR;
	GetHUDBounds( &vHead, &vUL, &vUR, &vLL, &vLR );

	CMatRenderContextPtr pRenderContext( materials );
	if ( m_bCreditsShown )
	{
		// Credits: nothing but the screen.
		pRenderContext->ClearColor4ub( 0, 0, 0, 255 );
		pRenderContext->ClearBuffers( true, true );
	}
	IMaterial *pMaterial = ( bMenuOpen || m_bCreditsShown ) ? m_pHudMaterialOpaque : m_pHudMaterial;
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
// World overlays: SteamVR controller models, hand skeletons, calibration axes
//-----------------------------------------------------------------------------
static void AddSegment( CMeshBuilder &meshBuilder, const Vector &a, const Vector &b, float flWidth, const unsigned char *color );

static bool ShowOverlay( const ConVar &var, bool bCalibrating )
{
	return var.GetInt() == 1 || ( var.GetInt() == 2 && bCalibrating );
}

void CClientVirtualReality::DrawWorldOverlays()
{
	if ( !UseVR() || !m_bTrackingInitialized || ( vr_dbg_skip.GetInt() & 4 ) )
		return;
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer || !pPlayer->IsAlive() )
		return;

	CreateMaterials();
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	if ( ShowOverlay( vr_show_controllers, m_bCalibrating ) )
		DrawControllerModels();
	DrawGunGlow();
	if ( ShowOverlay( vr_show_skeleton, m_bCalibrating ) )
		DrawSkeletons();
	if ( m_bCalibrating )
	{
		DrawAxes( m_WorldFromHand[GetGunHand()], 4.0f );
		DrawAxes( m_WorldFromGunModel, 3.0f );

		// Aim laser: where portals will go.
		Vector vecAimStart, vecAimDir;
		GetGunAim( vecAimStart, vecAimDir );
		static const unsigned char s_Laser[4] = { 255, 220, 60, 255 };
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_pOverlayMaterial );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );
		AddSegment( meshBuilder, vecAimStart, vecAimStart + vecAimDir * 400.0f, 0.2f, s_Laser );
		meshBuilder.End();
		pMesh->Draw();
	}
	if ( m_bBeamEdit )
		DrawBeamEditMarkers();

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}

void CClientVirtualReality::ReleaseControllerModels()
{
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		ControllerModel_t &model = m_ControllerModel[i];
		if ( model.pMesh )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->DestroyStaticMesh( model.pMesh );
		}
		model.pMesh = NULL;
		model.nState = 0;
		model.szName[0] = 0;
	}
}

void CClientVirtualReality::DrawControllerModels()
{
	CMatRenderContextPtr pRenderContext( materials );
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		ControllerModel_t &model = m_ControllerModel[i];
		const char *pszName = g_PortalVR.GetRenderModelName( i );
		if ( Q_strcmp( pszName, model.szName ) )
		{
			if ( model.pMesh )
				pRenderContext->DestroyStaticMesh( model.pMesh );
			model.pMesh = NULL;
			model.nState = pszName[0] ? 1 : 0;
			Q_strncpy( model.szName, pszName, sizeof( model.szName ) );
		}

		if ( model.nState == 1 )
		{
			CUtlVector<VRRenderModelVertex_t> verts;
			CUtlVector<unsigned short> indices;
			int nResult = g_PortalVR.LoadRenderModel( model.szName, verts, indices );
			if ( nResult < 0 || ( nResult > 0 && ( verts.Count() == 0 || verts.Count() > 65535 ) ) )
			{
				model.nState = -1;
			}
			else if ( nResult > 0 )
			{
				model.pMesh = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_COLOR | VERTEX_TEXCOORD_SIZE( 0, 2 ),
					TEXTURE_GROUP_STATIC_VERTEX_BUFFER_OTHER, m_pControllerMaterial );
				CMeshBuilder meshBuilder;
				meshBuilder.Begin( model.pMesh, MATERIAL_TRIANGLES, verts.Count(), indices.Count() );
				for ( int v = 0; v < verts.Count(); v++ )
				{
					meshBuilder.Position3fv( verts[v].pos.Base() );
					meshBuilder.Normal3fv( verts[v].normal.Base() );
					meshBuilder.Color4ubv( verts[v].color );
					meshBuilder.TexCoord2f( 0, 0.0f, 0.0f );
					meshBuilder.AdvanceVertex();
				}
				for ( int n = 0; n < indices.Count(); n++ )
				{
					meshBuilder.Index( indices[n] );
					meshBuilder.AdvanceIndex();
				}
				meshBuilder.End();
				model.nState = 2;
			}
		}

		const VRTrackedPose_t &device = g_PortalVR.GetDevicePose( i );
		if ( model.nState != 2 || !device.bValid )
			continue;

		matrix3x4_t worldFromDevice;
		TrackingToWorld( device.mat, m_vecPoseOrigin, worldFromDevice );
		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->LoadMatrix( worldFromDevice );
		pRenderContext->Bind( m_pControllerMaterial );
		model.pMesh->Draw();
	}
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->LoadIdentity();
}

// Thin quads facing the viewer.
static void AddSegment( CMeshBuilder &meshBuilder, const Vector &a, const Vector &b, float flWidth, const unsigned char *color )
{
	Vector vecDir = b - a;
	Vector vecToEye = CurrentViewOrigin() - a;
	Vector vecSide = CrossProduct( vecDir, vecToEye );
	if ( VectorNormalize( vecSide ) < 1e-4f )
		vecSide.Init( 0, 0, 1 );
	vecSide *= flWidth * 0.5f;
	meshBuilder.Position3fv( ( a - vecSide ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 0, 0 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( a + vecSide ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 1, 0 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( b + vecSide ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 1, 1 ); meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( ( b - vecSide ).Base() ); meshBuilder.Color4ubv( color ); meshBuilder.TexCoord2f( 0, 0, 1 ); meshBuilder.AdvanceVertex();
}

void CClientVirtualReality::DrawSkeletons()
{
	CMatRenderContextPtr pRenderContext( materials );
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		const matrix3x4_t *pBones = g_PortalVR.GetSkeleton( i );
		if ( !pBones )
			continue;

		Vector pos[VR_SKELETON_BONE_COUNT];
		for ( int b = 0; b < VR_SKELETON_BONE_COUNT; b++ )
		{
			matrix3x4_t world;
			TrackingToWorld( pBones[b], m_vecPoseOrigin, world );
			MatrixGetColumn( world, 3, pos[b] );
		}

		static const unsigned char s_Color[VR_HAND_COUNT][4] = { { 80, 200, 255, 255 }, { 255, 160, 40, 255 } };
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_pOverlayMaterial );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, VR_SKELETON_BONE_COUNT );
		int nQuads = 0;
		for ( int b = 2; b < VR_SKELETON_BONE_COUNT; b++ )
		{
			int nParent = CPortalVR::GetSkeletonBoneParent( b );
			if ( nParent < 1 )
				continue;
			AddSegment( meshBuilder, pos[nParent], pos[b], 0.3f, s_Color[i] );
			nQuads++;
		}
		// Fill the rest of the reserved quads with degenerate ones.
		for ( ; nQuads < VR_SKELETON_BONE_COUNT; nQuads++ )
			AddSegment( meshBuilder, pos[1], pos[1], 0.0f, s_Color[i] );
		meshBuilder.End();
		pMesh->Draw();
	}
}

void CClientVirtualReality::DrawAxes( const matrix3x4_t &world, float flLength )
{
	static const unsigned char s_Axis[3][4] = { { 255, 40, 40, 255 }, { 40, 255, 40, 255 }, { 60, 60, 255, 255 } };
	Vector vecOrigin;
	MatrixGetColumn( world, 3, vecOrigin );

	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_pOverlayMaterial );
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 3 );
	for ( int a = 0; a < 3; a++ )
	{
		Vector vecAxis;
		MatrixGetColumn( world, a, vecAxis );
		AddSegment( meshBuilder, vecOrigin, vecOrigin + vecAxis * flLength, 0.25f, s_Axis[a] );
	}
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

	// End credits: shown like the menu, on a screen straight ahead in a black void.
#ifdef PORTAL
	const bool bCredits = g_bPortalRollingCredits;
#else
	const bool bCredits = false;
#endif
	if ( bCredits && !m_bCreditsShown )
		m_flHudYaw = m_angHead[YAW];
	m_bCreditsShown = bCredits;

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
	Vector vecDir, vecStart;
	GetGunAim( vecStart, vecDir );
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

	// The view model isn't drawn in VR (the hand-held gun is a world model, see
	// UpdateGunModel), but its attachments still anchor weapon effects: keep it on the gun.
	MatrixAngles( m_WorldFromGunModel, vmangles, vmorigin );
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
	if ( !pPlayer || !m_bTrackingInitialized || ( vr_dbg_skip.GetInt() & 32 ) )
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
	if ( m_bCalibrating || m_bBeamEdit )
		vecTurn.Init();	// the right stick turns the aim while calibrating
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
	// Smooth turning: UpdateSmoothTurn, every rendered frame.

	const bool bBothGrips = g_PortalVR.GetDigital( VRACTION_HAND_GRAB, VR_HAND_LEFT ).bDown && g_PortalVR.GetDigital( VRACTION_HAND_GRAB, VR_HAND_RIGHT ).bDown;
	if ( g_PortalVR.GetDigitalAny( VRACTION_RECENTER ).bPressed && !bBothGrips )	// both grips + stick click = gun calibration
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

	// Calibrating the gun: the controllers belong to the calibration, not the game.
	if ( m_bCalibrating || m_bBeamEdit )
		return;

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
	// Everything that acts on the game comes from the gun hand; the free hand only walks.
	if ( g_PortalVR.GetDigital( VRACTION_JUMP, gunHand ).bDown )
		cmd->buttons |= IN_JUMP;

	if ( g_PortalVR.GetDigital( VRACTION_CROUCH, gunHand ).bPressed )
		m_bCrouchToggled = !m_bCrouchToggled;
	bool bPhysicalCrouch = hmd.bValid && GetHeadHeight() < vr_crouch_height.GetFloat();
	m_bDuckRequested = m_bCrouchToggled || bPhysicalCrouch;
	if ( m_bDuckRequested )
		cmd->buttons |= IN_DUCK;

	// Gun grip: pick up / press buttons along the gun, press again to drop (server toggles).
	if ( g_PortalVR.GetDigital( VRACTION_GUN_GRAB, gunHand ).bDown )
		vr.buttons |= VRBTN_GUN_GRAB;


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
