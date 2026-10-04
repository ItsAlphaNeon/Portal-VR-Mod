//========= Portal VR ==========================================================//
//
// Purpose: "VR Settings" window, opened from the main / pause menu (gamemenu.res:
//			"engine vr_settings"). Every control applies immediately (no Apply step),
//			which suits the laser pointer.
//
//=============================================================================//
#include "cbase.h"
#include "vr/vr_settingspanel.h"
#include "client_virtualreality.h"
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Slider.h>
#include "KeyValues.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

static const int s_nSnapAngles[] = { 15, 30, 45, 60, 90 };

class CVRSettingsPanel : public Frame
{
	DECLARE_CLASS_SIMPLE( CVRSettingsPanel, Frame );

public:
	CVRSettingsPanel( VPANEL parent );

	virtual void Activate();
	virtual void OnCommand( const char *pszCommand );
	virtual void OnClose();
	virtual void PerformLayout();

private:
	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnSliderMoved, "SliderMoved", panel );
	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel );

	Label *AddLabel( const char *pszText, int y );
	ComboBox *AddCombo( const char *pszLabel, int y, const char **ppszItems, int nItems );
	CheckButton *AddCheck( const char *pszText, int y );
	void LoadValues();
	void UpdateSpeedLabel();

	ComboBox	*m_pTurnMode;
	ComboBox	*m_pSnapAngle;
	Slider		*m_pTurnSpeed;
	Label		*m_pTurnSpeedValue;
	ComboBox	*m_pMoveDir;
	ComboBox	*m_pPortalView;
	CheckButton	*m_pSeated;
	CheckButton	*m_pHud;
	CheckButton	*m_pControllers;
	CheckButton	*m_pGunAnim;
	bool		m_bLoading;

	// Layout: vgui lays children out again after construction, so bounds are applied
	// in PerformLayout.
	struct Placement_t { Panel *pPanel; int x, y, w, h; };
	CUtlVector<Placement_t> m_Placements;
	int			m_nWide, m_nTall;
	void Place( Panel *pPanel, int x, int y, int w, int h )
	{
		Placement_t p = { pPanel, x, y, w, h };
		m_Placements.AddToTail( p );
		pPanel->SetBounds( x, y, w, h );
	}
};

static const int kLabelX = 20;
static const int kControlX = 230;
static const int kControlW = 230;
static const int kRowH = 30;

CVRSettingsPanel::CVRSettingsPanel( VPANEL parent ) : BaseClass( NULL, "VRSettingsPanel" )
{
	SetParent( parent );
	SetScheme( scheme()->LoadSchemeFromFile( "resource/SourceScheme.res", "SourceScheme" ) );
	SetProportional( false );
	SetTitle( "VR Settings", true );
	SetSizeable( false );
	SetMoveable( true );
	SetMinimizeButtonVisible( false );
	SetMaximizeButtonVisible( false );
	SetCloseButtonVisible( true );
	SetKeyBoardInputEnabled( true );
	SetMouseInputEnabled( true );
	m_bLoading = false;

	int y = 40;
	static const char *s_pszTurn[] = { "Snap turn", "Smooth turn" };
	m_pTurnMode = AddCombo( "Turning", y, s_pszTurn, 2 ); y += kRowH;

	static const char *s_pszSnap[] = { "15 degrees", "30 degrees", "45 degrees", "60 degrees", "90 degrees" };
	m_pSnapAngle = AddCombo( "Snap turn angle", y, s_pszSnap, ARRAYSIZE( s_pszSnap ) ); y += kRowH;

	AddLabel( "Smooth turn speed", y );
	m_pTurnSpeed = new Slider( this, "TurnSpeed" );
	Place( m_pTurnSpeed, kControlX, y, kControlW - 60, 24 );
	m_pTurnSpeed->SetRange( 30, 360 );
	m_pTurnSpeed->AddActionSignalTarget( this );
	m_pTurnSpeedValue = new Label( this, "TurnSpeedValue", "" );
	Place( m_pTurnSpeedValue, kControlX + kControlW - 55, y, 60, 24 );
	y += kRowH;

	static const char *s_pszMove[] = { "Where you look", "Where your left hand points" };
	m_pMoveDir = AddCombo( "Walk direction", y, s_pszMove, 2 ); y += kRowH;

	static const char *s_pszPortal[] = { "Instant, horizon stays level", "Original game (view rolls back)" };
	m_pPortalView = AddCombo( "Floor/ceiling portals", y, s_pszPortal, 2 ); y += kRowH + 6;

	m_pSeated = AddCheck( "Seated mode (your head height becomes Chell's eye height)", y ); y += kRowH;
	m_pHud = AddCheck( "Show HUD panel", y ); y += kRowH;
	m_pControllers = AddCheck( "Show controller models", y ); y += kRowH;
	m_pGunAnim = AddCheck( "Animated, glowing portal gun", y ); y += kRowH + 10;

	Button *pRecenter = new Button( this, "Recenter", "Recenter / measure height", this, "recenter" );
	Place( pRecenter, kLabelX, y, 200, 28 );
	Button *pCalibrate = new Button( this, "Calibrate", "Calibrate gun position", this, "calibrate" );
	Place( pCalibrate, kControlX, y, kControlW, 28 );
	y += 36;
	Button *pBeamEdit = new Button( this, "BeamEdit", "Edit grab electricity", this, "beamedit" );
	Place( pBeamEdit, kControlX, y, kControlW, 28 );
	y += 40;

	Button *pDone = new Button( this, "Done", "Done", this, "close" );
	Place( pDone, kControlX + kControlW - 100, y, 100, 28 );
	y += 44;

	m_nWide = kControlX + kControlW + kLabelX;
	m_nTall = y;
	SetSize( m_nWide, m_nTall );
	SetVisible( false );
}

Label *CVRSettingsPanel::AddLabel( const char *pszText, int y )
{
	Label *pLabel = new Label( this, NULL, pszText );
	Place( pLabel, kLabelX, y, kControlX - kLabelX - 10, 24 );
	return pLabel;
}

ComboBox *CVRSettingsPanel::AddCombo( const char *pszLabel, int y, const char **ppszItems, int nItems )
{
	AddLabel( pszLabel, y );
	ComboBox *pCombo = new ComboBox( this, NULL, nItems, false );
	for ( int i = 0; i < nItems; i++ )
		pCombo->AddItem( ppszItems[i], new KeyValues( "item", "index", i ) );
	Place( pCombo, kControlX, y, kControlW, 24 );
	pCombo->AddActionSignalTarget( this );
	return pCombo;
}

CheckButton *CVRSettingsPanel::AddCheck( const char *pszText, int y )
{
	CheckButton *pCheck = new CheckButton( this, NULL, pszText );
	Place( pCheck, kLabelX, y, kControlX + kControlW - kLabelX, 24 );
	pCheck->AddActionSignalTarget( this );
	return pCheck;
}

void CVRSettingsPanel::UpdateSpeedLabel()
{
	char szText[32];
	Q_snprintf( szText, sizeof( szText ), "%d/s", m_pTurnSpeed->GetValue() );
	m_pTurnSpeedValue->SetText( szText );
}

void CVRSettingsPanel::LoadValues()
{
	m_bLoading = true;
	ConVarRef vr_turn_mode( "vr_turn_mode" );
	ConVarRef vr_snap_turn_angle( "vr_snap_turn_angle" );
	ConVarRef vr_smooth_turn_speed( "vr_smooth_turn_speed" );
	ConVarRef vr_move_hand_relative( "vr_move_hand_relative" );
	ConVarRef vr_portal_view_mode( "vr_portal_view_mode" );
	ConVarRef vr_seated( "vr_seated" );
	ConVarRef vr_hud_visible( "vr_hud_visible" );
	ConVarRef vr_show_controllers( "vr_show_controllers" );
	ConVarRef vr_gun_anim( "vr_gun_anim" );

	m_pTurnMode->ActivateItemByRow( vr_turn_mode.GetInt() ? 1 : 0 );
	int nSnap = 2;
	for ( int i = 0; i < ARRAYSIZE( s_nSnapAngles ); i++ )
	{
		if ( abs( s_nSnapAngles[i] - vr_snap_turn_angle.GetInt() ) < abs( s_nSnapAngles[nSnap] - vr_snap_turn_angle.GetInt() ) )
			nSnap = i;
	}
	m_pSnapAngle->ActivateItemByRow( nSnap );
	m_pTurnSpeed->SetValue( vr_smooth_turn_speed.GetInt(), false );
	UpdateSpeedLabel();
	m_pMoveDir->ActivateItemByRow( vr_move_hand_relative.GetBool() ? 1 : 0 );
	m_pPortalView->ActivateItemByRow( vr_portal_view_mode.GetInt() == 1 ? 1 : 0 );
	m_pSeated->SetSelected( vr_seated.GetBool() );
	m_pHud->SetSelected( vr_hud_visible.GetBool() );
	m_pControllers->SetSelected( vr_show_controllers.GetInt() != 0 );
	m_pGunAnim->SetSelected( vr_gun_anim.GetBool() );
	m_bLoading = false;
}

void CVRSettingsPanel::PerformLayout()
{
	SetSize( m_nWide, m_nTall );
	BaseClass::PerformLayout();
	for ( int i = 0; i < m_Placements.Count(); i++ )
		m_Placements[i].pPanel->SetBounds( m_Placements[i].x, m_Placements[i].y, m_Placements[i].w, m_Placements[i].h );
}

void CVRSettingsPanel::Activate()
{
	LoadValues();
	int nWide, nTall;
	surface()->GetScreenSize( nWide, nTall );
	SetPos( ( nWide - GetWide() ) / 2, ( nTall - GetTall() ) / 2 );
	BaseClass::Activate();
}

void CVRSettingsPanel::OnTextChanged( Panel *panel )
{
	if ( m_bLoading )
		return;
	if ( panel == m_pTurnMode )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_turn_mode %d\n", m_pTurnMode->GetActiveItem() ) );
	else if ( panel == m_pSnapAngle )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_snap_turn_angle %d\n", s_nSnapAngles[clamp( m_pSnapAngle->GetActiveItem(), 0, (int)ARRAYSIZE( s_nSnapAngles ) - 1 )] ) );
	else if ( panel == m_pMoveDir )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_move_hand_relative %d\n", m_pMoveDir->GetActiveItem() ) );
	else if ( panel == m_pPortalView )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_portal_view_mode %d\n", m_pPortalView->GetActiveItem() ) );
}

void CVRSettingsPanel::OnSliderMoved( Panel *panel )
{
	if ( m_bLoading || panel != m_pTurnSpeed )
		return;
	engine->ClientCmd_Unrestricted( VarArgs( "vr_smooth_turn_speed %d\n", m_pTurnSpeed->GetValue() ) );
	UpdateSpeedLabel();
}

void CVRSettingsPanel::OnCheckButtonChecked( Panel *panel )
{
	if ( m_bLoading )
		return;
	if ( panel == m_pSeated )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_seated %d\n", m_pSeated->IsSelected() ? 1 : 0 ) );
	else if ( panel == m_pHud )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_hud_visible %d\n", m_pHud->IsSelected() ? 1 : 0 ) );
	else if ( panel == m_pControllers )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_show_controllers %d\n", m_pControllers->IsSelected() ? 1 : 0 ) );
	else if ( panel == m_pGunAnim )
		engine->ClientCmd_Unrestricted( VarArgs( "vr_gun_anim %d\n", m_pGunAnim->IsSelected() ? 1 : 0 ) );
}

void CVRSettingsPanel::OnCommand( const char *pszCommand )
{
	if ( !Q_stricmp( pszCommand, "recenter" ) )
	{
		engine->ClientCmd_Unrestricted( "vr_recenter\n" );
	}
	else if ( !Q_stricmp( pszCommand, "calibrate" ) )
	{
		Close();
		engine->ClientCmd_Unrestricted( "gameui_hide\nvr_gun_calibrate\n" );
	}
	else if ( !Q_stricmp( pszCommand, "beamedit" ) )
	{
		Close();
		engine->ClientCmd_Unrestricted( "gameui_hide\nvr_gun_beam_edit\n" );
	}
	else if ( !Q_stricmp( pszCommand, "close" ) )
	{
		Close();
	}
	else
	{
		BaseClass::OnCommand( pszCommand );
	}
}

void CVRSettingsPanel::OnClose()
{
	engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
	BaseClass::OnClose();
}

//-----------------------------------------------------------------------------
// Creation / console command
//-----------------------------------------------------------------------------
static CVRSettingsPanel *s_pVRSettingsPanel = NULL;

void VRSettingsPanel_Create( VPANEL parent )
{
	if ( !s_pVRSettingsPanel )
		s_pVRSettingsPanel = new CVRSettingsPanel( parent );
}

void VRSettingsPanel_Destroy()
{
	if ( s_pVRSettingsPanel )
	{
		s_pVRSettingsPanel->SetParent( (Panel *)NULL );
		delete s_pVRSettingsPanel;
		s_pVRSettingsPanel = NULL;
	}
}

CON_COMMAND( vr_settings, "Open the VR Settings window" )
{
	if ( s_pVRSettingsPanel )
		s_pVRSettingsPanel->Activate();
}
