"""Generates the default SteamVR Input binding files for Portal VR.

Hand-dependent actions (fire, grab, use) are bound on BOTH hands; the game reads each one
restricted to the gun hand or the free hand (vr_gun_hand), so left-handed mode needs no
separate bindings. Run after editing:  python tools/gen_bindings.py
"""
import json
import os

OUT = os.path.join(os.path.dirname(__file__), '..', 'sp', 'game', 'portalvr', 'actions')
A = '/actions/portal/in/'


def button(path, action, mode='button', inp='click'):
	return {'path': path, 'mode': mode, 'inputs': {inp: {'output': A + action}}}


def both(component, action, **kw):
	return [button(f'/user/hand/{h}/input/{component}', action, **kw) for h in ('left', 'right')]


def joystick(path, action):
	return {'path': path, 'mode': 'joystick', 'inputs': {'position': {'output': A + action}}}


def trigger_pull(path, action):
	return {'path': path, 'mode': 'trigger', 'inputs': {'pull': {'output': A + action}}}


def common(controller, skeleton=True):
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
	b = common('frame_controller')
	s = b['sources']
	s += [
		# Triggers: blue portal on the gun hand, use on the free hand.
		{'path': '/user/hand/left/input/trigger', 'mode': 'trigger',
		 'inputs': {'click': {'output': A + 'FirePortal1'}}},
		{'path': '/user/hand/right/input/trigger', 'mode': 'trigger',
		 'inputs': {'click': {'output': A + 'FirePortal1'}}},
	]
	s += both('trigger', 'Use', mode='trigger')
	s += both('bumper', 'FirePortal2')
	# Grips: gun-tractor on the gun hand, physical grab on the free hand.
	for h in ('left', 'right'):
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'trigger',
				  'inputs': {'click': {'output': A + 'HandGrab'}, 'pull': {'output': A + 'GripSqueeze'}}})
	s += both('grip', 'GunGrab', mode='trigger')
	s.append(joystick('/user/hand/left/input/thumbstick', 'Move'))
	s.append(button('/user/hand/left/input/thumbstick', 'Crouch'))
	s.append(joystick('/user/hand/right/input/thumbstick', 'Turn'))
	s.append(button('/user/hand/right/input/thumbstick', 'Recenter'))
	s.append(button('/user/hand/right/input/a', 'Jump'))
	s.append(button('/user/hand/right/input/b', 'Crouch'))
	s.append(button('/user/hand/right/input/menu', 'Menu'))
	s.append(button('/user/hand/left/input/view', 'ToggleHUD'))
	s.append(button('/user/hand/left/input/dpad_up', 'QuickSave'))
	s.append(button('/user/hand/left/input/dpad_down', 'QuickLoad'))
	return b


def knuckles():
	b = common('knuckles')
	s = b['sources']
	for h in ('left', 'right'):
		s.append({'path': f'/user/hand/{h}/input/trigger', 'mode': 'trigger',
				  'inputs': {'click': {'output': A + 'FirePortal1'}}})
	s += both('trigger', 'Use', mode='trigger')
	s += both('trackpad', 'FirePortal2', mode='button')
	for h in ('left', 'right'):
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'grab',
				  'inputs': {'grab': {'output': A + 'HandGrab'}}})
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'trigger',
				  'inputs': {'pull': {'output': A + 'GripSqueeze'}}})
	for h in ('left', 'right'):
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'grab',
				  'inputs': {'grab': {'output': A + 'GunGrab'}}})
	s.append(joystick('/user/hand/left/input/thumbstick', 'Move'))
	s.append(button('/user/hand/left/input/thumbstick', 'Crouch'))
	s.append(joystick('/user/hand/right/input/thumbstick', 'Turn'))
	s.append(button('/user/hand/right/input/thumbstick', 'Recenter'))
	s.append(button('/user/hand/right/input/a', 'Jump'))
	s.append(button('/user/hand/right/input/b', 'Menu'))
	s.append(button('/user/hand/left/input/b', 'Crouch'))
	s.append(button('/user/hand/left/input/a', 'ToggleHUD'))
	return b


def oculus_touch():
	b = common('oculus_touch')
	s = b['sources']
	for h in ('left', 'right'):
		s.append({'path': f'/user/hand/{h}/input/trigger', 'mode': 'trigger',
				  'inputs': {'click': {'output': A + 'FirePortal1'}}})
	s += both('trigger', 'Use', mode='trigger')
	s.append(button('/user/hand/right/input/b', 'FirePortal2'))
	s.append(button('/user/hand/left/input/y', 'FirePortal2'))
	for h in ('left', 'right'):
		s.append({'path': f'/user/hand/{h}/input/grip', 'mode': 'trigger',
				  'inputs': {'click': {'output': A + 'HandGrab'}, 'pull': {'output': A + 'GripSqueeze'}}})
	s += both('grip', 'GunGrab', mode='trigger')
	s.append(joystick('/user/hand/left/input/joystick', 'Move'))
	s.append(button('/user/hand/left/input/joystick', 'Crouch'))
	s.append(joystick('/user/hand/right/input/joystick', 'Turn'))
	s.append(button('/user/hand/right/input/joystick', 'Recenter'))
	s.append(button('/user/hand/right/input/a', 'Jump'))
	s.append(button('/user/hand/left/input/x', 'Crouch'))
	s.append(button('/user/hand/left/input/application_menu', 'Menu'))
	return b


def write(controller, bindings, name):
	doc = {
		'action_manifest_version': 0,
		'alias_info': {},
		'app_key': 'system.generated.hl2.exe',
		'bindings': {'/actions/portal': bindings},
		'category': 'steamvr_input',
		'controller_type': controller,
		'description': 'Portal VR default: gun trigger = blue, gun bumper = orange, free-hand grip = grab, left stick = move, right stick = turn',
		'name': name,
		'options': {},
		'simulated_actions': [],
	}
	with open(os.path.join(OUT, f'bindings_{controller}.json'), 'w', newline='\n') as f:
		json.dump(doc, f, indent='\t')
		f.write('\n')


if __name__ == '__main__':
	write('frame_controller', frame_controller(), 'Portal VR Default (Steam Frame)')
	write('knuckles', knuckles(), 'Portal VR Default (Index)')
	write('oculus_touch', oculus_touch(), 'Portal VR Default (Touch)')
	print('wrote bindings to', os.path.abspath(OUT))
