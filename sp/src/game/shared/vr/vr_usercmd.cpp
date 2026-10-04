//========= Portal VR ==========================================================//
//
// Purpose: Serialization of the VR block of CUserCmd.
//
//=============================================================================//
#include "cbase.h"
#include "vr/vr_usercmd.h"
#include "tier1/bitbuf.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static void WriteVector( bf_write *buf, const Vector &v )
{
	buf->WriteFloat( v.x );
	buf->WriteFloat( v.y );
	buf->WriteFloat( v.z );
}

static void ReadVector( bf_read *buf, Vector &v )
{
	v.x = buf->ReadFloat();
	v.y = buf->ReadFloat();
	v.z = buf->ReadFloat();
}

static void WriteAngles( bf_write *buf, const QAngle &a )
{
	buf->WriteFloat( a.x );
	buf->WriteFloat( a.y );
	buf->WriteFloat( a.z );
}

static void ReadAngles( bf_read *buf, QAngle &a )
{
	a.x = buf->ReadFloat();
	a.y = buf->ReadFloat();
	a.z = buf->ReadFloat();
}

void WriteVRUserCmd( bf_write *buf, const VRUserCmd_t &to )
{
	if ( !to.IsActive() )
	{
		buf->WriteOneBit( 0 );
		return;
	}

	buf->WriteOneBit( 1 );
	buf->WriteUBitLong( to.flags, 8 );
	buf->WriteUBitLong( to.buttons, 8 );
	WriteVector( buf, to.hmdOffset );
	WriteAngles( buf, to.hmdAngles );
	WriteVector( buf, to.aimOffset );
	WriteAngles( buf, to.aimAngles );
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		WriteVector( buf, to.handOffset[i] );
		WriteAngles( buf, to.handAngles[i] );
		WriteVector( buf, to.handVelocity[i] );
		WriteVector( buf, to.handAngVelocity[i] );
	}
	WriteVector( buf, to.roomscaleMove );
}

void ReadVRUserCmd( bf_read *buf, VRUserCmd_t &to )
{
	to.Reset();
	if ( !buf->ReadOneBit() )
		return;

	to.flags = buf->ReadUBitLong( 8 );
	to.buttons = buf->ReadUBitLong( 8 );
	ReadVector( buf, to.hmdOffset );
	ReadAngles( buf, to.hmdAngles );
	ReadVector( buf, to.aimOffset );
	ReadAngles( buf, to.aimAngles );
	for ( int i = 0; i < VR_HAND_COUNT; i++ )
	{
		ReadVector( buf, to.handOffset[i] );
		ReadAngles( buf, to.handAngles[i] );
		ReadVector( buf, to.handVelocity[i] );
		ReadVector( buf, to.handAngVelocity[i] );
	}
	ReadVector( buf, to.roomscaleMove );

	// Never trust a malformed roomscale step to teleport the player.
	const float flMaxStep = 64.0f;
	if ( !to.roomscaleMove.IsValid() || to.roomscaleMove.Length() > flMaxStep )
		to.roomscaleMove.Init();
	to.roomscaleMove.z = 0.0f;
}
