//========= Portal VR ==========================================================//
//
// Purpose: OpenVR (SteamVR) backend for Portal VR.
//
// Implements the engine's ISourceVirtualReality interface (which the 2013 stereo
// rendering code in view.cpp / viewrender.cpp is already written against), plus the
// tracking, SteamVR Input and frame submission that the rest of the client uses.
//
// All poses handed out by this class are in "tracking space": OpenVR's standing
// universe converted to Source axes (+X forward, +Y left, +Z up) and game units.
// CClientVirtualReality maps tracking space into the world.
//
//=============================================================================//
#ifndef VR_OPENVR_H
#define VR_OPENVR_H
#ifdef _WIN32
#pragma once
#endif

#include "sourcevr/isourcevirtualreality.h"
#include "mathlib/vector2d.h"
#include "vr/vr_usercmd.h"
#include "tier1/utlvector.h"

class ITexture;
class IMaterialSystem;

struct VRTrackedPose_t
{
	bool		bValid;
	matrix3x4_t	mat;			// tracking space (Source axes, game units)
	Vector		vecVelocity;	// tracking space, units/s
	Vector		vecAngVelocity;	// tracking space, degrees/s (axis * rate)
};

// SteamVR hand skeleton (HandSkeletonBone in the SteamVR docs): 0 root, 1 wrist,
// 2-5 thumb, 6-10 index, 11-15 middle, 16-20 ring, 21-25 pinky, 26-30 aux.
#define VR_SKELETON_BONE_COUNT 31

// A vertex of a SteamVR controller render model (Source axes and units, model space).
struct VRRenderModelVertex_t
{
	Vector			pos;
	Vector			normal;
	unsigned char	color[4];
};

enum VRAction_t
{
	VRACTION_FIRE_PORTAL1 = 0,
	VRACTION_FIRE_PORTAL2,
	VRACTION_GUN_GRAB,
	VRACTION_HAND_GRAB,
	VRACTION_USE,
	VRACTION_JUMP,
	VRACTION_CROUCH,
	VRACTION_RECENTER,
	VRACTION_MENU,
	VRACTION_TOGGLE_HUD,
	VRACTION_QUICKSAVE,
	VRACTION_QUICKLOAD,

	VRACTION_DIGITAL_COUNT,
};

struct VRDigitalState_t
{
	bool bDown;
	bool bPressed;		// went down this frame
	bool bReleased;		// went up this frame
};

class CPortalVR : public ISourceVirtualReality
{
public:
	CPortalVR();

	//---------------------------------------------------------------------
	// IAppSystem (unused: this object is never registered with the engine)
	//---------------------------------------------------------------------
	virtual bool Connect( CreateInterfaceFn factory ) { return true; }
	virtual void Disconnect() {}
	virtual void *QueryInterface( const char *pInterfaceName ) { return NULL; }
	virtual InitReturnVal_t Init() { return INIT_OK; }
	virtual void Shutdown();

	//---------------------------------------------------------------------
	// ISourceVirtualReality
	//---------------------------------------------------------------------
	virtual bool ShouldRunInVR() { return m_bActive; }
	virtual bool IsHmdConnected() { return m_pSystem != NULL; }
	virtual void GetViewportBounds( VREye eEye, int *pnX, int *pnY, int *pnWidth, int *pnHeight );
	virtual bool DoDistortionProcessing( VREye eEye ) { return true; }
	virtual bool CompositeHud( VREye eEye, float ndcHudBounds[4], bool bDoUndistort, bool bBlackout, bool bTranslucent ) { return false; }
	virtual VMatrix GetMideyePose();
	virtual bool SampleTrackingState( float PlayerGameFov, float fPredictionSeconds );
	virtual bool GetDisplayBounds( VRRect_t *pRect ) { return false; }
	virtual bool GetEyeProjectionMatrix( VMatrix *pResult, VREye eEye, float zNear, float zFar, float fovScale );
	virtual VMatrix GetMidEyeFromEye( VREye eEye );
	virtual int GetVRModeAdapter() { return 0; }
	virtual bool WillDriftInYaw() { return false; }
	virtual void CreateRenderTargets( IMaterialSystem *pMaterialSystem );
	virtual void ShutdownRenderTargets();
	virtual ITexture *GetRenderTarget( VREye eEye, EWhichRenderTarget eWhich );
	virtual void GetRenderTargetFrameBufferDimensions( int &nWidth, int &nHeight );
	virtual bool Activate() { return m_pSystem != NULL; }
	virtual void Deactivate() {}
	virtual bool ShouldForceVRMode() { return false; }
	virtual void SetShouldForceVRMode() {}

	//---------------------------------------------------------------------
	// Portal VR
	//---------------------------------------------------------------------

	// Loads openvr_api.dll from the mod's bin folder and starts SteamVR. Safe to call
	// repeatedly; only the first call does anything. Returns true if VR is running.
	bool StartRuntime();
	bool IsActive() const { return m_bActive; }

	// Game units per real-world meter (vr_world_scale applied).
	float UnitsPerMeter() const;

	// Per-eye render size in pixels.
	int GetEyeWidth() const { return m_nEyeWidth; }
	int GetEyeHeight() const { return m_nEyeHeight; }

	// Called once per rendered frame before the views are set up: blocks on the
	// compositor (WaitGetPoses) and samples HMD + hand poses for this frame.
	void BeginFrame();

	// Called after an eye has been rendered: copies it into the shared submit texture.
	void ResolveEye( VREye eEye );

	// Called after both eyes have been rendered into the eye texture.
	void SubmitFrame();

	// While true, the game is loading or no 3D view is being drawn: we stop
	// submitting and let the compositor show a black fade.
	void SetLoading( bool bLoading );

	// Poses for the current frame (tracking space).
	const VRTrackedPose_t &GetHmdPose() const { return m_HmdPose; }
	const VRTrackedPose_t &GetHandPose( int hand ) const { return m_HandPose[hand]; }

	// Raw pose of the controller held in a hand: the origin of its SteamVR render model.
	const VRTrackedPose_t &GetDevicePose( int hand ) const { return m_DevicePose[hand]; }
	// SteamVR render model of the controller in a hand ("" until known).
	const char *GetRenderModelName( int hand ) const { return m_szRenderModel[hand]; }
	// Polls an async render model load: 1 = done (geometry filled), 0 = still loading, -1 = failed.
	int LoadRenderModel( const char *pszName, CUtlVector<VRRenderModelVertex_t> &verts, CUtlVector<unsigned short> &indices );

	// Live hand skeleton from SteamVR skeletal input (tracking space). NULL if the hand has none.
	const matrix3x4_t *GetSkeleton( int hand ) const { return m_bSkeletonValid[hand] ? m_SkeletonBones[hand] : NULL; }
	// SteamVR's "grip limit" reference skeleton (a fist closed around the controller), relative
	// to the hand's grip pose. False if the hand has no skeletal data this frame.
	bool GetGripFromFistSkeleton( int hand, matrix3x4_t *pBones );
	static int GetSkeletonBoneParent( int bone );

	// Head-relative eye transforms (tracking-space units, Source axes).
	const matrix3x4_t &GetHeadFromEye( VREye eEye ) const { return m_HeadFromEye[eEye]; }

	// Input. UpdateInput() is called once per frame; the getters are cheap.
	void UpdateInput();
	const VRDigitalState_t &GetDigital( VRAction_t action, int hand ) const { return m_Digital[action][hand]; }
	// Any hand.
	VRDigitalState_t GetDigitalAny( VRAction_t action ) const;
	Vector2D GetMoveStick() const { return m_vecMoveStick; }
	Vector2D GetTurnStick() const { return m_vecTurnStick; }
	float GetGripSqueeze( int hand ) const { return m_flGripSqueeze[hand]; }
	// Finger curls (thumb, index, middle, ring, pinky) from skeletal input, 0..1.
	const float *GetFingerCurls( int hand ) const { return m_flFingerCurl[hand]; }

	void TriggerHaptic( int hand, float flDuration, float flFrequency, float flAmplitude );

	// For the debug log / status command.
	void PrintStatus();

	ITexture *GetEyeTexture() const { return m_pEyeTexture; }

private:
	bool LoadRuntimeLibrary();
	void InitInput();
	void InstallTextureCaptureHook();

	bool m_bTriedStart;
	bool m_bActive;
	bool m_bLoading;
	bool m_bFrameStarted;

	void *m_pSystem;	// vr::IVRSystem*

	int m_nEyeWidth;
	int m_nEyeHeight;

	VRTrackedPose_t m_HmdPose;
	VRTrackedPose_t m_HandPose[VR_HAND_COUNT];
	VRTrackedPose_t m_DevicePose[VR_HAND_COUNT];
	unsigned int m_nDeviceIndex[VR_HAND_COUNT];
	char m_szRenderModel[VR_HAND_COUNT][128];
	VRTrackedPose_t m_SkeletonRoot[VR_HAND_COUNT];
	matrix3x4_t m_SkeletonBones[VR_HAND_COUNT][VR_SKELETON_BONE_COUNT];
	bool m_bSkeletonValid[VR_HAND_COUNT];
	bool m_bSkeletonRootFromDevice[VR_HAND_COUNT];
	matrix3x4_t m_HeadFromEye[2];

	// Render target that holds both eyes side by side (what SteamVR gets), and the per-eye
	// targets the eyes are rendered into.
	ITexture *m_pEyeTexture;
	ITexture *m_pEyeRT[2];

	// Input handles (vr::VRActionHandle_t / VRInputValueHandle_t are uint64).
	unsigned long long m_hActionSet;
	unsigned long long m_hDigital[VRACTION_DIGITAL_COUNT];
	unsigned long long m_hMove;
	unsigned long long m_hTurn;
	unsigned long long m_hGripSqueeze;
	unsigned long long m_hPose[VR_HAND_COUNT];
	unsigned long long m_hSkeleton[VR_HAND_COUNT];
	unsigned long long m_hHaptic;
	unsigned long long m_hHandSource[VR_HAND_COUNT];
	bool m_bInputReady;

	VRDigitalState_t m_Digital[VRACTION_DIGITAL_COUNT][VR_HAND_COUNT];
	Vector2D m_vecMoveStick;
	Vector2D m_vecTurnStick;
	float m_flGripSqueeze[VR_HAND_COUNT];
	float m_flFingerCurl[VR_HAND_COUNT][5];

	int m_nLastInputFrame;
	int m_nSubmitErrors;
};

extern CPortalVR g_PortalVR;

// Writes to portalvr/vr_log.txt (and the console in developer mode).
void VRLog( PRINTF_FORMAT_STRING const char *pFmt, ... ) FMTFUNCTION( 1, 2 );

#endif // VR_OPENVR_H
