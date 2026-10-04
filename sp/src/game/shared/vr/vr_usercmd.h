//========= Portal VR ==========================================================//
//
// Purpose: VR state carried from the client to the server in every CUserCmd.
//
// The client owns the tracking space (where the play area sits in the world and how
// it is rotated). Each command therefore carries poses already converted into world
// orientation, as offsets from the player's absolute origin, so the server never needs
// to know about tracking space.
//
//=============================================================================//
#ifndef VR_USERCMD_H
#define VR_USERCMD_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"

class bf_read;
class bf_write;

enum VRHand_t
{
	VR_HAND_LEFT = 0,
	VR_HAND_RIGHT,
	VR_HAND_COUNT
};

enum VRCmdFlags_t
{
	VRCMD_ACTIVE		= ( 1 << 0 ),	// player is in VR; the rest of the block is meaningful
	VRCMD_HMD_VALID		= ( 1 << 1 ),
	VRCMD_LEFT_VALID	= ( 1 << 2 ),
	VRCMD_RIGHT_VALID	= ( 1 << 3 ),
	VRCMD_LEFT_HANDED	= ( 1 << 4 ),	// the portal gun is held in the left hand
};

enum VRButtons_t
{
	VRBTN_HAND_GRAB		= ( 1 << 0 ),	// free hand grip: physical grab / pull
	VRBTN_GUN_GRAB		= ( 1 << 1 ),	// gun hand grip: hold object at the barrel
	VRBTN_USE			= ( 1 << 2 ),	// free hand trigger: press buttons
};

struct VRUserCmd_t
{
	VRUserCmd_t() { Reset(); }

	void Reset()
	{
		flags = 0;
		buttons = 0;
		hmdOffset.Init();
		hmdAngles.Init();
		aimOffset.Init();
		aimAngles.Init();
		roomscaleMove.Init();
		for ( int i = 0; i < VR_HAND_COUNT; i++ )
		{
			handOffset[i].Init();
			handAngles[i].Init();
			handVelocity[i].Init();
			handAngVelocity[i].Init();
		}
	}

	bool IsActive() const { return ( flags & VRCMD_ACTIVE ) != 0; }
	bool IsHmdValid() const { return ( flags & ( VRCMD_ACTIVE | VRCMD_HMD_VALID ) ) == ( VRCMD_ACTIVE | VRCMD_HMD_VALID ); }
	bool IsHandValid( int hand ) const { return ( flags & ( hand == VR_HAND_LEFT ? VRCMD_LEFT_VALID : VRCMD_RIGHT_VALID ) ) != 0; }
	int GunHand() const { return ( flags & VRCMD_LEFT_HANDED ) ? VR_HAND_LEFT : VR_HAND_RIGHT; }
	int FreeHand() const { return ( flags & VRCMD_LEFT_HANDED ) ? VR_HAND_RIGHT : VR_HAND_LEFT; }

	int		flags;
	int		buttons;

	// Head: world-oriented offset from the player's abs origin, and world angles.
	Vector	hmdOffset;
	QAngle	hmdAngles;

	// Hands (grip pose): world-oriented offset from the player's abs origin, and world angles.
	Vector	handOffset[VR_HAND_COUNT];
	QAngle	handAngles[VR_HAND_COUNT];

	// Portal gun muzzle: world-oriented offset from the player's abs origin, and aim angles.
	Vector	aimOffset;
	QAngle	aimAngles;

	// Hand velocities relative to the player (world axes, units/s and degrees/s), for throwing.
	Vector	handVelocity[VR_HAND_COUNT];
	Vector	handAngVelocity[VR_HAND_COUNT];

	// Horizontal world-space displacement the player physically walked this command.
	// Applied by game movement with collision before regular movement.
	Vector	roomscaleMove;
};

void WriteVRUserCmd( bf_write *buf, const VRUserCmd_t &to );
void ReadVRUserCmd( bf_read *buf, VRUserCmd_t &to );

#endif // VR_USERCMD_H
