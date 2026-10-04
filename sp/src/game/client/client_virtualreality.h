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
class C_BaseAnimating;
class C_BaseEntity;
class IMaterial;
class IMesh;

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

	// Re-center the play area on the player (hull under the head). Seated: also re-measures height.
	void Recenter();

	// Seated mode: extra height added to the head so it stands at Chell's eye height.
	float GetHeightOffset() const;
	// Head height above the play area floor, including the seated offset.
	float GetHeadHeight() const;
	void MeasureSeatedHeight();
	void UpdateDuckJumpOffset( C_BasePlayer *pPlayer );

	// World pose of a hand's grip (valid after ProcessCurrentTrackingState this frame).
	bool GetHandWorldPose( int hand, Vector &origin, QAngle &angles ) const;
	int GetGunHand() const;
	int GetFreeHand() const { return GetGunHand() == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT; }

	// Portal gun aim (world): muzzle origin and direction from the gun hand.
	bool GetGunAim( Vector &origin, Vector &direction ) const;

	float GetTrackingYaw() const { return m_flTrackingYaw; }
	void DebugTurn( float flDegrees ) { ApplyTurn( flDegrees ); }

	// World transform of the portal gun model (model space: +X along the barrel).
	const matrix3x4_t &GetWorldFromGunModel() const { return m_WorldFromGunModel; }

	// Controller models, hand skeletons and calibration helpers, drawn into the current
	// eye's view (called from CViewRender::DrawViewModels with the 3D view pushed).
	void DrawWorldOverlays();

	// Gun placement calibration: the free hand grabs the gun model and puts it where it
	// feels right on the gun hand; A saves it.
	void SetGunCalibration( bool bOn );
	bool IsCalibratingGun() const { return m_bCalibrating; }
	// Grab electricity editor: the sticks move the beam end points, A saves them.
	void SetBeamEdit( bool bOn );
	bool IsEditingBeams() const { return m_bBeamEdit; }
	// Places the gun on the gun hand from SteamVR's hand skeleton (fist around the controller).
	bool AutoPlaceGun( bool bVerbose );

	// The hand-held gun entity while it is shown (NULL otherwise). Portals ghost it.
	C_BaseEntity *GetGunModelEntity() const;

private:
	void UpdateWorldPoses( C_BasePlayer *pPlayer );
	void TrackingToWorld( const matrix3x4_t &trk, const Vector &vecPlayerOrigin, matrix3x4_t &world ) const;
	Vector TrackingOffsetToWorld( const Vector &trk ) const;	// offset from the hull center, world axes
	void ApplyTurn( float flDegrees );
	void UpdateSmoothTurn( C_BasePlayer *pPlayer );
	void InitTracking( C_BasePlayer *pPlayer );
	void CreateMaterials();
	void UpdateMenu();				// menu button + laser pointer on the menu panel
	void DrawLaser();
	void UpdateGunTransform();
	void UpdateGunModel( C_BasePlayer *pPlayer );
	void UpdateGunAnimation( C_BasePlayer *pPlayer, C_BaseAnimating *pGun, matrix3x4_t &worldFromModel );
	void DrawGunGlow();
	void UpdateGunCalibration();
	void SaveGunCalibration();
	void UpdateBeamEdit();
	void SaveBeamPositions();
	void UpdateGunBeams( C_BaseAnimating *pGun, const matrix3x4_t &worldFromModel );
	void FreeGunBeams();
	void DrawBeamEditMarkers();
	void DrawControllerModels();
	void DrawSkeletons();
	void DrawAxes( const matrix3x4_t &world, float flLength );
	void ReleaseControllerModels();
	bool GetPortalLerpRotation( Quaternion &q ) const;

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
	Vector			m_vecPoseOrigin;		// player origin the world poses were computed with
	float			m_flTestStartTime;
	float			m_flDuckJumpOffset;		// see UpdateDuckJumpOffset
	bool			m_bDuckRequested;		// this command asked to duck (button or real crouch)		// vr_test_cfg: -1 = waiting for spawn, 0 = done

	// vr_portal_view_mode 1: extra rotation after a portal that settles back to level
	bool			m_bPortalLerpActive;
	matrix3x4_t		m_matPortalLerp;		// this frame's extra rotation (around the head)
	Quaternion		m_qPortalLerpStart;
	float			m_flPortalLerpStartTime;

	// Portal gun
	matrix3x4_t		m_WorldFromGunModel;
	EHANDLE			m_hGunModel;
	bool			m_bGunAutoPlaced;
	bool			m_bCalibrating;
	bool			m_bCalibGrabbing;		// the free hand is holding the gun model
	matrix3x4_t		m_FreeFromGunModel;
	float			m_flCalibHintTime;
	bool			m_bBeamEdit;
	double			m_flLastSmoothTurnTime;
	int				m_nBeamEditTarget;		// 0 = end point (barrel), 1-3 = claw tips

	// SteamVR controller render models
	struct ControllerModel_t
	{
		char		szName[128];
		int			nState;					// 0 = not loaded, 1 = loading, 2 = ready, -1 = failed
		IMesh		*pMesh;
	};
	ControllerModel_t m_ControllerModel[VR_HAND_COUNT];
	IMaterial		*m_pControllerMaterial;
	IMaterial		*m_pOverlayMaterial;
	IMaterial		*m_pGlowMaterial;

	// Gun animation / glow
	IMaterial		*m_pGunMaterial;
	IMaterial		*m_pGunCoreMaterial;
	float			m_flGunFireTime;
	float			m_flGunLastNextAttack;
	float			m_flGunHoldBlend;
	bool			m_bGunWasHolding;
	Vector			m_vecGunGlow;
	struct Beam_t	*m_pGunBeam[3];			// grab electricity, claw tips -> muzzle
	Vector			m_vecBeamPoint[4];		// world: end point, then the claw tips (for the editor markers)
	bool			m_bBeamPointsValid;

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
	bool			m_bCreditsShown;		// end credits on the menu screen, black around it
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
