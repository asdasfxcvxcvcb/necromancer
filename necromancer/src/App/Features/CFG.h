#pragma once

#include "../../Utils/Config/Config.h"

namespace CFG
{
#pragma region Aimbot

	CFGVAR(Aimbot_Active, false);
	CFGVAR(Aimbot_AutoShoot, true);
	CFGVAR(Aimbot_Always_On, false);
	CFGVAR(Aimbot_Key, 0);
	CFGVAR(Aimbot_Ignore_Untagged_Key, 0); // When held, only target tagged players (Cheater/Targeted/RetardLegit/Streamer)
	CFGVAR(Aimbot_Target_Players, true);
	CFGVAR(Aimbot_Target_Buildings, true);
	CFGVAR(Aimbot_Ignore_Friends, true);
	CFGVAR(Aimbot_Ignore_Invisible, false);
	CFGVAR(Aimbot_Ignore_Invulnerable, true);
	CFGVAR(Aimbot_Ignore_Taunting, false);
	CFGVAR(Aimbot_Ignore_Stickies, false);

	CFGVAR(Aimbot_Hitscan_Active, true);
	CFGVAR(Aimbot_Hitscan_Target_LagRecords, true);
	CFGVAR(Aimbot_Hitscan_Target_Stickies, true);
	CFGVAR(Aimbot_Hitscan_Aim_Type, 1); //0 Normal 1 Silent 2 Smooth 3 Triggerbot
	CFGVAR(Aimbot_Hitscan_Sort, 0); //0 FOV 1 Distance
	CFGVAR(Aimbot_Hitscan_Hitbox, 2); //0 Head 1 Body 2 Auto 3 Switch
	CFGVAR(Aimbot_Hitscan_Switch_Key, 0); // Key to toggle head/body for sniper/ambassador
	CFGVAR(Aimbot_Hitscan_Switch_State, false); // false = Head, true = Body
	CFGVAR(Aimbot_Hitscan_Switch_Indicator_X, 960);
	CFGVAR(Aimbot_Hitscan_Switch_Indicator_Y, 540);
	CFGVAR(Aimbot_Hitscan_FOV, 15.0f);
	CFGVAR(Aimbot_Hitscan_Smoothing, 3.0f);
	CFGVAR(Aimbot_Hitscan_Fake_Latency, 0.0f); // Fake latency in milliseconds (0-600ms)
	CFGVAR(Aimbot_Hitscan_Hitchance, 0); // Hitchance percentage (0 = disabled, 1-100 = required hit %)
	CFGVAR(Aimbot_Hitscan_Pellet_Visibility_Scale, 0.50f); // 0.30 - 0.80 recommended
	CFGVAR(Aimbot_Hitscan_Scan_Head, true);
	CFGVAR(Aimbot_Hitscan_Scan_Body, true);
	CFGVAR(Aimbot_Hitscan_Scan_Arms, false);
	CFGVAR(Aimbot_Hitscan_Scan_Legs, false);
	CFGVAR(Aimbot_Hitscan_Scan_Buildings, true);
	CFGVAR(Aimbot_Hitscan_Advanced_Smooth_AutoShoot, true);
	CFGVAR(Aimbot_Hitscan_Auto_Scope, false);
	CFGVAR(Aimbot_Hitscan_Wait_For_Headshot, true);
	CFGVAR(Aimbot_Hitscan_Wait_For_Charge, false);
	CFGVAR(Aimbot_Hitscan_Minigun_TapFire, false);
	CFGVAR(Aimbot_Hitscan_Smart_Shotgun, false); // Smart shotgun damage prediction - waits for pellet visibility
	CFGVAR(Aimbot_Hitscan_FakeLagFix, false); // Shoot on unchoke - when enemy sends update, their position is accurate
	CFGVAR(Aimbot_Hitscan_Resolver, true); // Auto resolver - adjusts aim for fake angles

	CFGVAR(Aimbot_Projectile_Active, true);
	CFGVAR(Aimbot_Projectile_NoSpread, true);
	CFGVAR(Aimbot_Projectile_Auto_Double_Donk, true);
	CFGVAR(Aimbot_Projectile_Advanced_Head_Aim, true);
	
	// Huntsman Advanced Head Aim Settings (lerp-based like Amalgam)
	CFGVAR(Aimbot_Projectile_Huntsman_Lerp, 50.0f);      // Base lerp % towards top of bbox (0-100)
	CFGVAR(Aimbot_Projectile_Huntsman_Lerp_Low, 100.0f); // Lerp % when target is above shooter (0-100)
	CFGVAR(Aimbot_Projectile_Huntsman_Add, 3.0f);        // Base Z offset to add to head position
	CFGVAR(Aimbot_Projectile_Huntsman_Add_Low, 0.0f);    // Z offset when target is above shooter
	CFGVAR(Aimbot_Projectile_Huntsman_Clamp, 2.0f);      // Clamp distance from bbox edges
	CFGVAR(Aimbot_Projectile_Ground_Strafe_Prediction, true);
	CFGVAR(Aimbot_Projectile_Air_Strafe_Prediction, true);
	CFGVAR(Aimbot_Projectile_BBOX_Multipoint, true);
	CFGVAR(Aimbot_Projectile_Rocket_Splash, 1); //0 Disabled 1 Enabled 2 Preferred
	CFGVAR(Aimbot_Projectile_Aim_Type, 1); //0 Normal 1 Silent
	CFGVAR(Aimbot_Projectile_Sort, 0); //0 FOV 1 Distance
	CFGVAR(Aimbot_Projectile_Aim_Position, 3); //0 Feet 1 Body 2 Head 3 Auto
	CFGVAR(Aimbot_Projectile_Aim_Prediction_Method, 0); //0 Full Acceleration 1 Current Velocity
	CFGVAR(Aimbot_Projectile_FOV, 45.0f);
	CFGVAR(Aimbot_Projectile_Max_Simulation_Time, 2.5f); // Increased from 1.5 for better prediction
	CFGVAR(Aimbot_Projectile_Max_Processing_Targets, 1);
	
	// Strafe prediction tuning (Amalgam-style)
	CFGVAR(Aimbot_Projectile_Ground_Samples, 12);
	CFGVAR(Aimbot_Projectile_Air_Samples, 16);
	CFGVAR(Aimbot_Projectile_Ground_Straight_Fuzzy, 1000.f);
	CFGVAR(Aimbot_Projectile_Air_Straight_Fuzzy, 500.f);
	CFGVAR(Aimbot_Projectile_Ground_Max_Changes, 1);
	CFGVAR(Aimbot_Projectile_Air_Max_Changes, 0);
	CFGVAR(Aimbot_Projectile_Ground_Max_Change_Time, 8);
	CFGVAR(Aimbot_Projectile_Air_Max_Change_Time, 4);
	
	// Distance-based minimum samples (Amalgam-style)
	CFGVAR(Aimbot_Projectile_Ground_Low_Min_Distance, 500.f);
	CFGVAR(Aimbot_Projectile_Ground_Low_Min_Samples, 4.f);
	CFGVAR(Aimbot_Projectile_Ground_High_Min_Distance, 2000.f);
	CFGVAR(Aimbot_Projectile_Ground_High_Min_Samples, 12.f);
	CFGVAR(Aimbot_Projectile_Air_Low_Min_Distance, 500.f);
	CFGVAR(Aimbot_Projectile_Air_Low_Min_Samples, 4.f);
	CFGVAR(Aimbot_Projectile_Air_High_Min_Distance, 2000.f);
	CFGVAR(Aimbot_Projectile_Air_High_Min_Samples, 12.f);
	
	// Delta prediction (Amalgam-style)
	CFGVAR(Aimbot_Projectile_Delta_Count, 16);
	CFGVAR(Aimbot_Projectile_Delta_Mode, 0); // 0 = Average, 1 = Max
	
	// Friction flags (Amalgam-style) - bitfield: 1 = CalculateIncrease, 2 = RunReduce
	CFGVAR(Aimbot_Projectile_Friction_Flags, 3);

	// Neckbreaker - iterate roll angles to bypass obstructions
	CFGVAR(Aimbot_Projectile_Neckbreaker, false);
	CFGVAR(Aimbot_Projectile_NeckbreakerStep, 90);

	// Crusader's Crossbow Healing Settings
	CFGVAR(Aimbot_Crossbow_Heal_Teammates, true); // Auto-aim at teammates who need healing
	CFGVAR(Aimbot_Crossbow_Heal_Priority, 0); // 0 = Lowest HP first, 1 = Closest first, 2 = FOV first

	// Amalgam Projectile Aimbot Settings
	CFGVAR(Aimbot_Amalgam_Projectile_Active, false); // Enable Amalgam projectile aimbot
	CFGVAR(Aimbot_Amalgam_Projectile_Splash, 1); // 0=Off, 1=Include, 2=Prefer, 3=Only (default Include)
	// Hitbox options (individual bools for multiselect)
	CFGVAR(Aimbot_Amalgam_Projectile_Hitbox_Auto, true);
	CFGVAR(Aimbot_Amalgam_Projectile_Hitbox_Head, true);
	CFGVAR(Aimbot_Amalgam_Projectile_Hitbox_Body, true);
	CFGVAR(Aimbot_Amalgam_Projectile_Hitbox_Feet, true);
	CFGVAR(Aimbot_Amalgam_Projectile_Hitbox_BodyaimLethal, false);
	CFGVAR(Aimbot_Amalgam_Projectile_Hitbox_PrioritizeFeet, true); // Prioritize feet for grounded targets
	// Modifier options
	CFGVAR(Aimbot_Amalgam_Projectile_Mod_PrimeTime, true); // Use prime time for projectile timing
	CFGVAR(Aimbot_Amalgam_Projectile_Mod_ChargeWeapon, true); // Auto-charge sticky/huntsman while aiming
	CFGVAR(Aimbot_Amalgam_Projectile_HitChance, 0); // 0 - 100% (hidden, always 0)
	CFGVAR(Aimbot_Amalgam_Projectile_SplashRadius, 100); // 0 - 100% (hidden, always 100)
	CFGVAR(Aimbot_Amalgam_Projectile_RocketSplashMode, 0); // 0=Regular, 1=SpecialLight, 2=SpecialHeavy
	CFGVAR(Aimbot_Amalgam_Projectile_SplashPoints, 75); // 50 - 400
	
	// Midpoint Aim - aims at the midpoint of target's predicted path for better hit chance
	CFGVAR(Aimbot_Projectile_Midpoint_Aim, false); // Enable midpoint aim
	CFGVAR(Aimbot_Projectile_Midpoint_Max_Distance, 9.5f); // Max path distance in feet (1 foot = 16 units)

	// Timed Double Donk - times Loose Cannon shots to explode ~0.5s after impact for mini-crit bonus
	CFGVAR(Aimbot_Projectile_Timed_Double_Donk, false); // Enable timed double donk for Loose Cannon
	CFGVAR(Aimbot_Projectile_Double_Donk_Delay, 0.01f); // Fuse expire delay after impact (-0.1 to 0.4 seconds)
	CFGVAR(Aimbot_Projectile_Cannon_Cancel_Charge, true); // Cancel Loose Cannon charge if target is lost

	// Behavior-based prediction - uses learned player behavior to improve aim
	CFGVAR(Aimbot_Projectile_Use_Dodge_Prediction, true); // Apply aim offset based on predicted dodge direction

	CFGVAR(Aimbot_Melee_Active, true);
	CFGVAR(Aimbot_Melee_Always_Active, false);
	CFGVAR(Aimbot_Melee_Target_LagRecords, true);
	CFGVAR(Aimbot_Melee_Aim_Type, 1); //0 Normal 1 Silent 2 Smooth
	CFGVAR(Aimbot_Melee_Sort, 0); //0 FOV 1 Distance
	CFGVAR(Aimbot_Melee_FOV, 180.0f);
	CFGVAR(Aimbot_Melee_Smoothing, 5.0f);
	CFGVAR(Aimbot_Melee_Predict_Swing, true);
	CFGVAR(Aimbot_Melee_Walk_To_Target, false);
	CFGVAR(Aimbot_Melee_Whip_Teammates, true);
	CFGVAR(Aimbot_Melee_Crouch_Airborne, false);
	CFGVAR(Aimbot_Melee_Auto_Repair, true); // Auto target friendly buildings that need repair/upgrade/ammo

	// Wrangler Aimbot - uses hitscan settings for aim type, FOV, smoothing
	// Uses projectile splash settings for splash prediction

#pragma endregion

#pragma region Triggerbot

	CFGVAR(Triggerbot_Active, false);
	CFGVAR(Triggerbot_Key, 0);

	CFGVAR(Triggerbot_AutoBackstab_Active, true);
	CFGVAR(Triggerbot_AutoBackstab_Always_On, true);
	CFGVAR(Triggerbot_AutoBackstab_Knife_If_Lethal, true);
	CFGVAR(Triggerbot_AutoBacktab_Mode, 1); //0 Legit 1 Rage
	CFGVAR(Triggerbot_AutoBacktab_Aim_Mode, 1); //0 Normal 1 Silent
	CFGVAR(Triggerbot_AutoBackstab_Ignore_Friends, false);
	CFGVAR(Triggerbot_AutoBackstab_Ignore_Invisible, false);
	CFGVAR(Triggerbot_AutoBackstab_Ignore_Invulnerable, true);
	CFGVAR(Triggerbot_AutoBackstab_Ignore_Razorback, true);

	CFGVAR(Triggerbot_AutoDetonate_Active, true);
	CFGVAR(Triggerbot_AutoDetonate_Always_On, true);
	CFGVAR(Triggerbot_AutoDetonate_Timer_Enabled, false);
	CFGVAR(Triggerbot_AutoDetonate_Timer_Value, 0.1f);
	CFGVAR(Triggerbot_AutoDetonate_DangerZone_Enabled, false);
	CFGVAR(Triggerbot_AutoDetonate_Target_Players, true);
	CFGVAR(Triggerbot_AutoDetonate_Target_Buildings, true);
	CFGVAR(Triggerbot_AutoDetonate_Ignore_Friends, false);
	CFGVAR(Triggerbot_AutoDetonate_Ignore_Invisible, true);
	CFGVAR(Triggerbot_AutoDetonate_Ignore_Invulnerable, true);

	CFGVAR(Triggerbot_AutoAirblast_Active, true);
	CFGVAR(Triggerbot_AutoAirblast_Mode, 1); //0 Legit 1 Rage
	CFGVAR(Triggerbot_AutoAirblast_Aim_Mode, 1); //0 Normal 1 Silent
	CFGVAR(Triggerbot_AutoAirblast_Aimbot_Support, true); // Use projectile aimbot to aim reflected rockets at enemies
	CFGVAR(Triggerbot_AutoAirblast_Ignore_Rocket, false);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_SentryRocket, false);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_Jar, false);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_JarGas, true);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_JarMilk, true);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_Arrow, true);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_Flare, true);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_Cleaver, false);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_HealingBolt, false);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_PipebombProjectile, false);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_BallOfFire, true);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_EnergyRing, false);
	CFGVAR(Triggerbot_AutoAirblast_Ignore_EnergyBall, false);

	CFGVAR(Triggerbot_AutoSapper_Active, true);
	CFGVAR(Triggerbot_AutoSapper_Always_On, true);
	CFGVAR(Triggerbot_AutoSapper_Mode, 1); // 0 Legit 1 Rage
	CFGVAR(Triggerbot_AutoSapper_Aim_Mode, 1); // 0 Normal 1 Silent
	CFGVAR(Triggerbot_AutoSapper_ESP, true); // Draw sapper range circles

#pragma endregion

#pragma region AutoVaccinator

	CFGVAR(Triggerbot_AutoVaccinator_Active, true);
	CFGVAR(Triggerbot_AutoVaccinator_Always_On, true);
	CFGVAR(Triggerbot_AutoVaccinator_NoPop, false); // When enabled with Always On, only cycles resistance without popping
	CFGVAR(Triggerbot_AutoVaccinator_Pop, 0); //0 everyone 1 friends only

#pragma endregion

#pragma region AutoUber

	CFGVAR(AutoUber_Active, true);
	CFGVAR(AutoUber_Always_On, true);
	CFGVAR(AutoUber_CritProjectile_Check, true);
	CFGVAR(AutoUber_SniperSightline_Check, true);
	CFGVAR(AutoUber_AutoHeal_Active, false);
	CFGVAR(AutoUber_AutoHeal_Friends_Only, false);

#pragma endregion

#pragma region ESP

	CFGVAR(ESP_Active, true);
	CFGVAR(ESP_Tracer_From, 1); //0 Top 1 Center 2 Bottom
	CFGVAR(ESP_Tracer_To, 2); //0 Top 1 Center 2 Bottom
	CFGVAR(ESP_Text_Color, 0); //0 Default 1 White

	CFGVAR(ESP_Players_Active, true);
	CFGVAR(ESP_Players_Alpha, 0.7f);
	CFGVAR(ESP_Players_Bones_Color, 1); //0 Default 1 White
	CFGVAR(ESP_Players_Arrows_Radius, 100.0f);
	CFGVAR(ESP_Players_Arrows_Max_Distance, 500.0f);
	CFGVAR(ESP_Players_Ignore_Local, false);
	CFGVAR(ESP_Players_Ignore_Friends, false);
	CFGVAR(ESP_Players_Ignore_Enemies, false);
	CFGVAR(ESP_Players_Ignore_Teammates, true);
	CFGVAR(ESP_Players_Ignore_Invisible, false);
	CFGVAR(ESP_Players_Ignore_Tagged, false); // When false, tagged players (Cheater/RetardLegit/Ignored) always show regardless of team
	CFGVAR(ESP_Players_Ignore_Tagged_Teammates, false); // When true, don't show ESP for tagged teammates (except medics if Show_Teammate_Medics is on)
	CFGVAR(ESP_Players_Show_Teammate_Medics, true);
	CFGVAR(ESP_Players_Name, false);
	CFGVAR(ESP_Players_Weapon_Name, false);
	CFGVAR(ESP_Players_Tags, false); // Show Friend/Cheater/RetardLegit/Ignored tags above player
	CFGVAR(ESP_Players_Class, false);
	CFGVAR(ESP_Players_Class_Icon, false);
	CFGVAR(ESP_Players_Health, false);
	CFGVAR(ESP_Players_HealthBar, false);
	CFGVAR(ESP_Players_Uber, false);
	CFGVAR(ESP_Players_UberBar, true);
	CFGVAR(ESP_Players_Box, false);
	CFGVAR(ESP_Players_Tracer, false);
	CFGVAR(ESP_Players_Bones, false);
	CFGVAR(ESP_Players_Arrows, true);
	CFGVAR(ESP_Players_Conds, true);
	CFGVAR(ESP_Players_Sniper_Lines, true);
	CFGVAR(ESP_Players_Show_F2P, false); // Show F2P tag on players
	CFGVAR(ESP_Players_Show_Party, false); // Show party indicator on players
	CFGVAR(ESP_Players_Behavior_Debug, false); // Debug: Show movement simulation confidence and learned behavior

	CFGVAR(ESP_Buildings_Active, true);
	CFGVAR(ESP_Buildings_Alpha, 0.7f);
	CFGVAR(ESP_Buildings_Ignore_Local, false);
	CFGVAR(ESP_Buildings_Ignore_Enemies, false);
	CFGVAR(ESP_Buildings_Ignore_Teammates, true);
	CFGVAR(ESP_Buildings_Show_Teammate_Dispensers, true);
	CFGVAR(ESP_Buildings_Name, false);
	CFGVAR(ESP_Buildings_Health, false);
	CFGVAR(ESP_Buildings_HealthBar, true);
	CFGVAR(ESP_Buildings_Level, false);
	CFGVAR(ESP_Buildings_LevelBar, false);
	CFGVAR(ESP_Buildings_Box, false);
	CFGVAR(ESP_Buildings_Tracer, false);
	CFGVAR(ESP_Buildings_Conds, true);

	CFGVAR(ESP_World_Active, false);
	CFGVAR(ESP_World_Alpha, 1.0f);
	CFGVAR(ESP_World_Ignore_HealthPacks, false);
	CFGVAR(ESP_World_Ignore_AmmoPacks, false);
	CFGVAR(ESP_World_Ignore_LocalProjectiles, false);
	CFGVAR(ESP_World_Ignore_EnemyProjectiles, false);
	CFGVAR(ESP_World_Ignore_TeammateProjectiles, true);
	CFGVAR(ESP_World_Ignore_Halloween_Gift, false);
	CFGVAR(ESP_World_Ignore_MVM_Money, false);
	CFGVAR(ESP_World_Name, true);
	CFGVAR(ESP_World_Box, false);
	CFGVAR(ESP_World_Tracer, false);

#pragma endregion

#pragma region Radar

	CFGVAR(Radar_Active, false);
	CFGVAR(Radar_Style, 1); //0 Rectangle 1 Circle
	CFGVAR(Radar_Size, 250);
	CFGVAR(Radar_Icon_Size, 18);
	CFGVAR(Radar_Radius, 1200.0f);
	CFGVAR(Radar_Cross_Alpha, 0.5f);
	CFGVAR(Radar_Outline_Alpha, 1.0f);
	CFGVAR(Radar_Background_Alpha, 0.9f);
	CFGVAR(Radar_Pos_X, 20);
	CFGVAR(Radar_Pos_Y, 20);

	CFGVAR(Radar_Players_Active, true);
	CFGVAR(Radar_Players_Ignore_Local, true);
	CFGVAR(Radar_Players_Ignore_Friends, false);
	CFGVAR(Radar_Players_Ignore_Teammates, true);
	CFGVAR(Radar_Players_Ignore_Enemies, false);
	CFGVAR(Radar_Players_Ignore_Invisible, false);
	CFGVAR(Radar_Players_Show_Teammate_Medics, true);

	CFGVAR(Radar_Buildings_Active, true);
	CFGVAR(Radar_Buildings_Ignore_Local, false);
	CFGVAR(Radar_Buildings_Ignore_Teammates, true);
	CFGVAR(Radar_Buildings_Ignore_Enemies, false);
	CFGVAR(Radar_Buildings_Show_Teammate_Dispensers, true);

	CFGVAR(Radar_World_Active, true);
	CFGVAR(Radar_World_Ignore_HealthPacks, false);
	CFGVAR(Radar_World_Ignore_AmmoPacks, false);
	CFGVAR(Radar_World_Ignore_Halloween_Gift, false);
	CFGVAR(Radar_World_Ignore_MVM_Money, false);

#pragma endregion

#pragma region Materials

	CFGVAR(Materials_Active, false);

	CFGVAR(Materials_Players_Active, false);
	CFGVAR(Materials_Players_No_Depth, false);
	CFGVAR(Materials_Players_Alpha, 1.0f);
	CFGVAR(Materials_Players_Material, 1); //0 Original 1 Flat 2 Shaded 3 Glossy 4 Glow 5 Plastic
	CFGVAR(Materials_Players_Ignore_Local, false);
	CFGVAR(Materials_Players_Ignore_Friends, false);
	CFGVAR(Materials_Players_Ignore_Enemies, false);
	CFGVAR(Materials_Players_Ignore_Teammates, true);
	CFGVAR(Materials_Players_Ignore_Tagged, false); // When false, tagged players (Cheater/RetardLegit/Ignored) always show regardless of team
	CFGVAR(Materials_Players_Ignore_Tagged_Teammates, false); // When true, don't show chams for tagged teammates (except medics if Show_Teammate_Medics is on)
	CFGVAR(Materials_Players_Ignore_LagRecords, false);
	CFGVAR(Materials_Players_Show_Teammate_Medics, false);
	CFGVAR(Materials_Players_LagRecords_Style, 1); //0 All 1 Last Only

	// Fake Model
	CFGVAR(Materials_FakeModel_Active, true);
	CFGVAR(Materials_FakeModel_Alpha, 0.5f);
	CFGVAR(Materials_FakeModel_Material, 1); //0 Original 1 Flat 2 Shaded 3 Glossy 4 Glow 5 Plastic

	// Lag Records Material
	CFGVAR(Materials_LagRecords_Active, true);
	CFGVAR(Materials_LagRecords_Alpha, 0.3f);
	CFGVAR(Materials_LagRecords_Material, 2); //0 Original 1 Flat 2 Shaded 3 Glossy 4 Glow 5 Plastic
	CFGVAR(Materials_LagRecords_Style, 1); //0 All 1 Last Only

	CFGVAR(Materials_Buildings_Active, false);
	CFGVAR(Materials_Buildings_No_Depth, false);
	CFGVAR(Materials_Buildings_Alpha, 0.30f);
	CFGVAR(Materials_Buildings_Material, 1); //0 Original 1 Flat 2 Shaded 3 Glossy 4 Glow 5 Plastic
	CFGVAR(Materials_Buildings_Ignore_Local, false);
	CFGVAR(Materials_Buildings_Ignore_Enemies, false);
	CFGVAR(Materials_Buildings_Ignore_Teammates, false);
	CFGVAR(Materials_Buildings_Show_Teammate_Dispensers, true);

	CFGVAR(Materials_World_Active, false);
	CFGVAR(Materials_World_No_Depth, false);
	CFGVAR(Materials_World_Alpha, 0.0f);
	CFGVAR(Materials_World_Material, 3); //0 Original 1 Flat 2 Shaded 3 Glossy 4 Glow 5 Plastic
	CFGVAR(Materials_World_Ignore_HealthPacks, false);
	CFGVAR(Materials_World_Ignore_AmmoPacks, false);
	CFGVAR(Materials_World_Ignore_LocalProjectiles, false);
	CFGVAR(Materials_World_Ignore_EnemyProjectiles, false);
	CFGVAR(Materials_World_Ignore_TeammateProjectiles, false);
	CFGVAR(Materials_World_Ignore_Halloween_Gift, false);
	CFGVAR(Materials_World_Ignore_MVM_Money, false);

	CFGVAR(Materials_ViewModel_Active, false);
	CFGVAR(Materials_ViewModel_Hands_Alpha, 0.5f);
	CFGVAR(Materials_ViewModel_Hands_Material, 1); //0 Original 1 Flat 2 Shaded 3 Glossy 4 Glow 5 Plastic
	CFGVAR(Materials_ViewModel_Weapon_Alpha, 1.0f);
	CFGVAR(Materials_ViewModel_Weapon_Material, 0); //0 Original 1 Flat 2 Shaded 3 Glossy 4 Glow 5 Plastic

#pragma endregion

#pragma region Outlines

	CFGVAR(Outlines_Active, true);
	CFGVAR(Outlines_Style, 1); //0 Bloom 1 Crisp 2 Cartoony 3 Cartoony Alt
	CFGVAR(Outlines_Bloom_Amount, 1);

	CFGVAR(Outlines_Players_Active, true);
	CFGVAR(Outlines_Players_Alpha, 1.0f);
	CFGVAR(Outlines_Players_Ignore_Local, false);
	CFGVAR(Outlines_Players_Ignore_Friends, false);
	CFGVAR(Outlines_Players_Ignore_Enemies, false);
	CFGVAR(Outlines_Players_Ignore_Teammates, true);
	CFGVAR(Outlines_Players_Ignore_Tagged, false); // When false, tagged players (Cheater/RetardLegit/Ignored) always show regardless of team
	CFGVAR(Outlines_Players_Ignore_Tagged_Teammates, false); // When true, don't show outlines for tagged teammates (except medics if Show_Teammate_Medics is on)
	CFGVAR(Outlines_Players_Show_Teammate_Medics, true);

	CFGVAR(Outlines_Buildings_Active, true);
	CFGVAR(Outlines_Buildings_Alpha, 1.0f);
	CFGVAR(Outlines_Buildings_Ignore_Local, false);
	CFGVAR(Outlines_Buildings_Ignore_Enemies, false);
	CFGVAR(Outlines_Buildings_Ignore_Teammates, true);
	CFGVAR(Outlines_Buildings_Show_Teammate_Dispensers, true);

	CFGVAR(Outlines_World_Active, true);
	CFGVAR(Outlines_World_Alpha, 1.0f);
	CFGVAR(Outlines_World_Ignore_HealthPacks, false);
	CFGVAR(Outlines_World_Ignore_AmmoPacks, false);
	CFGVAR(Outlines_World_Ignore_LocalProjectiles, false);
	CFGVAR(Outlines_World_Ignore_EnemyProjectiles, false);
	CFGVAR(Outlines_World_Ignore_TeammateProjectiles, true);
	CFGVAR(Outlines_World_Ignore_Halloween_Gift, false);
	CFGVAR(Outlines_World_Ignore_MVM_Money, false);

	// Misc Enemy - HP-based coloring
	CFGVAR(Visuals_Enemy_Outline_HP_Based, true); // Color enemy outlines based on HP
	CFGVAR(Visuals_Enemy_Materials_HP_Based, false); // Color enemy materials based on HP
	CFGVAR(Misc_Enemy_Custom_Name_Color, false); // Use custom color for all player names

#pragma endregion

#pragma region OtherVisuals

	CFGVAR(Visuals_Aimbot_FOV_Circle, true);
	CFGVAR(Visuals_Aimbot_FOV_Circle_Alpha, 0.53f);
	CFGVAR(Visuals_Aimbot_FOV_Circle_Color, Color_t({ 255, 255, 255, 255 }));  // Custom color for FOV circle
	CFGVAR(Visuals_Aimbot_FOV_Circle_RGB, false);  // RGB rainbow mode for FOV circle
	CFGVAR(Visuals_Aimbot_FOV_Circle_RGB_Rate, 3.0f);  // RGB color cycle rate
	CFGVAR(Visuals_Aimbot_FOV_Circle_Glow, false);  // Shader-based glow/bloom effect (like Paint)
	CFGVAR(Visuals_Aimbot_FOV_Circle_Bloom_Amount, 5);  // Bloom intensity (1-10)
	CFGVAR(Visuals_Crit_Indicator, false);
	CFGVAR(Visuals_Crit_Indicator_Pos_X, 801);
	CFGVAR(Visuals_Crit_Indicator_Pos_Y, 652);
	CFGVAR(Visuals_Crit_Indicator_Width, 140);
	CFGVAR(Visuals_Crit_Indicator_Height, 16);
	CFGVAR(Visuals_Crit_Indicator_TextMode, false);
	CFGVAR(Visuals_Crit_Indicator_TextSize, 100); // Percentage: 100-200
	CFGVAR(Visuals_Crit_Indicator_Debug, false);
	CFGVAR(Visuals_Draw_Movement_Path_Style, 1); //0 Off 1 Line 2 Separators 3 Spaced 4 Arrows 5 Boxes (Amalgam-style)
	CFGVAR(Visuals_Draw_Predicted_Path_Style, 1); //0 Off 1 Line 2 Separators 3 Spaced 4 Arrows 5 Boxes (Amalgam-style)
	CFGVAR(Visuals_Projectile_Trail, 0); //0 Default 1 None 2 Rocket 3 Critical 4 Energy 5 Charged 6 Ray 7 Fireball 8 Teleport 9 Fire 10 Flame 11 Sparks 12 Flare 13 Trail 14 Health 15 Smoke 16 Bubbles 17 Halloween 18 Monoculus 19 Sparkles 20 Rainbow
	
	// Amalgam Simulation Visuals
	CFGVAR(Visuals_Simulation_Movement_Style, 1); // 0=Off, 1=Line, 2=Dashed, 3=Arrows
	CFGVAR(Visuals_Simulation_Projectile_Style, 0); // 0=Off, 1=Line, 2=Dashed, 3=Arrows
	
	// Trajectory Preview (real-time projectile path visualization)
	CFGVAR(Visuals_Trajectory_Preview_Active, true); // Enable real-time trajectory preview
	CFGVAR(Visuals_Trajectory_Preview_Style, 0); // 0=Off, 1=Line, 2=Separators, 3=Spaced, 4=Arrows, 5=Boxes
	CFGVAR(Visuals_Trajectory_Preview_Box, true); // Draw box at impact point
	CFGVAR(Visuals_FOV_Override, 90.0f);
	CFGVAR(Visuals_Remove_Scope, false);
	CFGVAR(Visuals_Remove_Zoom, false);
	CFGVAR(Visuals_Remove_Punch, true);
	CFGVAR(Visuals_Remove_Screen_Overlay, true);
	CFGVAR(Visuals_Remove_Screen_Shake, true);
	CFGVAR(Visuals_Remove_Screen_Fade, true);
	CFGVAR(Visuals_Remove_MOTD, false);
	CFGVAR(Visuals_Removals_Mode, 1); //0 Everyone 1 Local Only
	CFGVAR(Visuals_Reveal_Scoreboard, true);
	CFGVAR(Visuals_Scoreboard_Utility, true);
	CFGVAR(Visuals_Tracer_Type, 0); //0 Default 1 C.A.P.P.E.R 2 Machina (White) 3 Machina (Team) 4 Big Nasty 5 Short Circuit 6 Mrasmus Zap 7 Random 8 Random (No Merasmus Zap)
	CFGVAR(Visuals_Crit_Tracer_Type, 0); //0 Off 1 C.A.P.P.E.R 2 Machina (White) 3 Machina (Team) 4 Big Nasty 5 Short Circuit 6 Mrasmus Zap 7 Random 8 Random (No Merasmus Zap)

	CFGVAR(Visuals_ViewModel_Active, false);
	CFGVAR(Visuals_ViewModel_Sway, false);
	CFGVAR(Visuals_ViewModel_Sway_Scale, 0.10f);
	CFGVAR(Visuals_ViewModel_Offset_Forward, 0.0f);
	CFGVAR(Visuals_ViewModel_Offset_Right, 0.0f);
	CFGVAR(Visuals_ViewModel_Offset_Up, 0.0f);
	CFGVAR(Visuals_ViewModel_Minimal, true);
	CFGVAR(Visuals_Viewmodel_Flip, false);
	CFGVAR(Visuals_ViewModel_WorldModel, false);

	CFGVAR(Visuals_Flat_Textures, false);
	CFGVAR(Visuals_Remove_Fog, false);
	CFGVAR(Visuals_Remove_Sky_Fog, true);
	CFGVAR(Visuals_Night_Mode, 0.0f);
	CFGVAR(Visuals_World_Modulation_Mode, 0); //0 Night 1 Custom Colors
	CFGVAR(Visuals_World_Modulation_No_Sky_Change, false);
	CFGVAR(Visuals_Distance_Prop_Alpha, false);

	CFGVAR(Visuals_Thirdperson_Active, false);
	CFGVAR(Visuals_Thirdperson_Key, 0);
	CFGVAR(Visuals_Thirdperson_Offset_Forward, 84.0f);
	CFGVAR(Visuals_Thirdperson_Offset_Right, 0.0f);
	CFGVAR(Visuals_Thirdperson_Offset_Up, 15.0f);

	CFGVAR(Visuals_SpectatorList_Active, true);
	CFGVAR(Visuals_SpectatorList_Avatars, true);
	CFGVAR(Visuals_SpectatorList_Outline_Alpha, 1.0f);
	CFGVAR(Visuals_SpectatorList_Background_Alpha, 0.9f);
	CFGVAR(Visuals_SpectatorList_Pos_X, 0);
	CFGVAR(Visuals_SpectatorList_Pos_Y, 275);
	CFGVAR(Visuals_SpectatorList_Width, 200);

	CFGVAR(Visuals_Ragdolls_Active, false);
	CFGVAR(Visuals_Ragdolls_No_Gib, false);
	CFGVAR(Visuals_Ragdolls_No_Death_Anim, false);
	CFGVAR(Visuals_Ragdolls_Effect, 0); //0 Default 1 Burning 2 Electrocuted 3 Ash 4 Gold 5 Ice 6 Dissolve 7 Random
	CFGVAR(Visuals_Ragdolls_Force_Mult, 1.0f);

	CFGVAR(Visuals_Paint_Active, false);
	CFGVAR(Visuals_Paint_Key, 0);
	CFGVAR(Visuals_Paint_Erase_Key, 0);
	CFGVAR(Visuals_Paint_LifeTime, 2.0f);
	CFGVAR(Visuals_Paint_Bloom_Amount, 7);

	CFGVAR(Visuals_Disable_Detail_Props, false);
	CFGVAR(Visuals_Disable_Ragdolls, false);
	CFGVAR(Visuals_Disable_Wearables, false);
	CFGVAR(Visuals_Disable_Post_Processing, false);
	CFGVAR(Visuals_Disable_Dropped_Weapons, false);
	CFGVAR(Visuals_Simple_Models, false);
	CFGVAR(Visuals_Auto_Interp, true); // Auto interp based on weapon type
	CFGVAR(Visuals_Particles_Mode, 0); //0 Original 1 Custom Color 2 Rainbow
	CFGVAR(Visuals_Particles_Rainbow_Rate, 10.0f);

	CFGVAR(Visuals_Beams_Active, false);
	CFGVAR(Visuals_Beams_LifeTime, 2.0f);
	CFGVAR(Visuals_Beams_Width, 6.0f);
	CFGVAR(Visuals_Beams_EndWidth, 1.0f);
	CFGVAR(Visuals_Beams_FadeLength, 2.0f);
	CFGVAR(Visuals_Beams_Amplitude, 3.1f);
	CFGVAR(Visuals_Beams_Speed, 0.0f);
	CFGVAR(Visuals_Beams_Flag_FBEAM_FADEIN, true);
	CFGVAR(Visuals_Beams_Flag_FBEAM_FADEOUT, true);
	CFGVAR(Visuals_Beams_Flag_FBEAM_SINENOISE, false);
	CFGVAR(Visuals_Beams_Flag_FBEAM_SOLID, false);
	CFGVAR(Visuals_Beams_Flag_FBEAM_SHADEIN, true);
	CFGVAR(Visuals_Beams_Flag_FBEAM_SHADEOUT, true);

	// Chat ESP - chat bubbles above player heads
	CFGVAR(Visuals_ChatESP_Active, false);
	CFGVAR(Visuals_ChatESP_Duration, 5.0f);
	CFGVAR(Visuals_ChatESP_MaxLength, 30);
	CFGVAR(Visuals_ChatESP_ShowPointer, true);
	CFGVAR(Visuals_ChatESP_MaxDistance, 500.0f); // Max distance to show bubbles

	CFGVAR(Visuals_SpyCamera_Active, false);
	CFGVAR(Visuals_SpyCamera_Background_Alpha, 0.9f);
	CFGVAR(Visuals_SpyCamera_Pos_X, 1598);
	CFGVAR(Visuals_SpyCamera_Pos_Y, -9);
	CFGVAR(Visuals_SpyCamera_Pos_W, 400);
	CFGVAR(Visuals_SpyCamera_Pos_H, 250);
	CFGVAR(Visuals_SpyCamera_FOV, 90.0f);

	CFGVAR(Viuals_SpyWarning_Active, true);
	CFGVAR(Viuals_SpyWarning_Announce, false);
	CFGVAR(Viuals_SpyWarning_Ignore_Cloaked, false);
	CFGVAR(Viuals_SpyWarning_Ignore_Friends, false);
	CFGVAR(Viuals_SpyWarning_Ignore_Invisible, false);

	CFGVAR(Visuals_TeamWellBeing_Active, false);
	CFGVAR(Visuals_TeamWellBeing_Medic_Only, false);
	CFGVAR(Visuals_TeamWellBeing_Background_Alpha, 0.9f);
	CFGVAR(Visuals_TeamWellBeing_Pos_X, 1402);
	CFGVAR(Visuals_TeamWellBeing_Pos_Y, 273);
	CFGVAR(Visuals_TeamWellBeing_Width, 200);

	CFGVAR(Visuals_Custom_Skybox_Texture_Name, std::string({ "" }));

	CFGVAR(Visuals_Chat_Teammate_Votes, true);
	CFGVAR(Visuals_Chat_Enemy_Votes, true);
	CFGVAR(Visuals_Chat_Player_List_Info, true);
	CFGVAR(Visuals_Chat_Name_Tags, true);
	CFGVAR(Visuals_Chat_Ban_Alerts, true);

	CFGVAR(Visuals_Weather, 0); // 0 = Off, 1 = Rain, 2 = Light Rain

	// Freecam
	CFGVAR(Visuals_Freecam_Key, 0);
	CFGVAR(Visuals_Freecam_Speed, 500.0f);
	CFGVAR(Visuals_Freecam_AspectRatio, 0.0f); // 0 = disabled (no change), otherwise custom aspect ratio

#pragma endregion

#pragma region Misc

	CFGVAR(Misc_Bunnyhop, true);
	CFGVAR(Misc_Choke_On_Bhop, false);
	CFGVAR(Misc_Crouch_While_Airborne, false);
	CFGVAR(Misc_Taunt_Slide, true);
	CFGVAR(Misc_Taunt_Slide_Control, true);
	CFGVAR(Misc_Taunt_Spin_Key, 0);
	CFGVAR(Misc_Taunt_Spin_Speed, 9.0f);
	CFGVAR(Misc_Taunt_Spin_Sine, false);
	CFGVAR_NOSAVE(Misc_Accuracy_Improvements, true);
	CFGVAR(Visuals_Disable_Interp, false);
	CFGVAR(Misc_Pure_Bypass, true);
	CFGVAR(Misc_NoiseMaker_Spam, false);
	CFGVAR(Misc_No_Push, false);
	CFGVAR(Misc_MVM_Giant_Weapon_Sounds, false);
	CFGVAR(Misc_Fake_Taunt, false);
	CFGVAR(Misc_Ping_Reducer, true); // Network fix (input delay fix)
	CFGVAR(Misc_Ping_Reducer_Active, false); // Enable ping reducer
	CFGVAR(Misc_Ping_Reducer_Value, 1.0f); // Ping reducer multiplier (0.1 - 1.0)
	CFGVAR(Misc_Pred_Error_Jitter_Fix, true);
	CFGVAR(Misc_SetupBones_Optimization, true);
	CFGVAR(Misc_ComputeLightingOrigin_Fix, true);
	CFGVAR(Misc_Equip_Region_Unlock, false);
	CFGVAR(Misc_Fast_Stop, false);
	CFGVAR(Misc_Fast_Accelerate, false);
	CFGVAR(Misc_Duck_Speed, false);
	CFGVAR(Misc_Auto_Strafe, true);
	CFGVAR(Misc_Auto_Strafe_Avoid_Walls, false);
	CFGVAR(Misc_Auto_Strafe_Turn_Scale, 0.40f);
	CFGVAR(Misc_Auto_Strafe_Max_Delta, 180.0f);
	CFGVAR(Misc_Shield_Turn_Rate, true);
	CFGVAR(Misc_Prevent_Server_Angle_Change, true);
	CFGVAR(Misc_Edge_Jump_Key, 0);
	CFGVAR(Misc_Auto_Rocket_Jump_Key, 0);
	CFGVAR(Misc_Auto_Rocket_Jump_Mode, 2); // 0 = Amalgam Style (High), 1 = Forward Style
	CFGVAR(Misc_Auto_Rocket_Jump_High_Forward_Bias, 138); // 0-2000: Lower = more forward, Higher = more vertical
	CFGVAR(Misc_Auto_Disguise, false);
	CFGVAR(Misc_Auto_Call_Medic_On_Damage, false);
	CFGVAR(Misc_Auto_Call_Medic_Low_HP, false);
	CFGVAR(Misc_Auto_Call_Medic_Low_HP_Class, 0);
	CFGVAR(Misc_Auto_Call_Medic_HP_Scout, 80);
	CFGVAR(Misc_Auto_Call_Medic_HP_Soldier, 100);
	CFGVAR(Misc_Auto_Call_Medic_HP_Pyro, 100);
	CFGVAR(Misc_Auto_Call_Medic_HP_Demoman, 90);
	CFGVAR(Misc_Auto_Call_Medic_HP_Heavy, 230);
	CFGVAR(Misc_Auto_Call_Medic_HP_Engineer, 80);
	CFGVAR(Misc_Auto_Call_Medic_HP_Sniper, 70);
	CFGVAR(Misc_Auto_Call_Medic_HP_Spy, 80);
	CFGVAR(Misc_Auto_Call_Medic_HP_Medic, 100);
	CFGVAR(Misc_Auto_Medigun_Key, 0);
	CFGVAR(Misc_Movement_Lock_Key, 0);
	CFGVAR(Misc_Clean_Screenshot, true);
	CFGVAR(Misc_Backpack_Expander, true);

	CFGVAR(Misc_AutoFaN_Key, 0);

	CFGVAR(Misc_MVM_Instant_Respawn_Key, 0);
	CFGVAR(Misc_MVM_Instant_Revive, false);

	CFGVAR(Misc_AntiCheat_Enabled, false);
	CFGVAR(Misc_AntiCheat_SkipCritDetection, false);
	CFGVAR(Misc_AntiCheat_IgnoreTickLimit, false);

	CFGVAR(Exploits_Shifting_Recharge_Key, 0);
	CFGVAR(Exploits_Shifting_Recharge_Limit, 24); // 2-24, max ticks to recharge (accounts for fakeangle 2 ticks, anticheat)

	CFGVAR(Exploits_RapidFire_Key, 0);
	CFGVAR(Exploits_FastSticky_Key, 0);  // Fast sticky shooting - auto recharges and fires stickies rapidly
	CFGVAR(Exploits_RapidFire_Ticks, 21);
	CFGVAR(Exploits_RapidFire_Min_Ticks_Target_Same, 3);
	CFGVAR(Exploits_RapidFire_Antiwarp, true);
	CFGVAR(Exploits_RapidFire_Max_Commands, 14);  // Max commands per packet during NORMAL play (lower = better for high ping)
	CFGVAR(Exploits_RapidFire_DT_Commands, 23);   // Max commands per packet during DOUBLETAP (higher = faster DT)
	CFGVAR(Exploits_RapidFire_Tick_Tracking, 0);  // 0=Disabled, 1=Linear
	CFGVAR(Exploits_RapidFire_Deficit_Tracking, true);  // Auto-recover when server rejects commands
	
	CFGVAR(Exploits_Warp_Key, 0);
	CFGVAR(Exploits_Warp_Mode, 1); //0 Slow 1 Full
	CFGVAR(Exploits_Warp_Exploit, 0); //0 None 1 Fake Peek 2 0 Velocity
	CFGVAR(Exploits_Shifting_Draw_Indicator, false);
	CFGVAR(Exploits_Shifting_Indicator_Style, 0); //0 Rectangle 1 Circle
	CFGVAR(Exploits_Shifting_FakeLag_Text_X, 16);
	CFGVAR(Exploits_Shifting_FakeLag_Text_Y, 2);
	CFGVAR(Exploits_Shifting_FakeLag_Text_Size, 100); // Percentage: 50-200

	CFGVAR(Exploits_FakeLag_Enabled, false);
	CFGVAR(Exploits_FakeLag_Only_Moving, false);
	CFGVAR(Exploits_FakeLag_Activate_On_Sightline, false);
;
	CFGVAR(Exploits_FakeLag_Max_Ticks, 12);

	// Anti-Aim (uses tick shifting to show fake angles)
	CFGVAR(Exploits_AntiAim_Enabled, false);
	// Pitch: 0=None, 1=Up, 2=Down, 3=Zero, 4=Jitter, 5=ReverseJitter
	CFGVAR(Exploits_AntiAim_PitchReal, 0);
	CFGVAR(Exploits_AntiAim_PitchFake, 0); // 0=None, 1=Up, 2=Down, 3=Jitter, 4=ReverseJitter
	// Yaw: 0=Forward, 1=Left, 2=Right, 3=Backwards, 4=Edge, 5=Jitter, 6=Spin
	CFGVAR(Exploits_AntiAim_YawReal, 0);
	CFGVAR(Exploits_AntiAim_YawFake, 0);
	// YawBase: 0=View, 1=Target
	CFGVAR(Exploits_AntiAim_RealYawBase, 0);
	CFGVAR(Exploits_AntiAim_FakeYawBase, 0);
	// Offsets and values
	CFGVAR(Exploits_AntiAim_RealYawOffset, 0.0f); // -180 to 180
	CFGVAR(Exploits_AntiAim_FakeYawOffset, 0.0f); // -180 to 180
	CFGVAR(Exploits_AntiAim_RealYawValue, 90.0f); // For Edge/Jitter modes
	CFGVAR(Exploits_AntiAim_FakeYawValue, -90.0f); // For Edge/Jitter modes
	CFGVAR(Exploits_AntiAim_SpinSpeed, 15.0f); // Spin speed
	// Options
	CFGVAR(Exploits_AntiAim_MinWalk, true); // Prevent standing still detection
	CFGVAR(Exploits_AntiAim_AntiOverlap, false); // Prevent real/fake overlap
	CFGVAR(Exploits_AntiAim_InvalidShootPitch, false); // Hide pitch on shot

	// Legit Anti-Aim - automatic yaw offsets based on class/weapon/movement
	CFGVAR(Exploits_LegitAA_Enabled, false);

	CFGVAR(Exploits_Crits_Force_Crit_Key, 0);
	CFGVAR(Exploits_Crits_Force_Crit_Key_Melee, 0);
	CFGVAR(Exploits_Crits_Skip_Random_Crits, true);
	CFGVAR(Exploits_Crits_Ignore_Ban, false);

	CFGVAR(Exploits_SeedPred_Active, true);
	CFGVAR(Exploits_SeedPred_DrawIndicator, false);

	// Chat Spammer
	CFGVAR(Misc_Chat_Spammer_Active, false);
	CFGVAR(Misc_Chat_Spammer_Interval, 1.0f);
	
	// Killsay
	CFGVAR(Misc_Chat_Killsay_Active, false);
	CFGVAR(Misc_Chat_Killsay_Tagged_Only, false);

	CFGVAR(Misc_Freeze_Queue, false);
	CFGVAR(Misc_Auto_Queue, false);
	CFGVAR(Misc_Auto_Accept_Items, false);
	CFGVAR(Misc_Anti_AFK, true);

	// Region Selector (Force Regions)
	CFGVAR(Exploits_Region_Selector_Active, false);
	// North America
	CFGVAR(Exploits_Region_ATL, false); // Atlanta
	CFGVAR(Exploits_Region_ORD, false); // Chicago
	CFGVAR(Exploits_Region_DFW, false); // Dallas
	CFGVAR(Exploits_Region_LAX, false); // Los Angeles
	CFGVAR(Exploits_Region_SEA, false); // Seattle
	CFGVAR(Exploits_Region_IAD, false); // Virginia
	// Europe
	CFGVAR(Exploits_Region_AMS, false); // Amsterdam
	CFGVAR(Exploits_Region_FRA, false); // Frankfurt
	CFGVAR(Exploits_Region_HEL, false); // Helsinki
	CFGVAR(Exploits_Region_LHR, false); // London
	CFGVAR(Exploits_Region_MAD, false); // Madrid
	CFGVAR(Exploits_Region_PAR, false); // Paris
	CFGVAR(Exploits_Region_STO, false); // Stockholm
	CFGVAR(Exploits_Region_VIE, false); // Vienna
	CFGVAR(Exploits_Region_WAW, false); // Warsaw
	// South America
	CFGVAR(Exploits_Region_EZE, false); // Buenos Aires
	CFGVAR(Exploits_Region_LIM, false); // Lima
	CFGVAR(Exploits_Region_SCL, false); // Santiago
	CFGVAR(Exploits_Region_GRU, false); // Sao Paulo
	// Asia
	CFGVAR(Exploits_Region_MAA, false); // Chennai
	CFGVAR(Exploits_Region_DXB, false); // Dubai
	CFGVAR(Exploits_Region_HKG, false); // Hong Kong
	CFGVAR(Exploits_Region_BOM, false); // Mumbai
	CFGVAR(Exploits_Region_SEO, false); // Seoul
	CFGVAR(Exploits_Region_SGP, false); // Singapore
	CFGVAR(Exploits_Region_TYO, false); // Tokyo
	// Australia
	CFGVAR(Exploits_Region_SYD, false); // Sydney
	// Africa
	CFGVAR(Exploits_Region_JNB, false); // Johannesburg
	
	CFGVAR(Misc_Projectile_Dodge_Enabled, false);
	CFGVAR(Misc_Projectile_Dodge_Use_Warp, false);
	CFGVAR(Misc_Projectile_Dodge_Only_Warp, false);
	CFGVAR(Misc_Projectile_Dodge_Disable_DT_Airborne, false);

#pragma endregion

#pragma region Colors

	CFGVAR(Color_Local, Color_t({ 43, 203, 186, 255 }));
	CFGVAR(Color_Friend, Color_t({ 38, 222, 129, 255 }));
	CFGVAR(Color_Enemy, Color_t({ 253, 159, 19, 255 }));
	CFGVAR(Color_Teammate, Color_t({ 196, 1, 235, 255 }));
	CFGVAR(Color_Target, Color_t({ 57, 88, 254, 255 }));
	CFGVAR(Color_Invulnerable, Color_t({ 165, 94, 234, 255 }));
	CFGVAR(Color_Invisible, Color_t({ 209, 216, 224, 255 }));
	CFGVAR(Color_Cheater, Color_t({ 248, 253, 8, 255 }));
	CFGVAR(Color_RetardLegit, Color_t({ 253, 3, 11, 255 }));
	CFGVAR(Color_Targeted, Color_t({ 255, 100, 0, 255 }));   // Orange for Targeted (same priority as Cheater)
	CFGVAR(Color_Streamer, Color_t({ 138, 43, 226, 255 }));  // Purple for Streamer (same priority as RetardLegit)
	CFGVAR(Color_Nigger, Color_t({ 139, 69, 19, 255 }));     // Brown for Nigger (same priority as Cheater)
	CFGVAR(Color_F2P, Color_t({ 200, 200, 200, 255 })); // F2P player tag color
	CFGVAR(Color_Custom_Name, Color_t({ 255, 255, 255, 255 })); // Custom name color for all players
	CFGVAR(Color_FakeModel, Color_t({ 0, 204, 204, 255 })); // Cyan for fake model
	CFGVAR(Color_LagRecord, Color_t({ 255, 255, 255, 255 })); // White for lag records
	// Party colors (12 unique colors for different parties)
	CFGVAR(Color_Party_1, Color_t({ 100, 50, 255, 255 }));   // Purple (local party)
	CFGVAR(Color_Party_2, Color_t({ 255, 100, 100, 255 }));  // Red
	CFGVAR(Color_Party_3, Color_t({ 100, 255, 100, 255 }));  // Green
	CFGVAR(Color_Party_4, Color_t({ 255, 255, 100, 255 }));  // Yellow
	CFGVAR(Color_Party_5, Color_t({ 100, 255, 255, 255 }));  // Cyan
	CFGVAR(Color_Party_6, Color_t({ 255, 100, 255, 255 }));  // Magenta
	CFGVAR(Color_Party_7, Color_t({ 255, 165, 0, 255 }));    // Orange
	CFGVAR(Color_Party_8, Color_t({ 0, 128, 255, 255 }));    // Blue
	CFGVAR(Color_Party_9, Color_t({ 128, 255, 0, 255 }));    // Lime
	CFGVAR(Color_Party_10, Color_t({ 255, 0, 128, 255 }));   // Pink
	CFGVAR(Color_Party_11, Color_t({ 0, 255, 128, 255 }));   // Teal
	CFGVAR(Color_Party_12, Color_t({ 128, 0, 255, 255 }));   // Violet
	CFGVAR(Color_OverHeal, Color_t({ 69, 170, 242, 255 }));
	CFGVAR(Color_Uber, Color_t({ 224, 86, 253, 255 }));
	CFGVAR(Color_Conds, Color_t({ 249, 202, 36, 255 }));
	CFGVAR(Color_HealthPack, Color_t({ 46, 204, 113, 255 }));
	CFGVAR(Color_AmmoPack, Color_t({ 200, 200, 200, 255 }));
	CFGVAR(Color_Beams, Color_t({ 200, 200, 200, 255 }));
	CFGVAR(Color_Halloween_Gift, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_MVM_Money, Color_t({ 0, 200, 20, 255 }));
	CFGVAR(Color_Particles, Color_t({ 253, 75, 19, 255 }));
	CFGVAR(Color_World, Color_t({ 79, 113, 254, 255 }));
	CFGVAR(Color_Sky, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_Props, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_Hands, Color_t({ 26, 137, 253, 255 }));
	CFGVAR(Color_Hands_Sheen, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_Weapon, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_Weapon_Sheen, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_Simulation_Movement, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_Simulation_Projectile, Color_t({ 255, 255, 255, 255 }));
	CFGVAR(Color_Trajectory, Color_t({ 255, 255, 255, 255 })); // Single trajectory color
	CFGVAR(Color_FakeLag, Color_t({ 255, 165, 0, 255 })); // FakeLag indicator color (orange)

	CFGVAR_NOSAVE(Color_ESP_Text, Color_t({ 200, 200, 200, 255 }));
	CFGVAR_NOSAVE(Color_ESP_Outline, Color_t({ 10, 10, 10, 255 }));

#pragma endregion

#pragma region Menu

	CFGVAR_NOSAVE(Menu_Pos_X, 500);
	CFGVAR_NOSAVE(Menu_Pos_Y, 200);
	CFGVAR_NOSAVE(Menu_Width, 555);
	CFGVAR_NOSAVE(Menu_Height, 620);
	CFGVAR_NOSAVE(Menu_Drag_Bar_Height, 15);

	CFGVAR_NOSAVE(Menu_Spacing_X, 3);
	CFGVAR_NOSAVE(Menu_Spacing_Y, 3);

	CFGVAR_NOSAVE(Menu_Tab_Button_Width, 60);
	CFGVAR_NOSAVE(Menu_Tab_Button_Height, 18);

	CFGVAR_NOSAVE(Menu_CheckBox_Width, 10);
	CFGVAR_NOSAVE(Menu_CheckBox_Height, 10);

	CFGVAR_NOSAVE(Menu_Slider_Width, 100);
	CFGVAR_NOSAVE(Menu_Slider_Height, 6);

	CFGVAR_NOSAVE(Menu_InputKey_Width, 60);
	CFGVAR_NOSAVE(Menu_InputKey_Height, 14);

	CFGVAR_NOSAVE(Menu_InputText_Width, 150);
	CFGVAR_NOSAVE(Menu_InputText_Height, 30);

	CFGVAR_NOSAVE(Menu_Select_Width, 120);
	CFGVAR_NOSAVE(Menu_Select_Height, 14);

	CFGVAR_NOSAVE(Menu_ColorPicker_Preview_Width, 10);
	CFGVAR_NOSAVE(Menu_ColorPicker_Preview_Height, 10);

	CFGVAR(Menu_Text, Color_t({ 240, 240, 240, 255 }));
	CFGVAR(Menu_Text_Active, Color_t({ 220, 220, 220, 255 }));
	CFGVAR(Menu_Text_Inactive, Color_t({ 160, 160, 160, 255 }));
	CFGVAR(Menu_Text_Disabled, Color_t({ 100, 100, 100, 255 }));

	CFGVAR(Menu_Accent_Primary, Color_t({ 79, 91, 253, 255 }));
	CFGVAR(Menu_Accent_Secondary, Color_t({ 101, 98, 243, 156 }));
	CFGVAR(Menu_Accent_Secondary_RGB, false);  // RGB rainbow mode for accent secondary
	CFGVAR(Menu_Accent_Secondary_RGB_Rate, 3.0f);  // RGB color cycle rate
	CFGVAR(Menu_Background, Color_t({ 0, 6, 20, 255 }));

	CFGVAR(Menu_Snow, false);
	
	// Background image
	CFGVAR(Menu_Background_Image_Enabled, false);
	CFGVAR(Menu_Background_Image_Transparency, 0.5f);

	// Draggable GroupBox positions (column * 100 + order)
	// Misc tab
	CFGVAR(Menu_GroupBox_Misc_Misc, 200);    // Right column, order; 0
	CFGVAR(Menu_GroupBox_Misc_Game, 201);    // Right column, order 1
	CFGVAR(Menu_GroupBox_Misc_MvM, 102);     // Middle column, order 2
	CFGVAR(Menu_GroupBox_Misc_Chat, 101);    // Middle column, order 1
	CFGVAR(Menu_GroupBox_Misc_Taunt, 103);   // Middle column, order 3
	CFGVAR(Menu_GroupBox_Misc_Auto, 100);    // Middle column, order 0
	CFGVAR(Menu_GroupBox_Misc_Movement, 0);  // Left column, order 0

	// Exploits tab
	CFGVAR(Menu_GroupBox_Exploits_Shifting, 0);
	CFGVAR(Menu_GroupBox_Exploits_FakeLag, 200);
	CFGVAR(Menu_GroupBox_Exploits_AntiAim, 202);
	CFGVAR(Menu_GroupBox_Exploits_Crits, 100);
	CFGVAR(Menu_GroupBox_Exploits_NoSpread, 201);
	CFGVAR(Menu_GroupBox_Exploits_RegionSelector, 101);

	// Aim tab
	CFGVAR(Menu_GroupBox_Aim_General, 0);
	CFGVAR(Menu_GroupBox_Aim_Hitscan, 100);
	CFGVAR(Menu_GroupBox_Aim_Projectile, 200);
	CFGVAR(Menu_GroupBox_Aim_Melee, 201);

#pragma endregion
}
