"""Generates the default SteamVR Input binding files for Portal VR.

Hand-dependent actions (fire, jump, crouch, gun grab) are bound on BOTH hands; the game reads
each one restricted to the gun hand (vr_gun_hand), so left-handed mode needs no separate
bindings. Menu, Recenter, ToggleHUD and the quick save/load are read from either hand.
HandGrab (grip on either hand) drives gun calibration and the grab electricity editor.

Default layout, gun hand = right:
  trigger            blue portal (and click in menus)
  orange portal      Frame: bumper. Others: the nearest free button (see each controller)
  grip               pick up / drop through the gun
  left stick         move;  right stick: turn;  right stick click: recenter
  jump / crouch      the A / B style face buttons where there are two, otherwise trackpad up/down

Run after editing:  python tools/gen_bindings.py
"""
import json
import os

OUT = os.path.join(os.path.dirname(__file__), '..', 'sp', 'game', 'portalvr', 'actions')
A = '/actions/portal/in/'
HANDS = ('left', 'right')


def button(hand, component, action, mode='button', inp='click'):
	return {'path': f'/user/hand/{hand}/input/{component}', 'mode': mode, 'inputs': {inp: {'output': A + action}}}


def both(component, action, **kw):
	return [button(h, component, action, **kw) for h in HANDS]


def joystick(hand, component, action):
	return {'path': f'/user/hand/{hand}/input/{component}', 'mode': 'joystick', 'inputs': {'position': {'output': A + action}}}


def dpad(hand, component, outputs, sub_mode='click'):
	return {'path': f'/user/hand/{hand}/input/{component}', 'mode': 'dpad', 'parameters': {'sub_mode': sub_mode},
			'inputs': {d: {'output': A + act} for d, act in outputs.items()}}


def trigger_fire():
	return [{'path': f'/user/hand/{h}/input/trigger', 'mode': 'trigger', 'inputs': {'click': {'output': A + 'FirePortal1'}}} for h in HANDS]


def analog_grip():
	"""Analog grips (Frame, Touch, Cosmos): click threshold = grab, value = squeeze."""
	s = []
	for h in HANDS:
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'trigger',
				  'inputs': {'click': {'output': A + 'HandGrab'}, 'pull': {'output': A + 'GripSqueeze'}}})
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'trigger', 'inputs': {'click': {'output': A + 'GunGrab'}}})
	return s


def button_grip():
	"""Grip buttons (Vive wands, WMR)."""
	s = []
	for h in HANDS:
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'button',
				  'inputs': {'click': {'output': A + 'HandGrab'}}})
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'button', 'inputs': {'click': {'output': A + 'GunGrab'}}})
	return s


def sticks(component='thumbstick'):
	"""Left stick moves, right stick turns, right stick click recenters."""
	return [joystick('left', component, 'Move'), joystick('right', component, 'Turn'),
			button('right', component, 'Recenter')]


def common(skeleton=True):
	b = {
		'poses': [
			{'output': A + 'PoseLeft', 'path': '/user/hand/left/pose/handgrip'},
			{'output': A + 'PoseRight', 'path': '/user/hand/right/pose/handgrip'},
		],
		'haptics': [
			{'output': '/actions/portal/out/Haptic', 'path': '/user/hand/left/output/haptic'},
			{'output': '/actions/portal/out/Haptic', 'path': '/user/hand/right/output/haptic'},
		],
		'sources': [],
	}
	if skeleton:
		b['skeleton'] = [
			{'output': A + 'SkeletonLeft', 'path': '/user/hand/left/input/skeleton/left'},
			{'output': A + 'SkeletonRight', 'path': '/user/hand/right/input/skeleton/right'},
		]
	return b


def frame_controller():
	# Steam Frame: A/B/menu/view on the right, bumpers above the triggers, clicky grips.
	b = common()
	s = b['sources']
	s += trigger_fire()
	s += both('trigger', 'Use', mode='trigger')
	s += both('bumper', 'FirePortal2')
	s += analog_grip()
	s += sticks()
	s.append(button('left', 'thumbstick', 'Crouch'))
	s.append(button('right', 'a', 'Jump'))
	s.append(button('right', 'b', 'Crouch'))
	s.append(button('right', 'menu', 'Menu'))
	s.append(button('left', 'view', 'ToggleHUD'))
	s.append(button('left', 'dpad_up', 'QuickSave'))
	s.append(button('left', 'dpad_down', 'QuickLoad'))
	return b


def knuckles():
	# Valve Index: A/B on each hand, trackpads, force-sensing grips, no usable menu button.
	#   right A = jump, right B = crouch, trackpad click = orange
	#   left B = menu, left A = toggle HUD
	b = common()
	s = b['sources']
	s += trigger_fire()
	s += both('trackpad', 'FirePortal2')
	for h in HANDS:
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'grab',
				  'inputs': {'grab': {'output': A + 'HandGrab'}}})
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'trigger',
				  'inputs': {'pull': {'output': A + 'GripSqueeze'}}})
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'grab',
				  'inputs': {'grab': {'output': A + 'GunGrab'}}})
	s += sticks()
	s.append(button('right', 'a', 'Jump'))
	s.append(button('right', 'b', 'Crouch'))
	s.append(button('left', 'a', 'Jump'))		# left-handed mode
	s.append(button('left', 'b', 'Menu'))
	return b


def oculus_touch():
	# Quest / Rift (Touch): right A/B, left X/Y + menu, analog grips.
	#   right A = jump, right B = orange, right stick click = crouch
	#   left stick click = recenter, left menu = menu, left X = toggle HUD, left Y = orange (left-handed)
	b = common()
	s = b['sources']
	s += trigger_fire()
	s.append(button('right', 'b', 'FirePortal2'))
	s.append(button('left', 'y', 'FirePortal2'))
	s += analog_grip()
	s.append(joystick('left', 'joystick', 'Move'))
	s.append(joystick('right', 'joystick', 'Turn'))
	s.append(button('right', 'joystick', 'Crouch'))
	s.append(button('left', 'joystick', 'Recenter'))
	s.append(button('right', 'a', 'Jump'))
	s.append(button('left', 'x', 'ToggleHUD'))
	s.append(button('left', 'application_menu', 'Menu'))
	return b


def vive_cosmos_controller():
	# Vive Cosmos: Touch-like face buttons plus bumpers (like the Frame).
	b = common()
	s = b['sources']
	s += trigger_fire()
	s += both('bumper', 'FirePortal2')
	s += analog_grip()
	s += [joystick('left', 'joystick', 'Move'), joystick('right', 'joystick', 'Turn'), button('right', 'joystick', 'Recenter')]
	s.append(button('right', 'a', 'Jump'))
	s.append(button('right', 'b', 'Crouch'))
	s.append(button('left', 'x', 'ToggleHUD'))
	s.append(button('left', 'application_menu', 'Menu'))
	return b


def vive_controller():
	# Vive wands: trackpads instead of sticks, one menu button per hand, grip buttons.
	#   left trackpad = move (click = recenter); right trackpad = turn,
	#   right trackpad click up = jump, down = crouch; right menu = orange; left menu = menu
	b = common(skeleton=False)
	s = b['sources']
	s += trigger_fire()
	s += button_grip()
	s.append(joystick('left', 'trackpad', 'Move'))
	s.append(button('left', 'trackpad', 'Recenter'))
	s.append(joystick('right', 'trackpad', 'Turn'))
	s.append(dpad('right', 'trackpad', {'north': 'Jump', 'south': 'Crouch'}))
	s.append(dpad('left', 'trackpad', {'north': 'Jump', 'south': 'Crouch'}))	# left-handed mode
	s.append(button('right', 'application_menu', 'FirePortal2'))
	s.append(button('left', 'application_menu', 'Menu'))
	return b


def holographic_controller():
	# Windows Mixed Reality (incl. Reverb G1): thumbsticks + trackpads, menu buttons, grip buttons.
	#   sticks move/turn (right stick click = recenter); right trackpad click up = jump, down = crouch;
	#   right menu = orange; left menu = menu; left trackpad click = toggle HUD
	b = common(skeleton=False)
	s = b['sources']
	s += trigger_fire()
	s += button_grip()
	s += sticks('joystick')
	s.append(dpad('right', 'trackpad', {'north': 'Jump', 'south': 'Crouch'}))
	s.append(dpad('left', 'trackpad', {'north': 'Jump', 'south': 'Crouch'}))	# left-handed mode
	s.append(button('right', 'application_menu', 'FirePortal2'))
	s.append(button('left', 'application_menu', 'Menu'))
	return b


def hpmotioncontroller():
	# HP Reverb G2: Touch-like (A/B right, X/Y left, menu on the left).
	b = oculus_touch()
	return b


CONTROLLERS = [
	('frame_controller', frame_controller, 'Steam Frame', 'gun trigger = blue, gun bumper = orange, grip = pick up'),
	('knuckles', knuckles, 'Valve Index', 'trigger = blue, trackpad click = orange, grip = pick up, A = jump, B = crouch, left B = menu'),
	('oculus_touch', oculus_touch, 'Quest / Rift', 'trigger = blue, B = orange, grip = pick up, A = jump, right stick click = crouch, left stick click = recenter'),
	('vive_cosmos_controller', vive_cosmos_controller, 'Vive Cosmos', 'trigger = blue, bumper = orange, grip = pick up, A = jump, B = crouch'),
	('vive_controller', vive_controller, 'Vive wands', 'trigger = blue, right menu = orange, grip = pick up, right trackpad up/down = jump/crouch'),
	('holographic_controller', holographic_controller, 'Windows Mixed Reality', 'trigger = blue, right menu = orange, grip = pick up, right trackpad up/down = jump/crouch'),
	('hpmotioncontroller', hpmotioncontroller, 'HP Reverb G2', 'trigger = blue, B = orange, grip = pick up, A = jump, right stick click = crouch'),
]


def write(controller, bindings, name, description):
	doc = {
		'action_manifest_version': 0,
		'alias_info': {},
		'app_key': 'system.generated.hl2.exe',
		'bindings': {'/actions/portal': bindings},
		'category': 'steamvr_input',
		'controller_type': controller,
		'description': 'Portal VR default: ' + description + '; left stick = move, right stick = turn',
		'name': 'Portal VR Default (%s)' % name,
		'options': {},
		'simulated_actions': [],
	}
	with open(os.path.join(OUT, f'bindings_{controller}.json'), 'w', newline='\n') as f:
		json.dump(doc, f, indent='\t')
		f.write('\n')


def update_manifest():
	path = os.path.join(OUT, 'action_manifest.json')
	with open(path, encoding='utf-8') as f:
		manifest = json.load(f)
	manifest['default_bindings'] = [{'controller_type': c, 'binding_url': f'bindings_{c}.json'} for c, *_ in CONTROLLERS]
	with open(path, 'w', newline='\n', encoding='utf-8') as f:
		json.dump(manifest, f, indent='\t')
		f.write('\n')


if __name__ == '__main__':
	for controller, fn, name, desc in CONTROLLERS:
		write(controller, fn(), name, desc)
	update_manifest()
	print('wrote bindings for', ', '.join(c for c, *_ in CONTROLLERS), 'to', os.path.abspath(OUT))
