//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The VR "brain" of the client: maps tracking space into the game world,
//			builds the per-eye views, turns VR input into user commands, places
//			the portal gun in the hand and draws the HUD as a panel in the world.
//
//			Portal VR: rewritten for 6DOF roomscale OpenVR (see vr/vr_openvr.h).
//			The public method names are kept from the 2013 SDK because the stock
//			client calls them (view.cpp, viewrender.cpp, in_main.cpp, ...).
//
//=============================================================================//

#ifndef CLIENTVIRTUALREALITY_H
#define CLIENTVIRTUALREALITY_H
#if defined( _WIN32 )
#pragma once
#endif

#include "tier3/tier3.h"
#include "iclientvirtualreality.h"
#include "view_shared.h"
#include "vr/vr_usercmd.h"

class CUserCmd;
class C_BasePlayer;
class IMaterial;

enum HeadtrackMovementMode_t
{
	HMM_SHOOTFACE_MOVEFACE = 0,		// Shoot from your face, move along your face.
	HMM_SHOOTFACE_MOVETORSO,		// Shoot from your face, move the direction your torso is facing.
	HMM_SHOOTMOUSE_MOVEFACE,		// Shoot from the mouse cursor which moves within the HUD, move along your face.
	HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE,	// Shoot from the mouse cursor which moves, bounded within the HUD, move along your face.
	HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEMOUSE,	// Shoot from the mouse cursor which moves, bounded within the HUD, move along your weapon (the "mouse")

	// The following are not intended to be user-selectable modes, they are used by e.g. followcam stuff.
	HMM_SHOOTMOVELOOKMOUSEFACE,		// Shoot & move & look along the mouse cursor (i.e. original unchanged gameplay), face just looks on top of that.
	HMM_SHOOTMOVEMOUSE_LOOKFACE,	// Shoot & move along the mouse cursor (i.e. original unchanged gameplay), face just looks.
	HMM_SHOOTMOVELOOKMOUSE,			// Shoot, move and look along the mouse cursor - HMD orientation is completely ignored!

	HMM_LAST,

	HMM_NOOVERRIDE = HMM_LAST		// Used as a retrun from ShouldOverrideHeadtrackControl(), not an actual mode.
};


//-----------------------------------------------------------------------------
// The implementation
//-----------------------------------------------------------------------------
class CClientVirtualReality: public CTier3AppSystem< IClientVirtualReality >
{
	typedef CTier3AppSystem< IClientVirtualReality > BaseClass;

public:

	CClientVirtualReality();
	~CClientVirtualReality();

	//
	// IAppSystem
	//
	virtual bool							Connect( CreateInterfaceFn factory );
	virtual void							Disconnect();
	virtual void *							QueryInterface( const char *pInterfaceName );
	virtual InitReturnVal_t					Init();
	virtual void							Shutdown();

	// Called when startup is complete
	void StartupComplete();

	//---------------------------------------------------------
	// IClientVirtualReality implementation
	//---------------------------------------------------------
	virtual void DrawMainMenu() OVERRIDE;

	//---------------------------------------------------------
	// Stock SDK hooks (called from view.cpp, viewrender.cpp, in_main.cpp, ...)
	//---------------------------------------------------------
	bool OverrideView ( CViewSetup *pViewMiddle, Vector *pViewModelOrigin, QAngle *pViewModelAngles, HeadtrackMovementMode_t hmmMovementOverride );
	bool OverrideStereoView( CViewSetup *pViewMiddle, CViewSetup *pViewLeft, CViewSetup *pViewRight );
	bool OverridePlayerMotion( float flInputSampleFrametime, const QAngle &oldAngles, const QAngle &curAngles, const Vector &curMotion, QAngle *pNewAngles, Vector *pNewMotion );
	bool OverrideWeaponHudAimVectors ( Vector *pAimOrigin, Vector *pAimDirection );
	bool CurrentlyZoomed() { return false; }
	void OverrideTorsoTransform( const Vector & position, const QAngle & angles ) {}
	void CancelTorsoTransformOverride( ) {}
	bool CanOverlayHudQuad() { return false; }
	void GetHUDBounds( Vector *pViewer, Vector *pUL, Vector *pUR, Vector *pLL, Vector *pLR );
	void RenderHUDQuad( bool bBlackout, bool bTranslucent );
	float GetZoomedModeMagnification() { return 1.0f; }
	bool ProcessCurrentTrackingState( float fGameFOV );
	const VMatrix &GetHudProjectionFromWorld() { return m_HudProjectionFromWorld; }
	void GetTorsoRelativeAim( Vector *pPosition, QAngle *pAngles );
	float GetHUDDistance();
	bool ShouldRenderHUDInWorld();
	const VMatrix & GetWorldFromMidEye() const { return m_WorldFromMidEye; }
	void OverrideViewModelTransform( Vector & vmorigin, QAngle & vmangles, bool bUseLargeOverride );
	void AlignTorsoAndViewToWeapon() {}
	void PostProcessFrame( StereoEye_t eEye );
	void OverlayHUDQuadWithUndistort( const CViewSetup &view, bool bDoUndistort, bool bBlackout, bool bTranslucent ) {}

	void Activate() {}
	void Deactivate() {}

	//---------------------------------------------------------
	// Portal VR
	//---------------------------------------------------------

	// Builds the VR part of a user command (poses, roomscale step, buttons, locomotion).
	// Called at the end of CInput::CreateMove.
	void CreateMove( float flFrametime, CUserCmd *cmd );

	// Called from CInput::ExtraMouseSample (between commands): just keeps the view angles on the HMD.
	void ExtraMouseSample( float flFrametime, QAngle &viewangles );

	// The local player was teleported through a portal: rotate the play space with it.
	void OnLocalPlayerPortalled( const VMatrix &matPortalTransform );

	// Map loads: fade the headset to black until the next frame is submitted.
	void LevelShutdown();

	// Draws the left eye into the desktop window.
	void DrawMirror( int nWidth, int nHeight );

	// Re-center the play area on the player (hull under the head).
	void Recenter();

	// World pose of a hand's grip (valid after ProcessCurrentTrackingState this frame).
	bool GetHandWorldPose( int hand, Vector &origin, QAngle &angles ) const;
	int GetGunHand() const;
	int GetFreeHand() const { return GetGunHand() == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT; }

	// Portal gun aim (world): muzzle origin and direction from the gun hand.
	bool GetGunAim( Vector &origin, Vector &direction ) const;

	float GetTrackingYaw() const { return m_flTrackingYaw; }

private:
	void UpdateWorldPoses( C_BasePlayer *pPlayer );
	void TrackingToWorld( const matrix3x4_t &trk, const Vector &vecPlayerOrigin, matrix3x4_t &world ) const;
	Vector TrackingOffsetToWorld( const Vector &trk ) const;	// offset from the hull center, world axes
	void ApplyTurn( float flDegrees );
	void InitTracking( C_BasePlayer *pPlayer );
	void CreateMaterials();
	void UpdateMenu();				// menu button + laser pointer on the menu panel
	void DrawLaser();

	// Play space placement: world yaw of tracking space, and the tracking-space point
	// (z = 0) that sits under the player's hull center.
	float			m_flTrackingYaw;
	Vector			m_vecTrackingCenter;
	bool			m_bTrackingInitialized;

	// This frame's world poses.
	VMatrix			m_WorldFromMidEye;
	matrix3x4_t		m_WorldFromHand[VR_HAND_COUNT];
	bool			m_bHandValid[VR_HAND_COUNT];
	Vector			m_vecHeadOffset;		// world-oriented offset of the head from the player's origin
	QAngle			m_angHead;

	// HUD panel
	VMatrix			m_WorldFromHud;
	VMatrix			m_HudProjectionFromWorld;
	Vector			m_vecHudViewer;
	float			m_fHudHalfWidth;
	float			m_fHudHalfHeight;
	float			m_flHudYaw;

	IMaterial		*m_pHudMaterial;
	IMaterial		*m_pHudMaterialOpaque;
	IMaterial		*m_pMirrorMaterial;
	IMaterial		*m_pLaserMaterial;

	// Menu pointer
	bool			m_bMenuOpen;
	bool			m_bPointerHit;
	Vector			m_vecPointerStart;
	Vector			m_vecPointerEnd;
	bool			m_bPointerButtonDown;

	// Input state
	bool			m_bCrouchToggled;
	bool			m_bSnapTurnReady;
	bool			m_bUseLatched;
};

extern CClientVirtualReality g_ClientVirtualReality;

#endif // CLIENTVIRTUALREALITY_H
