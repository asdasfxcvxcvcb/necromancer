local SECS = 5;

local function Min(x, y) {
	if (x < y)
		return x;
	return y;
}
local function Max(x, y) {
	if (x > y)
		return x;
	return y;
}

local function aabbline(mins, maxs, start, dir) {
	local a = Vector(
		(mins.x - start.x) / dir.x,
		(mins.y - start.y) / dir.y,
		(mins.z - start.z) / dir.z
	);
	local b = Vector(
		(maxs.x - start.x) / dir.x,
		(maxs.y - start.y) / dir.y,
		(maxs.z - start.z) / dir.z
	);
	local c = Vector(
		Min(a.x, b.x),
		Min(a.y, b.y),
		Min(a.z, b.z)
	);
	return Max(Max(c.x, c.y), c.z);
}

local HITBOXDATA;

function OnGameEvent_arrow_impact(params) {
	DebugDrawClear();

	local victim = EntIndexToHScript(params.attachedEntity);

	local shooter = EntIndexToHScript(params.shooter);

	local arrow = null;

	while (arrow = Entities.FindByClassname(arrow, "tf_projectile_arrow")) {
		if (arrow.GetMoveType() == Constants.EMoveType.MOVETYPE_NONE)
			continue;

		if (shooter != arrow.GetOwner())
			continue;

		break;
	}

	local scope = arrow.GetScriptScope();

	local origin = scope.arrowpos;

	local amins = arrow.GetBoundingMins();
	local amaxs = arrow.GetBoundingMaxs();

	local trace = {
		start = origin
		end = origin + scope.arrowvel
		hullmin = amins
		hullmax = amaxs
		mask = 100679691
		ignore = shooter
	};

	TraceHull(trace);

	// might not be very accurate but there's no other way to get this
	// also i didnt bother to calculate gravity at all because im lazy
	origin = trace.endpos;

	trace.end = origin + arrow.GetAbsVelocity() * FrameTime();
	trace.mask = 1107296257;

	// vscript trace results dont include hitboxes, fucking why
	TraceLineEx(trace);

	local directhit = trace.hit && trace.enthit;

	local start = origin + arrow.GetForwardVector() * 16;

	DebugDrawLine(
		origin, start, 0, 0, 255, true, SECS
	);

	DebugDrawLine(
		origin, trace.endpos, 255, 0, 0, true, SECS
	);

	DebugDrawBox(
		origin, amins, amaxs, 0, 0, 255, 64, SECS
	);

	local hitboxes = HITBOXDATA[victim.GetModelName()];

	local correct_hbox;
	local closest_dist = 99999;
	local closest_hbox, closest_orig, closest_hitpos;

	foreach (i, hbox in hitboxes) {
		if (hbox.bone == params.boneIndexAttached)
			correct_hbox = i;

		local borig = victim.GetBoneOrigin(hbox.bone);

		local dir = borig - start;

		local dist = fabs(aabbline(
			borig + hbox.mins, borig + hbox.maxs, start, dir
		));

		dir = dir * dist;

		dist = dir.Length();

		if (dist >= closest_dist)
			continue;

		closest_dist = dist;
		closest_hbox = i;
		closest_orig = borig;
		closest_hitpos = start + dir;
	}

	foreach (i, hbox in hitboxes) {
		local r = 255, g = 255, b = 255, a = 0;

		if (i == closest_hbox) {
			r = 0, b = 0;
		}

		DebugDrawBox(
			victim.GetBoneOrigin(hbox.bone),
			hbox.mins,
			hbox.maxs,
			r, g, b, a,
			SECS
		);
	}

	DebugDrawLine(
		start, closest_hitpos, 0, 255, 255, true, SECS
	);

	DebugDrawLine(
		closest_orig, closest_hitpos, 0, 0, 255, true, SECS
	);

	print(format(
		"---- HUNTSMAN INFO ----\n%s\nvictim: %s\ndirect hit: %s\nclosest hit: %s\n actual hit: %s\n",
		arrow.tostring(),
		victim.tostring(),
		directhit.tostring(),
		hitboxes[closest_hbox].name,
		hitboxes[correct_hbox].name
	));
}

__CollectGameEventCallbacks(this);

function FindArrowsThink() {
	for (local arrow; arrow = Entities.FindByClassname(arrow, "tf_projectile_arrow");) {
		arrow.ValidateScriptScope();

		local scope = arrow.GetScriptScope();

		scope.arrowpos <- arrow.GetOrigin();
		scope.arrowvel <- arrow.GetAbsVelocity();
	}

/*
	DebugDrawClear();

	for (local ply; ply = Entities.FindByClassname(ply, "player");) {
		if (!IsPlayerABot(ply))
			continue;

		foreach (i, hbox in HITBOXDATA[ply.GetModelName()]) {
			DebugDrawBox(
				ply.GetBoneOrigin(hbox.bone),
				hbox.mins,
				hbox.maxs,
				255, 255, 255, 0,
				0.2
			);
		}
	}
*/

	return -1;
}

AddThinkToEnt(GetListenServerHost(), "FindArrowsThink");

HITBOXDATA = {
	["models/player/scout.mdl"] = [
		{
			name = "bip_head"
			bone = 6
			mins = Vector(-5.5, -9, -7.0500001907349)
			maxs = Vector(5.5, 4, 4.9499998092651)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-7.5, -1, -4.5)
			maxs = Vector(7.5, 11, 6.5)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-7, 0, -6.5)
			maxs = Vector(7, 5, 3.5)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-7.5, -2, -7)
			maxs = Vector(7.5, 4, 3)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-8, -3, -7.5)
			maxs = Vector(8, 3, 2.5)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-7, -3, -6)
			maxs = Vector(7, 2, 4)
		},
		{
			name = "bip_upperArm_L"
			bone = 9
			mins = Vector(-1, -2, -2)
			maxs = Vector(13, 2, 3)
		},
		{
			name = "bip_lowerArm_L"
			bone = 11
			mins = Vector(0, -2.0999999046326, -2)
			maxs = Vector(10, 2.9000000953674, 2)
		},
		{
			name = "bip_hand_L"
			bone = 19
			mins = Vector(-3.4500000476837, -9.5, -4)
			maxs = Vector(1.0499999523163, -0.5, 2)
		},
		{
			name = "bip_upperArm_R"
			bone = 10
			mins = Vector(-1, -3, -2)
			maxs = Vector(13, 2, 2)
		},
		{
			name = "bip_lowerArm_R"
			bone = 12
			mins = Vector(0, -2.9000000953674, -2)
			maxs = Vector(10, 2.0999999046326, 2)
		},
		{
			name = "bip_hand_R"
			bone = 20
			mins = Vector(-1.0499999523163, 0.5, -2)
			maxs = Vector(3.4500000476837, 9.5, 4)
		},
		{
			name = "bip_hip_L"
			bone = 13
			mins = Vector(4, -4, -5)
			maxs = Vector(20, 4, 3)
		},
		{
			name = "bip_knee_L"
			bone = 15
			mins = Vector(0.5, -2.5, -5.5)
			maxs = Vector(19.5, 3.5, 1.5)
		},
		{
			name = "bip_foot_L"
			bone = 17
			mins = Vector(-1.75, -10, -2.5)
			maxs = Vector(1.75, 2, 2.5)
		},
		{
			name = "bip_hip_R"
			bone = 14
			mins = Vector(4, -3, -4)
			maxs = Vector(20, 5, 4)
		},
		{
			name = "bip_knee_R"
			bone = 16
			mins = Vector(0.5, -3.5, -5.5)
			maxs = Vector(19.5, 2.5, 1.5)
		},
		{
			name = "bip_foot_R"
			bone = 18
			mins = Vector(-1.75, -2, -2.5)
			maxs = Vector(1.75, 10, 2.5)
		},
		{
			name = "bip_packmiddle"
			bone = 38
			mins = Vector(-2.75, -10, -3)
			maxs = Vector(4.25, 8, 5)
		},
	],
	["models/player/soldier.mdl"] = [
		{
			name = "bip_head"
			bone = 6
			mins = Vector(-6.25, -9, -7.5500001907349)
			maxs = Vector(6.25, 5, 5.4499998092651)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-10.5, -3, -9)
			maxs = Vector(10.5, 11, 7)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-9.5, -1.5, -10)
			maxs = Vector(9.5, 5.5, 5)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-10, -2, -10.5)
			maxs = Vector(10, 4, 3.5)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-10, -4, -11)
			maxs = Vector(10, 4, 5)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-10, -4.5, -7)
			maxs = Vector(10, 1.5, 5)
		},
		{
			name = "bip_upperArm_L"
			bone = 9
			mins = Vector(0, -4, -3)
			maxs = Vector(14, 4, 3)
		},
		{
			name = "bip_lowerArm_L"
			bone = 11
			mins = Vector(0, -2.75, -4)
			maxs = Vector(14, 3.75, 4)
		},
		{
			name = "bip_hand_L"
			bone = 19
			mins = Vector(-2.75, -10, -2.5)
			maxs = Vector(1.75, 0, 4.5)
		},
		{
			name = "bip_upperArm_R"
			bone = 10
			mins = Vector(0, -4, -3)
			maxs = Vector(14, 4, 3)
		},
		{
			name = "bip_lowerArm_R"
			bone = 12
			mins = Vector(0, -4, -3.75)
			maxs = Vector(14, 4, 2.75)
		},
		{
			name = "bip_hand_R"
			bone = 20
			mins = Vector(-1.75, 0, -4.5)
			maxs = Vector(2.75, 10, 2.5)
		},
		{
			name = "bip_hip_L"
			bone = 13
			mins = Vector(1.5, -5.5, -4)
			maxs = Vector(16.5, 4.5, 4)
		},
		{
			name = "bip_knee_L"
			bone = 15
			mins = Vector(0, -2, -5)
			maxs = Vector(18, 4, 2)
		},
		{
			name = "bip_foot_L"
			bone = 17
			mins = Vector(-2.5, -11, -3.75)
			maxs = Vector(2.5, 3, 2.25)
		},
		{
			name = "bip_hip_R"
			bone = 14
			mins = Vector(1.5, -4.5, -4)
			maxs = Vector(16.5, 5.5, 4)
		},
		{
			name = "bip_knee_R"
			bone = 16
			mins = Vector(0, -4, -5)
			maxs = Vector(18, 2, 2)
		},
		{
			name = "bip_foot_R"
			bone = 18
			mins = Vector(-2.5, -3, -2.25)
			maxs = Vector(2.5, 11, 3.75)
		},
	],
	["models/player/pyro.mdl"] = [
		{
			name = "bip_head"
			bone = 6
			mins = Vector(-6, -9.5, -8)
			maxs = Vector(6, 3.5, 5)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-10, -1, -6)
			maxs = Vector(10, 11, 10)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-9.5, -1.5, -11.75)
			maxs = Vector(9.5, 6.5, 4.25)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-9, -2, -12.5)
			maxs = Vector(9, 4, 3.5)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-9, -3, -10.5)
			maxs = Vector(9, 3, 5.5)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-7, -3, -6)
			maxs = Vector(7, 2, 4)
		},
		{
			name = "bip_upperArm_L"
			bone = 8
			mins = Vector(-1.5, -3, -2.75)
			maxs = Vector(13.5, 5, 3.25)
		},
		{
			name = "bip_lowerArm_L"
			bone = 9
			mins = Vector(0, -3.0999999046326, -3)
			maxs = Vector(12, 3.9000000953674, 4)
		},
		{
			name = "bip_hand_L"
			bone = 10
			mins = Vector(-2.5, -9.5, -3.25)
			maxs = Vector(2.5, -0.5, 3.25)
		},
		{
			name = "bip_upperArm_R"
			bone = 12
			mins = Vector(-1.5, -5, -3.25)
			maxs = Vector(13.5, 3, 2.75)
		},
		{
			name = "bip_lowerArm_R"
			bone = 13
			mins = Vector(0, -3.9000000953674, -4)
			maxs = Vector(12, 3.0999999046326, 3)
		},
		{
			name = "bip_hand_R"
			bone = 14
			mins = Vector(-2.5, 0.5, -3.25)
			maxs = Vector(2.5, 9.5, 3.25)
		},
		{
			name = "bip_hip_L"
			bone = 15
			mins = Vector(2, -5, -6)
			maxs = Vector(16, 5, 5)
		},
		{
			name = "bip_knee_L"
			bone = 16
			mins = Vector(0.5, -6.5, -4.5)
			maxs = Vector(19.5, 2.5, 2.5)
		},
		{
			name = "bip_foot_L"
			bone = 17
			mins = Vector(-1.75, -10, -4)
			maxs = Vector(1.75, 2, 2)
		},
		{
			name = "bip_hip_R"
			bone = 19
			mins = Vector(2, -5, -6)
			maxs = Vector(16, 5, 5)
		},
		{
			name = "bip_knee_R"
			bone = 20
			mins = Vector(0.5, -4.5, -6.5)
			maxs = Vector(19.5, 2.5, 2.5)
		},
		{
			name = "bip_foot_R"
			bone = 21
			mins = Vector(-1.75, -2, -2)
			maxs = Vector(1.75, 10, 4)
		},
		{
			name = "prp_fuelTank"
			bone = 23
			mins = Vector(-4, -20, -4)
			maxs = Vector(6, 8, 4)
		},
	],
	["models/player/demo.mdl"] = [
		{
			name = "bip_head"
			bone = 16
			mins = Vector(-6, -8, -7.5)
			maxs = Vector(6, 5, 4.5)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-9, 0, -5.5)
			maxs = Vector(9, 10, 5.5)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-10, -2, -10)
			maxs = Vector(10, 4, 6)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-11, -3, -11)
			maxs = Vector(11, 3, 5)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-11.5, -3, -11)
			maxs = Vector(11.5, 3, 5)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-13.109999656677, -4, -10)
			maxs = Vector(13.109999656677, 2, 5)
		},
		{
			name = "bip_upperArm_L"
			bone = 6
			mins = Vector(0, -3, -4.75)
			maxs = Vector(16, 3, 2.75)
		},
		{
			name = "bip_lowerArm_L"
			bone = 13
			mins = Vector(0, -2.25, -4.25)
			maxs = Vector(13, 2.25, 2.75)
		},
		{
			name = "bip_hand_L"
			bone = 17
			mins = Vector(-5, -10, -3)
			maxs = Vector(1, 0, 5)
		},
		{
			name = "bip_upperArm_R"
			bone = 8
			mins = Vector(0, -2.75, -3)
			maxs = Vector(16, 4.75, 3)
		},
		{
			name = "bip_lowerArm_R"
			bone = 14
			mins = Vector(0, -2.25, -2.5999999046326)
			maxs = Vector(13, 4.25, 2.0999999046326)
		},
		{
			name = "bip_hand_R"
			bone = 20
			mins = Vector(-1, 0, -5)
			maxs = Vector(5, 10, 3)
		},
		{
			name = "bip_hip_L"
			bone = 9
			mins = Vector(1.5, -5, -5)
			maxs = Vector(18.5, 3, 4)
		},
		{
			name = "bip_knee_L"
			bone = 10
			mins = Vector(-0.5, -2.5, -4.5)
			maxs = Vector(20.5, 3, 2.5)
		},
		{
			name = "bip_foot_L"
			bone = 25
			mins = Vector(-2.4500000476837, -11.5, -4)
			maxs = Vector(3.0499999523163, 3.5, 2)
		},
		{
			name = "bip_hip_R"
			bone = 11
			mins = Vector(1.5, -4, -3)
			maxs = Vector(18.5, 5, 5)
		},
		{
			name = "bip_knee_R"
			bone = 12
			mins = Vector(-0.5, -3, -4.5)
			maxs = Vector(20.5, 2.5, 2.5)
		},
		{
			name = "bip_foot_R"
			bone = 26
			mins = Vector(-3.0499999523163, -3.5, -2)
			maxs = Vector(2.4500000476837, 11.5, 4)
		},
	],
	["models/player/heavy.mdl"] = [
		{
			name = "bip_head"
			bone = 6
			mins = Vector(-6, -9.3500003814697, -8.6999998092651)
			maxs = Vector(6, 4.6500000953674, 4.3000001907349)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-11, 0, -9)
			maxs = Vector(11, 10, 3)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-12.5, -4, -14)
			maxs = Vector(12.5, 6, 4)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-12.5, -4, -15)
			maxs = Vector(12.5, 4, 5)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-12.5, -4, -15)
			maxs = Vector(12.5, 4, 5)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-13.109999656677, -8, -12.5)
			maxs = Vector(13.109999656677, 2, 5.1599998474121)
		},
		{
			name = "bip_upperArm_L"
			bone = 9
			mins = Vector(-3, -5.5, -3.75)
			maxs = Vector(19, 5.5, 3.75)
		},
		{
			name = "bip_lowerArm_L"
			bone = 11
			mins = Vector(0.5, -5, -3.5)
			maxs = Vector(16.5, 4, 3.5)
		},
		{
			name = "bip_hand_L"
			bone = 17
			mins = Vector(-5.5, -13, -4.25)
			maxs = Vector(1.5, -1, 6.25)
		},
		{
			name = "bip_upperArm_R"
			bone = 10
			mins = Vector(-3, -5.5, -3.75)
			maxs = Vector(19, 5.5, 3.75)
		},
		{
			name = "bip_lowerArm_R"
			bone = 12
			mins = Vector(0.5, -4, -3.5)
			maxs = Vector(16.5, 5, 3.5)
		},
		{
			name = "bip_hand_R"
			bone = 18
			mins = Vector(-1.5, 1, -6.25)
			maxs = Vector(5.5, 13, 4.25)
		},
		{
			name = "bip_hip_L"
			bone = 13
			mins = Vector(0, -4.3000001907349, -3.9000000953674)
			maxs = Vector(14, 3.7000000476837, 3.0999999046326)
		},
		{
			name = "bip_knee_L"
			bone = 15
			mins = Vector(-0.5, -2.5, -4.75)
			maxs = Vector(16.5, 4.5, 2.75)
		},
		{
			name = "bip_foot_L"
			bone = 29
			mins = Vector(-2.5499999523163, -12.5, -3.7999999523163)
			maxs = Vector(2.9500000476837, 2.5, 2.2000000476837)
		},
		{
			name = "bip_hip_R"
			bone = 14
			mins = Vector(0, -3.7000000476837, -3.9000000953674)
			maxs = Vector(14, 4.3000001907349, 3.0999999046326)
		},
		{
			name = "bip_knee_R"
			bone = 16
			mins = Vector(-0.5, -2.5, -4.75)
			maxs = Vector(16.5, 4.5, 2.75)
		},
		{
			name = "bip_foot_R"
			bone = 30
			mins = Vector(-2.9500000476837, -2.5, -2.2000000476837)
			maxs = Vector(2.5499999523163, 12.5, 3.7999999523163)
		},
	],
	["models/player/engineer.mdl"] = [
		{
			name = "bip_head"
			bone = 8
			mins = Vector(-6, -11, -8)
			maxs = Vector(6, 4, 6)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-9, -0.5, -7.5)
			maxs = Vector(9, 11.5, 3.5)
		},
		{
			name = "bip_spine_0"
			bone = 3
			mins = Vector(-9, -2, -10)
			maxs = Vector(9, 4, 4)
		},
		{
			name = "bip_spine_1"
			bone = 4
			mins = Vector(-9, -3, -10.5)
			maxs = Vector(9, 3, 4.5)
		},
		{
			name = "bip_spine_2"
			bone = 5
			mins = Vector(-10, -3, -9)
			maxs = Vector(10, 3, 5)
		},
		{
			name = "bip_spine_3"
			bone = 6
			mins = Vector(-10, -3.5, -10)
			maxs = Vector(10, 1.5, 4)
		},
		{
			name = "bip_upperArm_L"
			bone = 12
			mins = Vector(0, -3.5, -2.75)
			maxs = Vector(12, 3.5, 3.25)
		},
		{
			name = "bip_lowerArm_L"
			bone = 13
			mins = Vector(0, -2.25, -3.25)
			maxs = Vector(13, 2.25, 3.25)
		},
		{
			name = "bip_hand_L"
			bone = 20
			mins = Vector(-3.5, -10, -3)
			maxs = Vector(1.5, 0, 5)
		},
		{
			name = "bip_upperArm_R"
			bone = 15
			mins = Vector(0, -3.5, -2.75)
			maxs = Vector(12, 3.5, 3.25)
		},
		{
			name = "bip_lowerArm_R"
			bone = 16
			mins = Vector(0, -3.25, -2.75)
			maxs = Vector(13, 3.25, 2.75)
		},
		{
			name = "bip_hand_R"
			bone = 19
			mins = Vector(-2.5, 0.5, -3.5)
			maxs = Vector(2.5, 9.5, 4.5)
		},
		{
			name = "bip_hip_L"
			bone = 9
			mins = Vector(2, -4, -5)
			maxs = Vector(16, 4, 4)
		},
		{
			name = "bip_knee_L"
			bone = 10
			mins = Vector(0, -2.25, -4.5)
			maxs = Vector(16, 3.25, 2.5)
		},
		{
			name = "bip_foot_L"
			bone = 17
			mins = Vector(-2, -10, -3)
			maxs = Vector(2, 2, 3)
		},
		{
			name = "bip_hip_R"
			bone = 1
			mins = Vector(2, -4, -5)
			maxs = Vector(16, 4, 4)
		},
		{
			name = "bip_knee_R"
			bone = 2
			mins = Vector(0, -3.25, -4.5)
			maxs = Vector(16, 2.25, 2.5)
		},
		{
			name = "bip_foot_R"
			bone = 18
			mins = Vector(-2, -2, -2.5)
			maxs = Vector(2, 10, 3.5)
		},
	],
	["models/player/medic.mdl"] = [
		{
			name = "bip_head"
			bone = 6
			mins = Vector(-5, -8, -7.5)
			maxs = Vector(5, 5, 3.5)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-9, -1, -6)
			maxs = Vector(9, 11, 5)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-8, -1.5, -8)
			maxs = Vector(8, 3.5, 4)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-7.5, -2, -8.5)
			maxs = Vector(7.5, 4, 3.5)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-8.5, -3, -8.75)
			maxs = Vector(8.5, 3, 3.75)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-10, -2, -5)
			maxs = Vector(10, 2, 3)
		},
		{
			name = "bip_upperArm_L"
			bone = 9
			mins = Vector(-0.5, -3, -2.25)
			maxs = Vector(12.5, 3.5, 2.75)
		},
		{
			name = "bip_lowerArm_L"
			bone = 11
			mins = Vector(0, -2.3499999046326, -3)
			maxs = Vector(12, 3.1500000953674, 3)
		},
		{
			name = "bip_hand_L"
			bone = 17
			mins = Vector(-3.5, -9.5, -3.25)
			maxs = Vector(1.5, -0.5, 3.25)
		},
		{
			name = "bip_upperArm_R"
			bone = 10
			mins = Vector(-0.5, -3.5, -2.75)
			maxs = Vector(12.5, 3, 2.25)
		},
		{
			name = "bip_lowerArm_R"
			bone = 12
			mins = Vector(0, -3, -3.1500000953674)
			maxs = Vector(12, 3, 2.3499999046326)
		},
		{
			name = "bip_hand_R"
			bone = 18
			mins = Vector(-1.5, 0.5, -3.25)
			maxs = Vector(3.5, 9.5, 3.25)
		},
		{
			name = "bip_hip_L"
			bone = 13
			mins = Vector(4, -3.25, -2.5)
			maxs = Vector(18, 5.25, 5.5)
		},
		{
			name = "bip_knee_L"
			bone = 15
			mins = Vector(0.5, -2.75, -3.5)
			maxs = Vector(19.5, 4.25, 3.5)
		},
		{
			name = "bip_foot_L"
			bone = 34
			mins = Vector(-2, -10, -4)
			maxs = Vector(2, 2, 2)
		},
		{
			name = "bip_hip_R"
			bone = 14
			mins = Vector(4, -2.5, -3.25)
			maxs = Vector(18, 5.5, 5.25)
		},
		{
			name = "bip_knee_R"
			bone = 16
			mins = Vector(0.5, -3.5, -2.75)
			maxs = Vector(19.5, 3.5, 4.25)
		},
		{
			name = "bip_foot_R"
			bone = 35
			mins = Vector(-2, -2, -2)
			maxs = Vector(2, 10, 4)
		},
	],
	["models/player/sniper.mdl"] = [
		{
			name = "bip_head"
			bone = 6
			mins = Vector(-6, -10.5, -7.5500001907349)
			maxs = Vector(6, 3.5, 5.4499998092651)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-9, -1, -5)
			maxs = Vector(9, 9, 6)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-9.5, -1.5, -7.5)
			maxs = Vector(9.5, 5.5, 7.5)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-9, -2, -8.5)
			maxs = Vector(9, 4, 5.5)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-9, -3, -8)
			maxs = Vector(9, 3, 6)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-9, -2, -6)
			maxs = Vector(9, 3, 4)
		},
		{
			name = "bip_upperArm_L"
			bone = 9
			mins = Vector(0, -3.25, -2.5)
			maxs = Vector(12, 3.75, 2.5)
		},
		{
			name = "bip_lowerArm_L"
			bone = 11
			mins = Vector(0, -3.5, -3)
			maxs = Vector(14, 2.5, 2)
		},
		{
			name = "bip_hand_L"
			bone = 19
			mins = Vector(-3.5, -9.5, -3.5)
			maxs = Vector(1, -0.5, 2.5)
		},
		{
			name = "bip_upperArm_R"
			bone = 10
			mins = Vector(0, -3.75, -2.5)
			maxs = Vector(12, 3.25, 2.5)
		},
		{
			name = "bip_lowerArm_R"
			bone = 12
			mins = Vector(0, -2.5, -3)
			maxs = Vector(14, 3.5, 2)
		},
		{
			name = "bip_hand_R"
			bone = 20
			mins = Vector(-1, 0.5, -2.5)
			maxs = Vector(3.5, 9.5, 3.5)
		},
		{
			name = "bip_hip_L"
			bone = 13
			mins = Vector(1.5, -3.5, -4.5)
			maxs = Vector(18.5, 3.5, 2.5)
		},
		{
			name = "bip_knee_L"
			bone = 15
			mins = Vector(0, -2, -5)
			maxs = Vector(18, 3, 2)
		},
		{
			name = "bip_foot_L"
			bone = 17
			mins = Vector(-1.5, -11, -3.75)
			maxs = Vector(3.5, 3, 2.25)
		},
		{
			name = "bip_hip_R"
			bone = 14
			mins = Vector(1.5, -3.5, -4.5)
			maxs = Vector(18.5, 3.5, 2.5)
		},
		{
			name = "bip_knee_R"
			bone = 16
			mins = Vector(0, -2, -3)
			maxs = Vector(18, 5, 2)
		},
		{
			name = "bip_foot_R"
			bone = 18
			mins = Vector(-3.5, -3, -2.25)
			maxs = Vector(1.5, 11, 3.75)
		},
	],
	["models/player/spy.mdl"] = [
		{
			name = "bip_head"
			bone = 6
			mins = Vector(-5, -7.75, -7.5)
			maxs = Vector(5, 3.75, 3.5)
		},
		{
			name = "bip_pelvis"
			bone = 0
			mins = Vector(-9, -1, -6.5)
			maxs = Vector(9, 9, 4.5)
		},
		{
			name = "bip_spine_0"
			bone = 1
			mins = Vector(-8, -2, -7)
			maxs = Vector(8, 4, 5)
		},
		{
			name = "bip_spine_1"
			bone = 2
			mins = Vector(-7.5, -2, -8)
			maxs = Vector(7.5, 4, 4)
		},
		{
			name = "bip_spine_2"
			bone = 3
			mins = Vector(-9, -3, -9)
			maxs = Vector(9, 3, 4)
		},
		{
			name = "bip_spine_3"
			bone = 4
			mins = Vector(-9, -3, -6)
			maxs = Vector(9, 1, 4)
		},
		{
			name = "bip_upperArm_L"
			bone = 10
			mins = Vector(0.25, -2.5, -1.75)
			maxs = Vector(13.75, 3.5, 3.25)
		},
		{
			name = "bip_lowerArm_L"
			bone = 12
			mins = Vector(0.5, -3.5, -2.5)
			maxs = Vector(13.5, 2.5, 2.5)
		},
		{
			name = "bip_hand_L"
			bone = 14
			mins = Vector(-3.25, -7.5, -2.5)
			maxs = Vector(0.75, -0.5, 2.5)
		},
		{
			name = "bip_upperArm_R"
			bone = 11
			mins = Vector(0.25, -3.5, -1.75)
			maxs = Vector(13.75, 2.5, 3.25)
		},
		{
			name = "bip_lowerArm_R"
			bone = 13
			mins = Vector(0.5, -2.5, -2.5)
			maxs = Vector(13.5, 2.5, 3.5)
		},
		{
			name = "bip_hand_R"
			bone = 15
			mins = Vector(-0.75, 0.5, -2.5)
			maxs = Vector(3.25, 7.5, 2.5)
		},
		{
			name = "bip_hip_L"
			bone = 16
			mins = Vector(2, -4, -3.5)
			maxs = Vector(22, 2, 3.5)
		},
		{
			name = "bip_knee_L"
			bone = 18
			mins = Vector(0, -2.5, -3.25)
			maxs = Vector(18, 2.5, 1.75)
		},
		{
			name = "bip_foot_L"
			bone = 20
			mins = Vector(-1.5, -11, -2.75)
			maxs = Vector(1.5, 3, 1.25)
		},
		{
			name = "bip_hip_R"
			bone = 17
			mins = Vector(2, -3.5, -2)
			maxs = Vector(22, 3.5, 4)
		},
		{
			name = "bip_knee_R"
			bone = 19
			mins = Vector(0, -2.5, -3.25)
			maxs = Vector(18, 2.5, 1.75)
		},
		{
			name = "bip_foot_R"
			bone = 21
			mins = Vector(-1.5, -3, -1.25)
			maxs = Vector(1.5, 11, 2.75)
		},
	],
};
