//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for Portal.
//
//=============================================================================//

#include "cbase.h"
#include "portal_player.h"
#include "trains.h"
#include "weapon_portalgun.h"
#include "portal/weapon_physcannon.h"
#include "ai_basenpc.h"
#include "prop_portal_shared.h"
#include "vphysics/player_controller.h"
#include "physicsshadowclone.h"

LINK_ENTITY_TO_CLASS( player, CPortal_Player );

IMPLEMENT_SERVERCLASS_ST(CPortal_Player, DT_Portal_Player)
// These SendPropExcludes prevent animation from working with a singleplayer animstate implementation
//SendPropExclude( "DT_BaseAnimating", "m_flPlaybackRate" ),	
//SendPropExclude( "DT_BaseAnimating", "m_nSequence" ),
//SendPropExclude( "DT_BaseAnimating", "m_nNewSequenceParity" ),
//SendPropExclude( "DT_BaseAnimating", "m_nResetEventsParity" ),
//SendPropExclude( "DT_BaseEntity", "m_angRotation" ),
//SendPropExclude( "DT_BaseAnimatingOverlay", "overlay_vars" ),
//SendPropExclude( "DT_BaseFlex", "m_viewtarget" ),
//SendPropExclude( "DT_BaseFlex", "m_flexWeight" ),
//SendPropExclude( "DT_BaseFlex", "m_blinktoggle" ),
//
//// portal_playeranimstate and clientside animation takes care of these on the client
//SendPropExclude( "DT_ServerAnimationData" , "m_flCycle" ),	
//SendPropExclude( "DT_AnimTimeMustBeFirst" , "m_flAnimTime" ),

SendPropBool( SENDINFO( m_bHeldObjectOnOppositeSideOfPortal) ),
SendPropEHandle( SENDINFO( m_pHeldObjectPortal ) ),
SendPropBool( SENDINFO( m_bPitchReorientation ) ),
SendPropEHandle( SENDINFO( m_hPortalEnvironment ) ),
SendPropEHandle( SENDINFO( m_hSurroundingLiquidPortal ) ),
SendPropBool( SENDINFO( m_bSuppressingCrosshair ) ),
//SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),

END_SEND_TABLE()

BEGIN_DATADESC( CPortal_Player )

	DEFINE_SOUNDPATCH( m_pWooshSound ),

	DEFINE_FIELD( m_bHeldObjectOnOppositeSideOfPortal, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_pHeldObjectPortal, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bPitchReorientation, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hPortalEnvironment, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hSurroundingLiquidPortal, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bSuppressingCrosshair, FIELD_BOOLEAN ),

END_DATADESC()

ConVar sv_regeneration_wait_time ("sv_regeneration_wait_time", "1.0", FCVAR_REPLICATED );
ConVar sv_regeneration_enable("sv_regeneration_enable", "0", FCVAR_REPLICATED | FCVAR_ARCHIVE);

#define PORTALPLAYER_PHYSDAMAGE_SCALE 4.0f

extern ConVar sv_turbophysics;
extern ConVar physicsshadowupdate_render;

//----------------------------------------------------
// Player Physics Shadow
//----------------------------------------------------
#define VPHYS_MAX_DISTANCE		2.0
#define VPHYS_MAX_VEL			10
#define VPHYS_MAX_DISTSQR		(VPHYS_MAX_DISTANCE*VPHYS_MAX_DISTANCE)
#define VPHYS_MAX_VELSQR		(VPHYS_MAX_VEL*VPHYS_MAX_VEL)

CPortal_Player::CPortal_Player()
{
	m_bHeldObjectOnOppositeSideOfPortal = false;
	m_pHeldObjectPortal = 0;
	m_bIntersectingPortalPlane = false;
	m_bPitchReorientation = false;
	m_bSilentDropAndPickup = false;
	m_bSuppressingCrosshair = false;
}

CPortal_Player::~CPortal_Player( void )
{
}

void CPortal_Player::Precache( void )
{
	BaseClass::Precache();

	PrecacheScriptSound( "PortalPlayer.EnterPortal" );
	PrecacheScriptSound( "PortalPlayer.ExitPortal" );

	PrecacheScriptSound( "PortalPlayer.Woosh" );
	PrecacheScriptSound( "PortalPlayer.FallRecover" );

	PrecacheModel ( "sprites/glow01.vmt" );

	//PrecacheScriptSound( "NPC_Citizen.die" );
}

void CPortal_Player::CreateSounds()
{
	if ( !m_pWooshSound )
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );

		m_pWooshSound = controller.SoundCreate( filter, entindex(), "PortalPlayer.Woosh" );
		controller.Play( m_pWooshSound, 0, 100 );
	}
}

void CPortal_Player::StopLoopingSounds()
{
	if ( m_pWooshSound )
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		controller.SoundDestroy( m_pWooshSound );
		m_pWooshSound = NULL;
	}

	BaseClass::StopLoopingSounds();
}

//-----------------------------------------------------------------------------
// Purpose: Sets  specific defaults.
//-----------------------------------------------------------------------------
void CPortal_Player::Spawn(void)
{
	SetPlayerModel();

	BaseClass::Spawn();

	CreateSounds();

	pl.deadflag = false;
	RemoveSolidFlags( FSOLID_NOT_SOLID );

	RemoveEffects( EF_NODRAW );
	StopObserverMode();

//	GiveDefaultItems(); no need to do this

	m_nRenderFX = kRenderNormal;

	m_Local.m_iHideHUD = 0;

	AddFlag(FL_ONGROUND); // set the player on the ground at the start of the round.

	m_impactEnergyScale = PORTALPLAYER_PHYSDAMAGE_SCALE;

	RemoveFlag( FL_FROZEN );

	//m_iSpawnInterpCounter = (m_iSpawnInterpCounter + 1) % 8;

	m_Local.m_bDucked = false;

	SetPlayerUnderwater(false);

#ifdef PORTAL_MP
	PickTeam();
#endif
}

/*
I think it makes the player make rebel hurt sounds when hit
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::UpdateExpression( void )
{
	if ( !m_pExpresser )
		return;

	int iConcept = CONCEPT_CHELL_IDLE;
	if ( GetHealth() <= 0 )
	{
		iConcept = CONCEPT_CHELL_DEAD;
	}

	GetExpresser()->SetOuter( this );

	ClearExpression();
	AI_Response response;
	bool result = SpeakFindResponse( response, g_pszChellConcepts[iConcept] );
	if ( !result )
	{
		m_flExpressionLoopTime = gpGlobals->curtime + RandomFloat(30,40);
		return;
	}

	char const *szScene = response.GetResponsePtr();

	// Ignore updates that choose the same scene
	if ( m_iszExpressionScene != NULL_STRING && stricmp( STRING(m_iszExpressionScene), szScene ) == 0 )
		return;

	if ( m_hExpressionSceneEnt )
	{
		ClearExpression();
	}

	m_iszExpressionScene = AllocPooledString( szScene );
	float flDuration = InstancedScriptedScene( this, szScene, &m_hExpressionSceneEnt, 0.0, true, NULL );
	m_flExpressionLoopTime = gpGlobals->curtime + flDuration;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::ClearExpression( void )
{
	if ( m_hExpressionSceneEnt != NULL )
	{ 
		StopScriptedScene( this, m_hExpressionSceneEnt );
	}
	m_flExpressionLoopTime = gpGlobals->curtime;
}
*/

void CPortal_Player::PreThink( void )
{
	QAngle vOldAngles = GetLocalAngles();
	QAngle vTempAngles = GetLocalAngles();

	vTempAngles = EyeAngles();

	if ( vTempAngles[PITCH] > 180.0f )
	{
		vTempAngles[PITCH] -= 360.0f;
	}

	SetLocalAngles( vTempAngles );

	BaseClass::PreThink();

	SetLocalAngles( vOldAngles );
}

void CPortal_Player::PostThink( void )
{
	BaseClass::PostThink();

	// this needs to be here and not in CHL2_Player because otherwise it won't work on player's shadowclone
	QAngle angles = GetLocalAngles();
	angles[PITCH] = 0;
	SetLocalAngles( angles );

	// Regenerate heath after 3 seconds
	if (IsAlive() && GetHealth() < GetMaxHealth() && sv_regeneration_enable.GetBool())
	{
		// Color to overlay on the screen while the player is taking damage
		color32 hurtScreenOverlay = {64,0,0,64};

		if ( gpGlobals->curtime > m_fTimeLastHurt + sv_regeneration_wait_time.GetFloat() )
		{
			TakeHealth( 1, DMG_GENERIC );
			m_bIsRegenerating = true;

			if ( GetHealth() >= GetMaxHealth() )
			{
				m_bIsRegenerating = false;
			}
		}
		else
		{
			m_bIsRegenerating = false;
			if (gpGlobals->curtime < m_fTimeLastHurt + 1.0)
			{
				UTIL_ScreenFade(this, hurtScreenOverlay, 1.0f, 0.1f, FFADE_IN | FFADE_PURGE);
			}
		}
	}

	UpdatePortalPlaneSounds();
	UpdateWooshSounds();

	//if ( IsAlive() && m_flExpressionLoopTime >= 0 && gpGlobals->curtime > m_flExpressionLoopTime )
	//{
		// Random expressions need to be cleared, because they don't loop. So if we
		// pick the same one again, we want to restart it.
		//ClearExpression();
		//m_iszExpressionScene = NULL_STRING;
		//UpdateExpression();
	//}

	//UpdateSecondsTaken();

	// Try to fix the player if they're stuck
	if ( m_bStuckOnPortalCollisionObject )
	{
		Vector vForward = ((CProp_Portal*)m_hPortalEnvironment.Get())->m_vPrevForward;
		Vector vNewPos = GetAbsOrigin() + vForward * gpGlobals->frametime * -1000.0f;
		Teleport( &vNewPos, NULL, &vForward );
		m_bStuckOnPortalCollisionObject = false;
	}
}

void CPortal_Player::UpdatePortalPlaneSounds( void )
{
	CProp_Portal *pPortal = m_hPortalEnvironment;
	if ( pPortal && pPortal->m_bActivated )
	{
		Vector vVelocity;
		GetVelocity( &vVelocity, NULL );

		if ( !vVelocity.IsZero() )
		{
			Vector vMin, vMax;
			CollisionProp()->WorldSpaceAABB( &vMin, &vMax );

			Vector vEarCenter = ( vMax + vMin ) / 2.0f;
			Vector vDiagonal = vMax - vMin;

			if ( !m_bIntersectingPortalPlane )
			{
				vDiagonal *= 0.25f;

				if ( UTIL_IsBoxIntersectingPortal( vEarCenter, vDiagonal, pPortal ) )
				{
					m_bIntersectingPortalPlane = true;

					CPASAttenuationFilter filter( this );
					CSoundParameters params;
					if ( GetParametersForSound( "PortalPlayer.EnterPortal", params, NULL ) )
					{
						EmitSound_t ep( params );
						ep.m_nPitch = 80.0f + vVelocity.Length() * 0.03f;
						ep.m_flVolume = MIN( 0.3f + vVelocity.Length() * 0.00075f, 1.0f );

						EmitSound( filter, entindex(), ep );
					}
				}
			}
			else
			{
				vDiagonal *= 0.30f;

				if ( !UTIL_IsBoxIntersectingPortal( vEarCenter, vDiagonal, pPortal ) )
				{
					m_bIntersectingPortalPlane = false;

					CPASAttenuationFilter filter( this );
					CSoundParameters params;
					if ( GetParametersForSound( "PortalPlayer.ExitPortal", params, NULL ) )
					{
						EmitSound_t ep( params );
						ep.m_nPitch = 80.0f + vVelocity.Length() * 0.03f;
						ep.m_flVolume = MIN( 0.3f + vVelocity.Length() * 0.00075f, 1.0f );

						EmitSound( filter, entindex(), ep );
					}
				}
			}
		}
	}
	else if ( m_bIntersectingPortalPlane )
	{
		m_bIntersectingPortalPlane = false;

		CPASAttenuationFilter filter( this );
		CSoundParameters params;
		if ( GetParametersForSound( "PortalPlayer.ExitPortal", params, NULL ) )
		{
			EmitSound_t ep( params );
			Vector vVelocity;
			GetVelocity( &vVelocity, NULL );
			ep.m_nPitch = 80.0f + vVelocity.Length() * 0.03f;
			ep.m_flVolume = MIN( 0.3f + vVelocity.Length() * 0.00075f, 1.0f );

			EmitSound( filter, entindex(), ep );
		}
	}
}

void CPortal_Player::UpdateWooshSounds( void )
{
	if ( m_pWooshSound )
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		float fWooshVolume = GetAbsVelocity().Length() - MIN_FLING_SPEED;

		if ( fWooshVolume < 0.0f )
		{
			controller.SoundChangeVolume( m_pWooshSound, 0.0f, 0.1f );
			return;
		}

		fWooshVolume /= 2000.0f;
		if ( fWooshVolume > 1.0f )
			fWooshVolume = 1.0f;

		controller.SoundChangeVolume( m_pWooshSound, fWooshVolume, 0.1f );
		//		controller.SoundChangePitch( m_pWooshSound, fWooshVolume + 0.5f, 0.1f );
	}
}

extern int	gEvilImpulse101;
extern bool UTIL_ItemCanBeTouchedByPlayer(CBaseEntity *pItem, CBasePlayer *pPlayer);
//-----------------------------------------------------------------------------
// Purpose: Player reacts to bumping a weapon. 
// Input  : pWeapon - the weapon that the player bumped into.
// Output : Returns true if player picked up the weapon
//-----------------------------------------------------------------------------
bool CPortal_Player::BumpWeapon( CBaseCombatWeapon *pWeapon )
{
	CBaseCombatCharacter *pOwner = pWeapon->GetOwner();

	// Can I have this weapon type?
	if ( !IsAllowedToPickupWeapons() )
		return false;

	if ( pOwner || !Weapon_CanUse( pWeapon ) || !g_pGameRules->CanHavePlayerItem( this, pWeapon ) )
	{
		if ( gEvilImpulse101 )
		{
			UTIL_Remove( pWeapon );
		}
		return false;
	}

	// Act differently in the episodes
	if (hl2_episodic.GetBool())
	{
		// Don't let the player touch the item unless unobstructed
		if (!UTIL_ItemCanBeTouchedByPlayer(pWeapon, this) && !gEvilImpulse101)
			return false;
	}
	else
	{
		// Don't let the player fetch weapons through walls (use MASK_SOLID so that you can't pickup through windows)
		if (pWeapon->FVisible(this, MASK_SOLID) == false && !(GetFlags() & FL_NOTARGET))
			return false;
	}

	CWeaponPortalgun *pPickupPortalgun = dynamic_cast<CWeaponPortalgun*>( pWeapon );

	// ----------------------------------------
	// If I already have it just take the ammo
	// ----------------------------------------
	if (Weapon_OwnsThisType(pWeapon->GetClassname(), pWeapon->GetSubType()))
	{
		// If we picked up a second portal gun set the bool to alow secondary fire
		if ( pPickupPortalgun )
		{
			CWeaponPortalgun *pPortalGun = static_cast<CWeaponPortalgun*>( Weapon_OwnsThisType( pWeapon->GetClassname() ) );

			if ( pPickupPortalgun->CanFirePortal1() )
				pPortalGun->SetCanFirePortal1();

			if ( pPickupPortalgun->CanFirePortal2() )
				pPortalGun->SetCanFirePortal2();

			UTIL_Remove( pWeapon );
			return true;
		}

		if (Weapon_EquipAmmoOnly(pWeapon))
		{
			// Only remove me if I have no ammo left
			if (pWeapon->HasPrimaryAmmo())
				return false;

			UTIL_Remove(pWeapon);
			return true;
		}
		else
		{
			return false;
		}
	}
	// -------------------------
	// Otherwise take the weapon
	// -------------------------
	else
	{
		pWeapon->CheckRespawn();

		pWeapon->AddSolidFlags(FSOLID_NOT_SOLID);
		pWeapon->AddEffects(EF_NODRAW);

		Weapon_Equip(pWeapon);

		// If we're holding and object before picking up portalgun, drop it
		if (pPickupPortalgun)
		{
			ForceDropOfCarriedPhysObjects(GetPlayerHeldEntity(this));
		}

		if (IsInAVehicle())
		{
			pWeapon->Holster();
		}
		else
		{
#ifdef HL2_DLL

			if (IsX360())
			{
				CFmtStr hint;
				hint.sprintf("#valve_hint_select_%s", pWeapon->GetClassname());
				UTIL_HudHintText(this, hint.Access());
			}

			// Always switch to a newly-picked up weapon
			if (!PlayerHasMegaPhysCannon())
			{
				// If it uses clips, load it full. (this is the first time you've picked up this type of weapon)
				if (pWeapon->UsesClipsForAmmo1())
				{
					pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
				}

				Weapon_Switch(pWeapon);
			}
#endif
		}
		return true;
	}
}

void CPortal_Player::ShutdownUseEntity( void )
{
	ShutdownPickupController( m_hUseEntity );
}

const Vector& CPortal_Player::WorldSpaceCenter( ) const
{
	m_vWorldSpaceCenterHolder = GetAbsOrigin();
	m_vWorldSpaceCenterHolder.z += ( (IsDucked()) ? (VEC_DUCK_HULL_MAX_SCALED( this ).z) : (VEC_HULL_MAX_SCALED( this ).z) ) * 0.5f;
	return m_vWorldSpaceCenterHolder;
}

void CPortal_Player::Teleport( const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity )
{
	Vector oldOrigin = GetLocalOrigin();
	QAngle oldAngles = GetLocalAngles();
	BaseClass::Teleport( newPosition, newAngles, newVelocity );
	m_angEyeAngles = pl.v_angle;

	// teleportation player animstate code now lives here
	QAngle absangles = GetAbsAngles();
	m_pPlayerAnimState->m_angRender = absangles;
	m_pPlayerAnimState->m_angRender.x = m_pPlayerAnimState->m_angRender.z = 0.0f;
	// Snap the yaw pose parameter lerping variables to face new angles.
	m_pPlayerAnimState->m_flCurrentFeetYaw = m_pPlayerAnimState->m_flGoalFeetYaw = m_pPlayerAnimState->m_flEyeYaw = EyeAngles()[YAW];
}

void CPortal_Player::VPhysicsShadowUpdate( IPhysicsObject *pPhysics )
{
	if( m_hPortalEnvironment.Get() == NULL )
		return BaseClass::VPhysicsShadowUpdate( pPhysics );


	//below is mostly a cut/paste of existing CBasePlayer::VPhysicsShadowUpdate code with some minor tweaks to avoid getting stuck in stuff when in a portal environment
	if ( sv_turbophysics.GetBool() )
		return;

	Vector newPosition;

	bool physicsUpdated = m_pPhysicsController->GetShadowPosition( &newPosition, NULL ) > 0 ? true : false;

	// UNDONE: If the player is penetrating, but the player's game collisions are not stuck, teleport the physics shadow to the game position
	if ( pPhysics->GetGameFlags() & FVPHYSICS_PENETRATING )
	{
		CUtlVector<CBaseEntity *> list;
		PhysGetListOfPenetratingEntities( this, list );
		for ( int i = list.Count()-1; i >= 0; --i )
		{
			// filter out anything that isn't simulated by vphysics
			// UNDONE: Filter out motion disabled objects?
			if ( list[i]->GetMoveType() == MOVETYPE_VPHYSICS )
			{
				// I'm currently stuck inside a moving object, so allow vphysics to 
				// apply velocity to the player in order to separate these objects
				m_touchedPhysObject = true;
			}

			// if it's an NPC, tell them that the player is intersecting them
			CAI_BaseNPC *pNPC = list[i]->MyNPCPointer();
			if ( pNPC )
			{
				pNPC->PlayerPenetratingVPhysics();
			}
		}
	}

	bool bCheckStuck = false;
	if ( m_afPhysicsFlags & PFLAG_GAMEPHYSICS_ROTPUSH )
	{
		bCheckStuck = true;
		m_afPhysicsFlags &= ~PFLAG_GAMEPHYSICS_ROTPUSH;
	}

	if ( m_pPhysicsController->IsInContact() || (m_afPhysicsFlags & PFLAG_VPHYSICS_MOTIONCONTROLLER) )
	{
		m_touchedPhysObject = true;
	}

	if ( IsFollowingPhysics() )
	{
		m_touchedPhysObject = true;
	}

	if ( GetMoveType() == MOVETYPE_NOCLIP || pl.deadflag )
	{
		m_oldOrigin = GetAbsOrigin();
		return;
	}

	if ( phys_timescale.GetFloat() == 0.0f )
	{
		physicsUpdated = false;
	}

	if ( !physicsUpdated )
		return;

	IPhysicsObject *pPhysGround = GetGroundVPhysics();

	Vector newVelocity;
	pPhysics->GetPosition( &newPosition, 0 );
	m_pPhysicsController->GetShadowVelocity( &newVelocity );
	// assume vphysics gave us back a position without penetration
	Vector lastValidPosition = newPosition;

	if ( physicsshadowupdate_render.GetBool() )
	{
		NDebugOverlay::Box( GetAbsOrigin(), WorldAlignMins(), WorldAlignMaxs(), 255, 0, 0, 24, 15.0f );
		NDebugOverlay::Box( newPosition, WorldAlignMins(), WorldAlignMaxs(), 0,0,255, 24, 15.0f);
		//	NDebugOverlay::Box( newPosition, WorldAlignMins(), WorldAlignMaxs(), 0,0,255, 24, .01f);
	}

	Vector tmp = GetAbsOrigin() - newPosition;
	if ( !m_touchedPhysObject && !(GetFlags() & FL_ONGROUND) )
	{
		tmp.z *= 0.5f;	// don't care about z delta as much
	}

	float dist = tmp.LengthSqr();
	float deltaV = (newVelocity - GetAbsVelocity()).LengthSqr();

	float maxDistErrorSqr = VPHYS_MAX_DISTSQR;
	float maxVelErrorSqr = VPHYS_MAX_VELSQR;
	if ( IsRideablePhysics(pPhysGround) )
	{
		maxDistErrorSqr *= 0.25;
		maxVelErrorSqr *= 0.25;
	}

	// player's physics was frozen, try moving to the game's simulated position if possible
	if ( m_pPhysicsController->WasFrozen() )
	{
		m_bPhysicsWasFrozen = true;
		// check my position (physics object could have simulated into my position
		// physics is not very far away, check my position
		trace_t trace;
		UTIL_TraceEntity( this, GetAbsOrigin(), GetAbsOrigin(), MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( !trace.startsolid )
			return;

		// The physics shadow position is probably not in solid, try to move from there to the desired position
		UTIL_TraceEntity( this, newPosition, GetAbsOrigin(), MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( !trace.startsolid )
		{
			// found a valid position between the two?  take it.
			SetAbsOrigin( trace.endpos );
			UpdateVPhysicsPosition(trace.endpos, vec3_origin, 0);
			return;
		}

	}

	if ( dist >= maxDistErrorSqr || deltaV >= maxVelErrorSqr || (pPhysGround && !m_touchedPhysObject) )
	{
		if ( m_touchedPhysObject || pPhysGround )
		{
			// BUGBUG: Rewrite this code using fixed timestep
			if ( deltaV >= maxVelErrorSqr && !m_bPhysicsWasFrozen )
			{
				Vector dir = GetAbsVelocity();
				float len = VectorNormalize(dir);
				float dot = DotProduct( newVelocity, dir );
				if ( dot > len )
				{
					dot = len;
				}
				else if ( dot < -len )
				{
					dot = -len;
				}

				VectorMA( newVelocity, -dot, dir, newVelocity );

				if ( m_afPhysicsFlags & PFLAG_VPHYSICS_MOTIONCONTROLLER )
				{
					float val = Lerp( 0.1f, len, dot );
					VectorMA( newVelocity, val - len, dir, newVelocity );
				}

				if ( !IsRideablePhysics(pPhysGround) )
				{
					if ( !(m_afPhysicsFlags & PFLAG_VPHYSICS_MOTIONCONTROLLER ) && IsSimulatingOnAlternateTicks() )
					{
						newVelocity *= 0.5f;
					}
					ApplyAbsVelocityImpulse( newVelocity );
				}
			}

			trace_t trace;
			UTIL_TraceEntity( this, newPosition, newPosition, MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
			if ( !trace.allsolid && !trace.startsolid )
			{
				SetAbsOrigin( newPosition );
			}
		}
		else
		{
			bCheckStuck = true;
			trace_t trace;

			Ray_t ray;
			ray.Init( GetAbsOrigin(), GetAbsOrigin(), WorldAlignMins(), WorldAlignMaxs() );

			CTraceFilterSimple OriginalTraceFilter( this, COLLISION_GROUP_PLAYER_MOVEMENT );
			CTraceFilterTranslateClones traceFilter( &OriginalTraceFilter );
			UTIL_Portal_TraceRay_With( m_hPortalEnvironment, ray, MASK_PLAYERSOLID, &traceFilter, &trace );

			// current position is not ok, fixup
			if ( trace.allsolid || trace.startsolid )
			{
				//try again with new position
				ray.Init( newPosition, newPosition, WorldAlignMins(), WorldAlignMaxs() );
				UTIL_Portal_TraceRay_With( m_hPortalEnvironment, ray, MASK_PLAYERSOLID, &traceFilter, &trace );

				if( trace.startsolid == false )
				{
					SetAbsOrigin( newPosition );
				}
				else
				{
					if( !FindClosestPassableSpace( this, newPosition - GetAbsOrigin(), MASK_PLAYERSOLID ) )
					{
						// Try moving the player closer to the center of the portal
						CProp_Portal *pPortal = m_hPortalEnvironment.Get();
						newPosition += ( pPortal->GetAbsOrigin() - WorldSpaceCenter() ) * 0.1f;
						SetAbsOrigin( newPosition );

						DevMsg( "Hurting the player for FindClosestPassableSpaceFailure!" );

						// Deal 1 damage per frame... this will kill a player very fast, but allow for the above correction to fix some cases
						CTakeDamageInfo info( this, this, vec3_origin, vec3_origin, 1, DMG_CRUSH );
						OnTakeDamage( info );
					}
				}
			}
		}
	}
	else
	{
		if ( m_touchedPhysObject )
		{
			// check my position (physics object could have simulated into my position
			// physics is not very far away, check my position
			trace_t trace;
			UTIL_TraceEntity( this, GetAbsOrigin(), GetAbsOrigin(),
				MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );

			// is current position ok?
			if ( trace.allsolid || trace.startsolid )
			{
				// no use the final stuck check to move back to old if this stuck fix didn't work
				bCheckStuck = true;
				lastValidPosition = m_oldOrigin;
				SetAbsOrigin( newPosition );
			}
		}
	}

	if ( bCheckStuck )
	{
		trace_t trace;
		UTIL_TraceEntity( this, GetAbsOrigin(), GetAbsOrigin(), MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );

		// current position is not ok, fixup
		if ( trace.allsolid || trace.startsolid )
		{
			// STUCK!?!?!
			//Warning( "Checkstuck failed.  Stuck on %s!!\n", trace.m_pEnt->GetClassname() );
			SetAbsOrigin( lastValidPosition );
		}
	}
	m_oldOrigin = GetAbsOrigin();
	m_bPhysicsWasFrozen = false;
}

bool CPortal_Player::UseFoundEntity( CBaseEntity *pUseEntity )
{
	bool usedSomething = false;

	//!!!UNDONE: traceline here to prevent +USEing buttons through walls			
	int caps = pUseEntity->ObjectCaps();
	variant_t emptyVariant;

	if ( m_afButtonPressed & IN_USE )
	{
		// Robin: Don't play sounds for NPCs, because NPCs will allow respond with speech.
		if ( !pUseEntity->MyNPCPointer() )
		{
			EmitSound( "HL2Player.Use" );
		}
	}

	if ( ( (m_nButtons & IN_USE) && (caps & FCAP_CONTINUOUS_USE) ) ||
		( (m_afButtonPressed & IN_USE) && (caps & (FCAP_IMPULSE_USE|FCAP_ONOFF_USE)) ) )
	{
		if ( caps & FCAP_CONTINUOUS_USE )
			m_afPhysicsFlags |= PFLAG_USING;

		pUseEntity->AcceptInput( "Use", this, this, emptyVariant, USE_TOGGLE );

		usedSomething = true;
	}
	// UNDONE: Send different USE codes for ON/OFF.  Cache last ONOFF_USE object to send 'off' if you turn away
	else if ( (m_afButtonReleased & IN_USE) && (pUseEntity->ObjectCaps() & FCAP_ONOFF_USE) )	// BUGBUG This is an "off" use
	{
		pUseEntity->AcceptInput( "Use", this, this, emptyVariant, USE_TOGGLE );

		usedSomething = true;
	}

#if	HL2_SINGLE_PRIMARY_WEAPON_MODE

	//Check for weapon pick-up
	if ( m_afButtonPressed & IN_USE )
	{
		CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon *>(pUseEntity);

		if ( ( pWeapon != NULL ) && ( Weapon_CanSwitchTo( pWeapon ) ) )
		{
			//Try to take ammo or swap the weapon
			if ( Weapon_OwnsThisType( pWeapon->GetClassname(), pWeapon->GetSubType() ) )
			{
				Weapon_EquipAmmoOnly( pWeapon );
			}
			else
			{
				Weapon_DropSlot( pWeapon->GetSlot() );
				Weapon_Equip( pWeapon );
			}

			usedSomething = true;
		}
	}
#endif

	return usedSomething;
}

void CPortal_Player::PlayerUse( void )
{
	// Was use pressed or released?
	if ( ! ((m_nButtons | m_afButtonPressed | m_afButtonReleased) & IN_USE) )
		return;

	if ( m_afButtonPressed & IN_USE )
	{
		// Currently using a latched entity?
		if ( ClearUseEntity() )
		{
			return;
		}
		else
		{
			if ( m_afPhysicsFlags & PFLAG_DIROVERRIDE )
			{
				m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
				m_iTrain = TRAIN_NEW|TRAIN_OFF;
				return;
			}
			else
			{	// Start controlling the train!
				CBaseEntity *pTrain = GetGroundEntity();
				if ( pTrain && !(m_nButtons & IN_JUMP) && (GetFlags() & FL_ONGROUND) && (pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE) && pTrain->OnControls(this) )
				{
					m_afPhysicsFlags |= PFLAG_DIROVERRIDE;
					m_iTrain = TrainSpeed(pTrain->m_flSpeed, ((CFuncTrackTrain*)pTrain)->GetMaxSpeed());
					m_iTrain |= TRAIN_NEW;
					EmitSound( "HL2Player.TrainUse" );
					return;
				}
			}
		}

		// Tracker 3926:  We can't +USE something if we're climbing a ladder
		if ( GetMoveType() == MOVETYPE_LADDER )
		{
			return;
		}
	}

	if( m_flTimeUseSuspended > gpGlobals->curtime )
	{
		// Something has temporarily stopped us being able to USE things.
		// Obviously, this should be used very carefully.(sjb)
		return;
	}

	CBaseEntity *pUseEntity = FindUseEntity();

	bool usedSomething = false;

	// Found an object
	if ( pUseEntity )
	{
		SetHeldObjectOnOppositeSideOfPortal( false );

		// TODO: Removed because we no longer have ghost animatings. May need to rework this code.
		//// If we found a ghost animating then it needs to be held across a portal
		//CGhostAnimating *pGhostAnimating = dynamic_cast<CGhostAnimating*>( pUseEntity );
		//if ( pGhostAnimating )
		//{
		//	CProp_Portal *pPortal = NULL;

		//	CPortalSimulator *pPortalSimulator = CPortalSimulator::GetSimulatorThatOwnsEntity( pGhostAnimating->GetSourceEntity() );

		//	//HACKHACK: This assumes all portal simulators are a member of a prop_portal
		//	pPortal = (CProp_Portal *)(((char *)pPortalSimulator) - ((int)&(((CProp_Portal *)0)->m_PortalSimulator)));
		//	Assert( (&(pPortal->m_PortalSimulator)) == pPortalSimulator ); //doublechecking the hack

		//	if ( pPortal )
		//	{
		//		SetHeldObjectPortal( pPortal->m_hLinkedPortal );
		//		SetHeldObjectOnOppositeSideOfPortal( true );
		//	}
		//}
		usedSomething = UseFoundEntity( pUseEntity );
	}
	else if ( m_nVRUseSource != VR_GRAB_NONE )
	{
		// VR: nothing in reach of the hand / gun.
		if ( m_afButtonPressed & IN_USE )
			m_bPlayUseDenySound = true;
	}
	else 
	{
		Vector forward;
		EyeVectors( &forward, NULL, NULL );
		Vector start = EyePosition();

		Ray_t rayPortalTest;
		rayPortalTest.Init( start, start + forward * PLAYER_USE_RADIUS );

		float fMustBeCloserThan = 2.0f;

		CProp_Portal *pPortal = UTIL_Portal_FirstAlongRay( rayPortalTest, fMustBeCloserThan );

		if ( pPortal )
		{
			SetHeldObjectPortal( pPortal );
			pUseEntity = FindUseEntityThroughPortal();
		}

		if ( pUseEntity )
		{
			SetHeldObjectOnOppositeSideOfPortal( true );
			usedSomething = UseFoundEntity( pUseEntity );
		}
		else if ( m_afButtonPressed & IN_USE )
		{
			// Signal that we want to play the deny sound, unless the user is +USEing on a ladder!
			// The sound is emitted in ItemPostFrame, since that occurs after GameMovement::ProcessMove which
			// lets the ladder code unset this flag.
			m_bPlayUseDenySound = true;
		}
	}

	// Debounce the use key
	if ( usedSomething && pUseEntity )
	{
		m_Local.m_nOldButtons |= IN_USE;
		m_afButtonPressed &= ~IN_USE;
	}
}

void CPortal_Player::PlayerRunCommand(CUserCmd *ucmd, IMoveHelper *moveHelper)
{
	// Portal VR: remember the latest head/hand poses for EyePosition(), aiming and grabbing.
	m_VRCmd = ucmd->vr;
	VRProcessGrabButtons( ucmd );

	// In VR the client re-aims its play space itself after portalling, so the angles it
	// sends are never stale pre-portal angles.
	if( m_bFixEyeAnglesFromPortalling && m_VRCmd.IsActive() )
		m_bFixEyeAnglesFromPortalling = false;

	if( m_bFixEyeAnglesFromPortalling )
	{
		//the idea here is to handle the notion that the player has portalled, but they sent us an angle update before receiving that message.
		//If we don't handle this here, we end up sending back their old angles which makes them hiccup their angles for a frame
		float fOldAngleDiff = fabs( AngleDistance( ucmd->viewangles.x, m_qPrePortalledViewAngles.x ) );
		fOldAngleDiff += fabs( AngleDistance( ucmd->viewangles.y, m_qPrePortalledViewAngles.y ) );
		fOldAngleDiff += fabs( AngleDistance( ucmd->viewangles.z, m_qPrePortalledViewAngles.z ) );

		float fCurrentAngleDiff = fabs( AngleDistance( ucmd->viewangles.x, pl.v_angle.x ) );
		fCurrentAngleDiff += fabs( AngleDistance( ucmd->viewangles.y, pl.v_angle.y ) );
		fCurrentAngleDiff += fabs( AngleDistance( ucmd->viewangles.z, pl.v_angle.z ) );

		if( fCurrentAngleDiff > fOldAngleDiff )
			ucmd->viewangles = TransformAnglesToWorldSpace( ucmd->viewangles, m_matLastPortalled.As3x4() );

		m_bFixEyeAnglesFromPortalling = false;
	}

	BaseClass::PlayerRunCommand( ucmd, moveHelper );

	VRFinishGrabButtons();
}

//-----------------------------------------------------------------------------
// Portal VR
//-----------------------------------------------------------------------------
Vector CPortal_Player::EyePosition( void )
{
	if ( m_VRCmd.IsHmdValid() && IsAlive() && !IsInAVehicle() )
		return GetAbsOrigin() + m_VRCmd.hmdOffset;
	return BaseClass::EyePosition();
}

bool CPortal_Player::GetVRAim( Vector &vecOrigin, Vector &vecDirection )
{
	if ( !m_VRCmd.IsActive() || !m_VRCmd.IsHandValid( m_VRCmd.GunHand() ) || !IsAlive() || IsInAVehicle() )
		return false;

	vecOrigin = GetAbsOrigin() + m_VRCmd.aimOffset;
	AngleVectors( m_VRCmd.aimAngles, &vecDirection );

	// Don't let the gun shoot from inside a wall it is poking through: if anything solid is
	// between the eye and the muzzle, fire from the eye along the same direction.
	trace_t tr;
	Vector vecEye = EyePosition();
	UTIL_TraceLine( vecEye, vecOrigin, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction < 1.0f )
		vecOrigin = vecEye;
	return true;
}

//-----------------------------------------------------------------------------
// Portal VR: grabbing
//
// The free-hand grip grabs what the hand touches (or pulls what it points at) and
// holds it in the hand; releasing the grip lets go with the hand's velocity.
// The gun-hand grip picks up what the gun points at and holds it in front of the
// barrel, like the original. The free-hand trigger presses buttons.
// Everything goes through the stock +use pickup code; we only decide what gets
// used, from where, and when.
//-----------------------------------------------------------------------------
ConVar vr_grab_radius( "vr_grab_radius", "10", FCVAR_REPLICATED, "How close (units) the hand must be to grab something." );
ConVar vr_pull_distance( "vr_pull_distance", "96", FCVAR_REPLICATED, "How far (units) the free hand can point to pull an object." );
ConVar vr_gun_grab_distance( "vr_gun_grab_distance", "128", FCVAR_REPLICATED, "How far (units) the gun can reach to pick something up." );
ConVar vr_throw_max_speed( "vr_throw_max_speed", "450", FCVAR_REPLICATED, "Maximum speed (units/s) of a thrown object." );
ConVar vr_throw_scale( "vr_throw_scale", "1.0", FCVAR_REPLICATED, "Multiplier on hand velocity when throwing." );

bool CPortal_Player::GetVRHandMatrix( int hand, matrix3x4_t &worldFromHand )
{
	if ( !m_VRCmd.IsActive() || !m_VRCmd.IsHandValid( hand ) )
		return false;
	AngleMatrix( m_VRCmd.handAngles[hand], GetAbsOrigin() + m_VRCmd.handOffset[hand], worldFromHand );
	return true;
}

void CPortal_Player::VRProcessGrabButtons( CUserCmd *ucmd )
{
	const int nButtons = m_VRCmd.IsActive() ? m_VRCmd.buttons : 0;
	const int nPressed = nButtons & ~m_nVRPrevButtons;
	const int nReleased = m_nVRPrevButtons & ~nButtons;
	m_nVRPrevButtons = nButtons;
	m_nVRUseSource = VR_GRAB_NONE;
	m_nVRPendingGrabMode = VR_GRAB_NONE;

	if ( !m_VRCmd.IsActive() )
	{
		m_nVRGrabMode = VR_GRAB_NONE;
		return;
	}

	CBaseEntity *pHeld = GetPlayerHeldEntity( this );
	if ( pHeld )
	{
		bool bDrop = false;
		if ( m_nVRGrabMode == VR_GRAB_HAND && ( nReleased & VRBTN_HAND_GRAB ) )
		{
			bDrop = true;
			m_hVRThrowObject = pHeld;
		}
		else if ( m_nVRGrabMode == VR_GRAB_GUN && ( nReleased & VRBTN_GUN_GRAB ) )
		{
			bDrop = true;
		}
		else if ( m_nVRGrabMode == VR_GRAB_NONE && ( nPressed & ( VRBTN_HAND_GRAB | VRBTN_GUN_GRAB ) ) )
		{
			bDrop = true; // picked up some other way (e.g. keyboard +use)
		}

		if ( bDrop )
			ucmd->buttons |= IN_USE;	// a +use press while holding drops
		return;
	}

	if ( nPressed & VRBTN_HAND_GRAB )
	{
		m_nVRUseSource = VR_GRAB_HAND;
		m_nVRPendingGrabMode = VR_GRAB_HAND;
		ucmd->buttons |= IN_USE;
	}
	else if ( nPressed & VRBTN_GUN_GRAB )
	{
		m_nVRUseSource = VR_GRAB_GUN;
		m_nVRPendingGrabMode = VR_GRAB_GUN;
		ucmd->buttons |= IN_USE;
	}
	else if ( nPressed & VRBTN_USE )
	{
		m_nVRUseSource = VR_GRAB_HAND;
		m_nVRPendingGrabMode = VR_GRAB_HAND;
		ucmd->buttons |= IN_USE;
	}
}

void CPortal_Player::VRFinishGrabButtons()
{
	CBaseEntity *pHeld = GetPlayerHeldEntity( this );
	if ( pHeld && m_nVRPendingGrabMode != VR_GRAB_NONE )
	{
		// Just picked something up with a VR grab.
		m_nVRGrabMode = m_nVRPendingGrabMode;
		m_bVRGrabOffsetValid = false;
	}
	else if ( !pHeld )
	{
		m_nVRGrabMode = VR_GRAB_NONE;
	}
	m_nVRUseSource = VR_GRAB_NONE;
	m_nVRPendingGrabMode = VR_GRAB_NONE;

	// Throw: give the released object the hand's velocity.
	CBaseEntity *pThrown = m_hVRThrowObject.Get();
	m_hVRThrowObject = NULL;
	if ( pThrown && !pHeld )
	{
		IPhysicsObject *pPhys = pThrown->VPhysicsGetObject();
		const int hand = m_VRCmd.FreeHand();
		if ( pPhys && m_VRCmd.IsHandValid( hand ) )
		{
			Vector vecVel = GetAbsVelocity() + m_VRCmd.handVelocity[hand] * vr_throw_scale.GetFloat();
			float flSpeed = vecVel.Length();
			if ( flSpeed > vr_throw_max_speed.GetFloat() )
				vecVel *= vr_throw_max_speed.GetFloat() / flSpeed;

			// Angular velocity is given in the object's local space.
			Vector vecAngLocal;
			VectorIRotate( m_VRCmd.handAngVelocity[hand], pThrown->EntityToWorldTransform(), vecAngLocal );
			AngularImpulse angImpulse( vecAngLocal.x, vecAngLocal.y, vecAngLocal.z );
			pPhys->SetVelocity( &vecVel, &angImpulse );
		}
	}
}

// Something the hand can use: a pickup-able physics object or a usable entity (button).
static bool VRIsUsable( CBaseEntity *pEntity )
{
	if ( !pEntity || pEntity->IsWorld() || pEntity->IsPlayer() )
		return false;
	return ( pEntity->ObjectCaps() & ( FCAP_IMPULSE_USE | FCAP_CONTINUOUS_USE | FCAP_ONOFF_USE ) ) != 0;
}

CBaseEntity *CPortal_Player::VRFindHandEntity( const Vector &vecHand, const Vector &vecDir )
{
	// 1) Touching: the closest usable thing within reach of the hand.
	CBaseEntity *pList[64];
	int nCount = UTIL_EntitiesInSphere( pList, ARRAYSIZE( pList ), vecHand, vr_grab_radius.GetFloat(), 0 );
	CBaseEntity *pBest = NULL;
	float flBestDist = FLT_MAX;
	for ( int i = 0; i < nCount; i++ )
	{
		CBaseEntity *pEnt = pList[i];
		if ( pEnt == this || !VRIsUsable( pEnt ) )
			continue;
		Vector vecNearest;
		pEnt->CollisionProp()->CalcNearestPoint( vecHand, &vecNearest );
		float flDist = vecNearest.DistTo( vecHand );
		if ( flDist < flBestDist )
		{
			flBestDist = flDist;
			pBest = pEnt;
		}
	}
	if ( pBest )
		return pBest;

	// 2) Pointing: pull what the hand points at.
	trace_t tr;
	UTIL_TraceLine( vecHand, vecHand + vecDir * vr_pull_distance.GetFloat(), MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_PLAYERCLIP, this, COLLISION_GROUP_NONE, &tr );
	if ( tr.m_pEnt && VRIsUsable( tr.m_pEnt ) )
		return tr.m_pEnt;
	return NULL;
}

CBaseEntity *CPortal_Player::FindUseEntity( void )
{
	if ( m_nVRUseSource == VR_GRAB_NONE )
		return BaseClass::FindUseEntity();

	if ( m_nVRUseSource == VR_GRAB_GUN )
	{
		Vector vecOrigin, vecDir;
		if ( !GetVRAim( vecOrigin, vecDir ) )
			return NULL;
		trace_t tr;
		UTIL_TraceLine( vecOrigin, vecOrigin + vecDir * vr_gun_grab_distance.GetFloat(), MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_PLAYERCLIP, this, COLLISION_GROUP_NONE, &tr );
		return ( tr.m_pEnt && VRIsUsable( tr.m_pEnt ) ) ? tr.m_pEnt : NULL;
	}

	matrix3x4_t hand;
	if ( !GetVRHandMatrix( m_VRCmd.FreeHand(), hand ) )
		return NULL;
	Vector vecHand, vecDir;
	MatrixGetColumn( hand, 3, vecHand );
	MatrixGetColumn( hand, 0, vecDir );
	return VRFindHandEntity( vecHand, vecDir );
}

void CPortal_Player::CheatImpulseCommands( int iImpulse )
{
	switch ( iImpulse )
	{
	case 101:
	{
		if (sv_cheats->GetBool())
		{
			gEvilImpulse101 = true;

			CBasePlayer::EquipSuit();

			// Give the player everything!
			CBaseCombatCharacter::GiveAmmo(255, "Pistol");
			CBaseCombatCharacter::GiveAmmo(255, "AR2");
			CBaseCombatCharacter::GiveAmmo(5, "AR2AltFire");
			CBaseCombatCharacter::GiveAmmo(255, "SMG1");
			CBaseCombatCharacter::GiveAmmo(255, "Buckshot");
			CBaseCombatCharacter::GiveAmmo(3, "smg1_grenade");
			CBaseCombatCharacter::GiveAmmo(3, "rpg_round");
			CBaseCombatCharacter::GiveAmmo(5, "grenade");
			CBaseCombatCharacter::GiveAmmo(32, "357");
			CBaseCombatCharacter::GiveAmmo(16, "XBowBolt");
#ifdef HL2_EPISODIC
			CBaseCombatCharacter::GiveAmmo(5, "Hopwire");
#endif		
			GiveNamedItem("weapon_smg1");
			GiveNamedItem("weapon_frag");
			GiveNamedItem("weapon_crowbar");
			GiveNamedItem("weapon_pistol");
			GiveNamedItem("weapon_ar2");
			GiveNamedItem("weapon_shotgun");
			GiveNamedItem("weapon_physcannon");
			GiveNamedItem("weapon_bugbait");
			GiveNamedItem("weapon_rpg");
			GiveNamedItem("weapon_357");
			GiveNamedItem("weapon_crossbow");
#ifdef HL2_EPISODIC
			// GiveNamedItem( "weapon_magnade" );
#endif
			if (GetHealth() < 100)
			{
				TakeHealth(25, DMG_GENERIC);
			}
			
			CWeaponPortalgun *pPortalGun = static_cast<CWeaponPortalgun*>(GiveNamedItem("weapon_portalgun"));

			if (!pPortalGun)
			{
				pPortalGun = static_cast<CWeaponPortalgun*>(Weapon_OwnsThisType("weapon_portalgun"));
			}

			if (pPortalGun)
			{
				pPortalGun->SetCanFirePortal1();
				pPortalGun->SetCanFirePortal2();
			}

			gEvilImpulse101 = false;
		}
	}
	break;
	case 102:
	{
		CWeaponPortalgun *pPortalGun = static_cast<CWeaponPortalgun*>(GiveNamedItem("weapon_portalgun"));

		if (!pPortalGun)
		{
			pPortalGun = static_cast<CWeaponPortalgun*>(Weapon_OwnsThisType("weapon_portalgun"));
		}

		if (pPortalGun)
		{
			pPortalGun->SetCanFirePortal1();
			pPortalGun->SetCanFirePortal2();
		}
	}
	break;

	default:
		BaseClass::CheatImpulseCommands( iImpulse );
	}
}

int CPortal_Player::OnTakeDamage( const CTakeDamageInfo &inputInfo )
{
	CTakeDamageInfo inputInfoCopy( inputInfo );

	// If you shoot yourself, make it hurt
	if ( inputInfoCopy.GetAttacker() == this && inputInfoCopy.GetDamageType() == DMG_BULLET )
	{
		inputInfoCopy.ScaleDamage( 5.0f );
		//inputInfoCopy.ScaleDamageForce( 0.05f ); // I don't think there's a need for less push force
	}

	CBaseEntity *pAttacker = inputInfoCopy.GetAttacker();
	CBaseEntity *pInflictor = inputInfoCopy.GetInflictor();

	bool bIsTurret = false;

	if ( pAttacker && FClassnameIs( pAttacker, "npc_portal_turret_floor" ) )
		bIsTurret = true;

	// Refuse damage from prop_glados_core.
	if ( (pAttacker && FClassnameIs( pAttacker, "prop_glados_core" )) ||
		(pInflictor && FClassnameIs( pInflictor, "prop_glados_core" ))  )
	{
		inputInfoCopy.SetDamage(0.0f);
	}

	if ( bIsTurret && ( inputInfoCopy.GetDamageType() & DMG_BULLET ) )
	{
		Vector vLateralForce = inputInfoCopy.GetDamageForce();
		vLateralForce.z = 0.0f;

		// Push if the player is moving against the force direction
		if ( GetAbsVelocity().Dot( vLateralForce ) < 0.0f )
			ApplyAbsVelocityImpulse( vLateralForce );
	}
	else if ( ( inputInfoCopy.GetDamageType() & DMG_CRUSH ) )
	{
		if ( bIsTurret )
		{
			inputInfoCopy.SetDamage( inputInfoCopy.GetDamage() * 0.5f );
		}

		//if ( inputInfoCopy.GetDamage() >= 10.0f )
		//{
		//	EmitSound( "PortalPlayer.BonkYelp" );
		//}
	}
	//else if ( ( inputInfoCopy.GetDamageType() & DMG_SHOCK ) || ( inputInfoCopy.GetDamageType() & DMG_BURN ) )
	//{
	//	EmitSound( "PortalPortal.PainYelp" );
	//}

	int ret = BaseClass::OnTakeDamage( inputInfoCopy );

	// Copy the multidamage damage origin over what the base class wrote, because
	// that gets translated correctly though portals.
	m_DmgOrigin = inputInfo.GetDamagePosition();

	if ( GetHealth() < 100 )
	{
		m_fTimeLastHurt = gpGlobals->curtime;
	}

	return ret;
}

void CPortal_Player::ForceDuckThisFrame( void )
{
	if( m_Local.m_bDucked != true )
	{
		//m_Local.m_bDucking = false;
		m_Local.m_bDucked = true;
		ForceButtons( IN_DUCK );
		AddFlag( FL_DUCKING );
		SetVCollisionState( GetAbsOrigin(), GetAbsVelocity(), VPHYS_CROUCH );
	}
}

void CPortal_Player::ForceDropOfCarriedPhysObjects( CBaseEntity *pOnlyIfHoldingThis )
{
	m_bHeldObjectOnOppositeSideOfPortal = false;
	BaseClass::ForceDropOfCarriedPhysObjects( pOnlyIfHoldingThis );
}

//-----------------------------------------------------------------------------
// Purpose: Update the area bits variable which is networked down to the client to determine
//			which area portals should be closed based on visibility.
// Input  : *pvs - pvs to be used to determine visibility of the portals
//-----------------------------------------------------------------------------
void CPortal_Player::UpdatePortalViewAreaBits( unsigned char *pvs, int pvssize )
{
	Assert ( pvs );

	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if( iPortalCount == 0 )
		return;

	CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
	int *portalArea = (int *)stackalloc( sizeof( int ) * iPortalCount );
	bool *bUsePortalForVis = (bool *)stackalloc( sizeof( bool ) * iPortalCount );

	unsigned char *portalTempBits = (unsigned char *)stackalloc( sizeof( unsigned char ) * 32 * iPortalCount );
	COMPILE_TIME_ASSERT( (sizeof( unsigned char ) * 32) >= sizeof( ((CPlayerLocalData*)0)->m_chAreaBits ) );

	// setup area bits for these portals
	for ( int i = 0; i < iPortalCount; ++i )
	{
		CProp_Portal* pLocalPortal = pPortals[ i ];
		// Make sure this portal is active before adding it's location to the pvs
		if ( pLocalPortal && pLocalPortal->m_bActivated )
		{
			CProp_Portal* pRemotePortal = pLocalPortal->m_hLinkedPortal.Get();

			// Make sure this portal's linked portal is in the PVS before we add what it can see
			if ( pRemotePortal && pRemotePortal->m_bActivated && pRemotePortal->NetworkProp() && 
				pRemotePortal->NetworkProp()->IsInPVS( edict(), pvs, pvssize ) )
			{
				portalArea[ i ] = engine->GetArea( pPortals[ i ]->GetAbsOrigin() );

				if ( portalArea [ i ] >= 0 )
				{
					bUsePortalForVis[ i ] = true;
				}

				engine->GetAreaBits( portalArea[ i ], &portalTempBits[ i * 32 ], sizeof( unsigned char ) * 32 );
			}
		}
	}

	// Use the union of player-view area bits and the portal-view area bits of each portal
	for ( int i = 0; i < m_Local.m_chAreaBits.Count(); i++ )
	{
		for ( int j = 0; j < iPortalCount; ++j )
		{
			// If this portal is active, in PVS and it's location is valid
			if ( bUsePortalForVis[ j ]  )
			{
				m_Local.m_chAreaBits.Set( i, m_Local.m_chAreaBits[ i ] | portalTempBits[ (j * 32) + i ] );
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// AddPortalCornersToEnginePVS
// Subroutine to wrap the adding of portal corners to the PVS which is called once for the setup of each portal.
// input - pPortal: the portal we are viewing 'out of' which needs it's corners added to the PVS
//////////////////////////////////////////////////////////////////////////
void AddPortalCornersToEnginePVS( CProp_Portal* pPortal )
{
	Assert ( pPortal );

	if ( !pPortal )
		return;

	Vector vForward, vRight, vUp;
	pPortal->GetVectors( &vForward, &vRight, &vUp );

	// Center of the remote portal
	Vector ptOrigin			= pPortal->GetAbsOrigin();

	// Distance offsets to the different edges of the portal... Used in the placement checks
	Vector vToTopEdge = vUp * ( PORTAL_HALF_HEIGHT - PORTAL_BUMP_FORGIVENESS );
	Vector vToBottomEdge = -vToTopEdge;
	Vector vToRightEdge = vRight * ( PORTAL_HALF_WIDTH - PORTAL_BUMP_FORGIVENESS );
	Vector vToLeftEdge = -vToRightEdge;

	// Distance to place PVS points away from portal, to avoid being in solid
	Vector vForwardBump		= vForward * 1.0f;

	// Add center and edges to the engine PVS
	engine->AddOriginToPVS( ptOrigin + vForwardBump);
	engine->AddOriginToPVS( ptOrigin + vToTopEdge + vToLeftEdge + vForwardBump );
	engine->AddOriginToPVS( ptOrigin + vToTopEdge + vToRightEdge + vForwardBump );
	engine->AddOriginToPVS( ptOrigin + vToBottomEdge + vToLeftEdge + vForwardBump );
	engine->AddOriginToPVS( ptOrigin + vToBottomEdge + vToRightEdge + vForwardBump );
}

void PortalSetupVisibility( CBaseEntity *pPlayer, int area, unsigned char *pvs, int pvssize )
{
	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if( iPortalCount == 0 )
		return;

	CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
	for( int i = 0; i != iPortalCount; ++i )
	{
		CProp_Portal *pPortal = pPortals[i];

		if ( pPortal && pPortal->m_bActivated )
		{
			if ( pPortal->NetworkProp()->IsInPVS( pPlayer->edict(), pvs, pvssize ) )
			{
				if ( engine->CheckAreasConnected( area, pPortal->NetworkProp()->AreaNum() ) )
				{
					CProp_Portal *pLinkedPortal = static_cast<CProp_Portal*>( pPortal->m_hLinkedPortal.Get() );
					if ( pLinkedPortal )
					{
						AddPortalCornersToEnginePVS ( pLinkedPortal );
					}
				}
			}
		}
	}
}

void CPortal_Player::SetupVisibility( CBaseEntity *pViewEntity, unsigned char *pvs, int pvssize )
{
	BaseClass::SetupVisibility( pViewEntity, pvs, pvssize );

	int area = pViewEntity ? pViewEntity->NetworkProp()->AreaNum() : NetworkProp()->AreaNum();

	// At this point the EyePosition has been added as a view origin, but if we are currently stuck
	// in a portal, our EyePosition may return a point in solid. Find the reflected eye position
	// and use that as a vis origin instead.
	if ( m_hPortalEnvironment )
	{
		CProp_Portal *pPortal = NULL, *pRemotePortal = NULL;
		pPortal = m_hPortalEnvironment;
		pRemotePortal = pPortal->m_hLinkedPortal;

		if ( pPortal && pRemotePortal && pPortal->m_bActivated && pRemotePortal->m_bActivated )
		{		
			Vector ptPortalCenter = pPortal->GetAbsOrigin();
			Vector vPortalForward;
			pPortal->GetVectors( &vPortalForward, NULL, NULL );

			Vector eyeOrigin = EyePosition();
			Vector vEyeToPortalCenter = ptPortalCenter - eyeOrigin;

			float fPortalDist = vPortalForward.Dot( vEyeToPortalCenter );
			if( fPortalDist > 0.0f ) //eye point is behind portal
			{
				// Move eye origin to it's transformed position on the other side of the portal
				UTIL_Portal_PointTransform( pPortal->MatrixThisToLinked(), eyeOrigin, eyeOrigin );

				// Use this as our view origin (as this is where the client will be displaying from)
				engine->AddOriginToPVS( eyeOrigin );
				if ( !pViewEntity || pViewEntity->IsPlayer() )
				{
					area = engine->GetArea( eyeOrigin );
				}	
			}
		}
	}

	PortalSetupVisibility( this, area, pvs, pvssize );
}


#ifdef PORTAL_MP

CBaseEntity* CPortal_Player::EntSelectSpawnPoint( void )
{
	CBaseEntity *pSpot = NULL;
	CBaseEntity *pLastSpawnPoint = g_pLastSpawn;
	edict_t		*player = edict();
	const char *pSpawnpointName = "info_player_start";

	/*if ( HL2MPRules()->IsTeamplay() == true )
	{
	if ( GetTeamNumber() == TEAM_COMBINE )
	{
	pSpawnpointName = "info_player_combine";
	pLastSpawnPoint = g_pLastCombineSpawn;
	}
	else if ( GetTeamNumber() == TEAM_REBELS )
	{
	pSpawnpointName = "info_player_rebel";
	pLastSpawnPoint = g_pLastRebelSpawn;
	}

	if ( gEntList.FindEntityByClassname( NULL, pSpawnpointName ) == NULL )
	{
	pSpawnpointName = "info_player_deathmatch";
	pLastSpawnPoint = g_pLastSpawn;
	}
	}*/

	pSpot = pLastSpawnPoint;
	// Randomize the start spot
	for ( int i = random->RandomInt(1,5); i > 0; i-- )
		pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );
	if ( !pSpot )  // skip over the null point
		pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );

	CBaseEntity *pFirstSpot = pSpot;

	do 
	{
		if ( pSpot )
		{
			// check if pSpot is valid
			if ( g_pGameRules->IsSpawnPointValid( pSpot, this ) )
			{
				if ( pSpot->GetLocalOrigin() == vec3_origin )
				{
					pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );
					continue;
				}

				// if so, go to pSpot
				goto ReturnSpot;
			}
		}
		// increment pSpot
		pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );
	} while ( pSpot != pFirstSpot ); // loop if we're not back to the start

	// we haven't found a place to spawn yet,  so kill any guy at the first spawn point and spawn there
	if ( pSpot )
	{
		CBaseEntity *ent = NULL;
		for ( CEntitySphereQuery sphere( pSpot->GetAbsOrigin(), 128 ); (ent = sphere.GetCurrentEntity()) != NULL; sphere.NextEntity() )
		{
			// if ent is a client, kill em (unless they are ourselves)
			if ( ent->IsPlayer() && !(ent->edict() == player) )
				ent->TakeDamage( CTakeDamageInfo( GetContainingEntity(INDEXENT(0)), GetContainingEntity(INDEXENT(0)), 300, DMG_GENERIC ) );
		}
		goto ReturnSpot;
	}

	if ( !pSpot  )
	{
		pSpot = gEntList.FindEntityByClassname( pSpot, "info_player_start" );

		if ( pSpot )
			goto ReturnSpot;
	}

ReturnSpot:

	/*if ( HL2MPRules()->IsTeamplay() == true )
	{
	if ( GetTeamNumber() == TEAM_COMBINE )
	{
	g_pLastCombineSpawn = pSpot;
	}
	else if ( GetTeamNumber() == TEAM_REBELS ) 
	{
	g_pLastRebelSpawn = pSpot;
	}
	}*/

	g_pLastSpawn = pSpot;

	//m_flSlamProtectTime = gpGlobals->curtime + 0.5;

	return pSpot;
}

void CPortal_Player::PickTeam( void )
{
	//picks lowest or random
	CTeam *pCombine = g_Teams[TEAM_COMBINE];
	CTeam *pRebels = g_Teams[TEAM_REBELS];
	if ( pCombine->GetNumPlayers() > pRebels->GetNumPlayers() )
	{
		ChangeTeam( TEAM_REBELS );
	}
	else if ( pCombine->GetNumPlayers() < pRebels->GetNumPlayers() )
	{
		ChangeTeam( TEAM_COMBINE );
	}
	else
	{
		ChangeTeam( random->RandomInt( TEAM_COMBINE, TEAM_REBELS ) );
	}
}

#endif

CON_COMMAND( startadmiregloves, "Starts the admire gloves animation." )
{
	CPortal_Player *pPlayer = (CPortal_Player *)UTIL_GetCommandClient();
	if( pPlayer == NULL )
		pPlayer = GetPortalPlayer( 1 ); //last ditch effort

	if( pPlayer )
		pPlayer->StartAdmireGlovesAnimation();
}

//CON_COMMAND( displayportalplayerstats, "Displays current level stats for portals placed, steps taken, and seconds taken." )
//{
//	CPortal_Player *pPlayer = (CPortal_Player *)UTIL_GetCommandClient();
//	if( pPlayer == NULL )
//		pPlayer = GetPortalPlayer( 1 ); //last ditch effort
//
//	if( pPlayer )
//	{
//		int iMinutes = static_cast<int>( pPlayer->NumSecondsTaken() / 60.0f );
//		int iSeconds = static_cast<int>( pPlayer->NumSecondsTaken() ) % 60;
//
//		CFmtStr msg;
//		NDebugOverlay::ScreenText( 0.5f, 0.5f, msg.sprintf( "Portals Placed: %d\nSteps Taken: %d\nTime: %d:%d", pPlayer->NumPortalsPlaced(), pPlayer->NumStepsTaken(), iMinutes, iSeconds ), 255, 255, 255, 150, 5.0f );
//	}
//}

CON_COMMAND( startneurotoxins, "Starts the nerve gas timer." )
{
	CPortal_Player *pPlayer = (CPortal_Player *)UTIL_GetCommandClient();
	if( pPlayer == NULL )
		pPlayer = GetPortalPlayer( 1 ); //last ditch effort

	float fCoundownTime = 180.0f;

	if ( args.ArgC() > 1 )
		fCoundownTime = atof( args[ 1 ] );

	if( pPlayer )
		pPlayer->SetNeuroToxinDamageTime( fCoundownTime );
}
