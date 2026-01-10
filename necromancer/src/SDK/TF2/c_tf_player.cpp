#include "c_tf_player.h"

#include "../../App/Features/Players/Players.h"

bool C_TFPlayer::IsPlayerOnSteamFriendsList()
{
	auto result{ reinterpret_cast<bool(__fastcall *)(void *, void *)>(Signatures::CTFPlayer_IsPlayerOnSteamFriendsList.Get())(this, this) };

	if (!result)
	{
		PlayerPriority info{};

		return F::Players->GetInfo(entindex(), info) && info.Ignored;
	}

	return result;
}

const char* C_TFPlayer::GetWeaponName()
{
	if (!this->m_hActiveWeapon().Get()) return "";

	switch (this->m_hActiveWeapon().Get()->As<C_TFWeaponBase>()->m_iItemDefinitionIndex())
	{
	case Scout_m_ForceANature:
	case Scout_m_FestiveForceANature: { return "FORCE-A-NATURE"; }
	case Scout_m_FestiveScattergun: { return "SCATTERGUN"; }
	case Scout_m_BackcountryBlaster: { return "BACK SCATTER"; }
	case Scout_s_MutatedMilk: { return "MAD MILK"; }
	case Scout_s_TheWinger: { return "WINGER"; }
	case Scout_s_FestiveBonk:
	case Scout_s_BonkAtomicPunch: { return "BONK ATOMIC PUNCH"; }
	case Scout_s_PrettyBoysPocketPistol: { return "POCKET PISTOL"; }
	case Scout_s_CritaCola: { return "CRIT A COLA"; }
	case Scout_t_FestiveBat: { return "BAT"; }
	case Scout_t_FestiveHolyMackerel: { return "HOLY MACKEREL"; }
	case Scout_t_TheAtomizer: { return "ATOMIZER"; }
	case Scout_t_TheCandyCane: { return "CANDY CANE"; }
	case Scout_t_TheFanOWar: { return "FAN O WAR"; }
	case Scout_t_SunonaStick: { return "SUN ON A STICK"; }
	case Scout_t_TheBostonBasher: { return "BOSTON BASHER"; }
	case Scout_t_ThreeRuneBlade: { return "THREE RUNE BLADE"; }
	case Scout_t_TheFreedomStaff: { return "FREEDOM STAFF"; }
	case Scout_t_TheBatOuttaHell: { return "BAT OUTTA HELL"; }
	case Scout_s_Lugermorph:
	case Scout_s_VintageLugermorph: { return "LUGERMORPH"; }
	case Scout_s_TheCAPPER: { return "C.A.P.P.E.R"; }
	case Scout_t_UnarmedCombat: { return "UNARMED COMBAT"; }
	case Scout_t_Batsaber: { return "BATSABER"; }
	case Scout_t_TheHamShank: { return "HAM SHANK"; }
	case Scout_t_TheNecroSmasher: { return "NEGRO SMASHER"; }
	case Scout_t_TheConscientiousObjector: { return "OBJECTOR"; }
	case Scout_t_TheCrossingGuard: { return "CROSSING GUARD"; }
	case Scout_t_TheMemoryMaker: { return "MEMORY MAKER"; }

	case Soldier_m_FestiveRocketLauncher: { return "ROCKET LAUNCHER"; }
	case Soldier_m_RocketJumper: { return "ROCKET JUMPER"; }
	case Soldier_m_TheAirStrike: { return "AIR STRIKE"; }
	case Soldier_m_TheLibertyLauncher: { return "LIBERTY LAUNCHER"; }
	case Soldier_m_TheOriginal: { return "ORIGINAL"; }
	case Soldier_m_FestiveBlackBox:
	case Soldier_m_TheBlackBox: { return "BLACK BOX"; }
	case Soldier_m_TheBeggarsBazooka: { return "BEGGARS BAZOOKA"; }
	case Soldier_s_FestiveShotgun: { return "SHOTGUN"; }
	case Soldier_s_FestiveBuffBanner: { return "BUFF BANNER"; }
	case Soldier_s_TheConcheror: { return "CONCHEROR"; }
	case Soldier_s_TheBattalionsBackup: { return "BATTALIONS BACKUP"; }
	case Soldier_s_PanicAttack: { return "PANIC ATTACK"; }
	case Soldier_t_TheMarketGardener: { return "MARKET GARDENER"; }
	case Soldier_t_TheDisciplinaryAction: { return "DISCIPLINARY ACTION"; }
	case Soldier_t_TheEqualizer: { return "EQUALIZER"; }
	case Soldier_t_ThePainTrain: { return "PAIN TRAIN"; }
	case Soldier_t_TheHalfZatoichi: { return "HALF ZATOICHI"; }

	case Pyro_m_FestiveFlameThrower: { return "FLAME THROWER"; }
	case Pyro_m_ThePhlogistinator: { return "PHLOGISTINATOR"; }
	case Pyro_m_FestiveBackburner:
	case Pyro_m_TheBackburner: { return "BACKBURNER"; }
	case Pyro_m_TheRainblower: { return "RAINBLOWER"; }
	case Pyro_m_TheDegreaser: { return "DEGREASER"; }
	case Pyro_m_NostromoNapalmer: { return "NOSTROMO NAPALMER"; }
	case Pyro_s_FestiveFlareGun: { return "FLARE GUN"; }
	case Pyro_s_TheScorchShot: { return "SCORCH SHOT"; }
	case Pyro_s_TheDetonator: { return "DETONATOR"; }
	case Pyro_s_TheReserveShooter: { return "RESERVE SHOOTER"; }
	case Pyro_t_TheFestiveAxtinguisher:
	case Pyro_t_TheAxtinguisher: { return "AXTINGUISHER"; }
	case Pyro_t_Homewrecker: { return "HOMEWRECKER"; }
	case Pyro_t_ThePowerjack: { return "POWERJACK"; }
	case Pyro_t_TheBackScratcher: { return "BACK SCRATCHER"; }
	case Pyro_t_TheThirdDegree: { return "THIRD DEGREE"; }
	case Pyro_t_ThePostalPummeler: { return "POSTAL PUMMELER"; }
	case Pyro_t_PrinnyMachete: { return "PRINNY MACHETE"; }
	case Pyro_t_SharpenedVolcanoFragment: { return "VOLCANO FRAGMENT"; }
	case Pyro_t_TheMaul: { return "MAUL"; }
	case Pyro_t_TheLollichop: { return "LOLLICHOP"; }

	case Demoman_m_FestiveGrenadeLauncher: { return "GRENADE LAUNCHER"; }
	case Demoman_m_TheIronBomber: { return "IRON BOMBER"; }
	case Demoman_m_TheLochnLoad: { return "LOCH N LOAD"; }
	case Demoman_s_FestiveStickybombLauncher: { return "STICKYBOMB LAUNCHER"; }
	case Demoman_s_StickyJumper: { return "STICKY JUMPER"; }
	case Demoman_s_TheQuickiebombLauncher: { return "QUICKIEBOMB LAUNCHER"; }
	case Demoman_s_TheScottishResistance: { return "SCOTTISH RESISTANCE"; }
	case Demoman_t_HorselessHeadlessHorsemannsHeadtaker: { return "HORSEMANNS HEADTAKER"; }
	case Demoman_t_TheScottishHandshake: { return "SCOTTISH HANDSHAKE"; }
	case Demoman_t_FestiveEyelander:
	case Demoman_t_TheEyelander: { return "EYELANDER"; }
	case Demoman_t_TheScotsmansSkullcutter: { return "SCOTSMANS SKULLCUTTER"; }
	case Demoman_t_ThePersianPersuader: { return "PERSIAN PERSUADER"; }
	case Demoman_t_NessiesNineIron: { return "NESSIES NINE IRON"; }
	case Demoman_t_TheClaidheamhMor: { return "CLAIDHEAMH MOR"; }

	case Heavy_m_IronCurtain: { return "IRON CURTAIN"; }
	case Heavy_m_FestiveMinigun: { return "MINIGUN"; }
	case Heavy_m_Tomislav: { return "TOMISLAV"; }
	case Heavy_m_TheBrassBeast: { return "BRASS BEAST"; }
	case Heavy_m_Natascha: { return "NATASCHA"; }
	case Heavy_m_TheHuoLongHeaterG:
	case Heavy_m_TheHuoLongHeater: { return "HUO-LONG HEATER"; }
	case Heavy_s_TheFamilyBusiness: { return "FAMILY BUSINESS"; }
	case Heavy_s_FestiveSandvich:
	case Heavy_s_RoboSandvich:
	case Heavy_s_Sandvich: { return "SANDVICH"; }
	case Heavy_s_Fishcake: { return "FISHCAKE"; }
	case Heavy_s_SecondBanana: { return "BANANA"; }
	case Heavy_s_TheDalokohsBar: { return "CHOCOLATE"; }
	case Heavy_s_TheBuffaloSteakSandvich: { return "STEAK"; }
	case Heavy_t_FistsofSteel: { return "FISTS OF STEEL"; }
	case Heavy_t_TheHolidayPunch: { return "HOLIDAY PUNCH"; }
	case Heavy_t_WarriorsSpirit: { return "WARRIORS SPIRIT"; }
	case Heavy_t_TheEvictionNotice: { return "EVICTION NOTICE"; }
	case Heavy_t_TheKillingGlovesofBoxing: { return "KILLING GLOVES OF BOXING"; }
	case Heavy_t_ApocoFists: { return "APOCO-FISTS"; }
	case Heavy_t_FestiveGlovesofRunningUrgently:
	case Heavy_t_GlovesofRunningUrgently: { return "GLOVES OF RUNNING URGENTLY"; }
	case Heavy_t_TheBreadBite: { return "BREAD BITE"; }

	case Engi_m_FestiveFrontierJustice: { return "FRONTIER JUSTICE"; }
	case Engi_m_TheWidowmaker: { return "WIDOWMAKER"; }
	case Engi_s_TheGigarCounter:
	case Engi_s_FestiveWrangler: { return "WRANGLER"; }
	case Engi_s_TheShortCircuit: { return "SHORT CIRCUIT"; }
	case Engi_t_FestiveWrench: { return "WRENCH"; }
	case Engi_t_GoldenWrench: { return "GOLDEN WRENCH"; }
	case Engi_t_TheGunslinger: { return "GUNSLINGER"; }
	case Engi_t_TheJag: { return "JAG"; }
	case Engi_t_TheEurekaEffect: { return "EUREKA EFFECT"; }
	case Engi_t_TheSouthernHospitality: { return "SOUTHERN HOSPITALITY"; }

	case Medic_m_FestiveCrusadersCrossbow: { return "CRUSADERS CROSSBOW"; }
	case Medic_m_TheOverdose: { return "OVERDOSE"; }
	case Medic_m_TheBlutsauger: { return "BLUTSAUGER"; }
	case Medic_s_FestiveMediGun: { return "MEDIGUN"; }
	case Medic_s_TheQuickFix: { return "QUICK FIX"; }
	case Medic_s_TheKritzkrieg: { return "KRITZKRIEG"; }
	case Medic_s_TheVaccinator: { return "VACCINATOR"; }
	case Medic_t_FestiveBonesaw: { return "BONESAW"; }
	case Medic_t_FestiveUbersaw:
	case Medic_t_TheUbersaw: { return "UBERSAW"; }
	case Medic_t_TheVitaSaw: { return "VITASAW"; }
	case Medic_t_TheSolemnVow: { return "SOLEMN VOW"; }
	case Medic_t_Amputator: { return "AMPUTATOR"; }

	case Sniper_m_FestiveSniperRifle: { return "SNIPER RIFLE"; }
	case Sniper_m_FestiveHuntsman:
	case Sniper_m_TheHuntsman: { return "HUNTSMAN"; }
	case Sniper_m_TheMachina: { return "MACHINA"; }
	case Sniper_m_TheAWPerHand: { return "AWPER HAND"; }
	case Sniper_m_TheHitmansHeatmaker: { return "HITMANS HEATMAKER"; }
	case Sniper_m_TheSydneySleeper: { return "SYDNEY SLEEPER"; }
	case Sniper_m_ShootingStar: { return "SHOOTING STAR"; }
	case Sniper_s_FestiveJarate: { return "JARATE"; }
	case Sniper_s_TheSelfAwareBeautyMark: { return "JARATE"; }
	case Sniper_s_FestiveSMG: { return "SMG"; }
	case Sniper_t_TheBushwacka: { return "BUSHWACKA"; }
	case Sniper_t_KukriR:
	case Sniper_t_Kukri: { return "KUKRI"; }
	case Sniper_t_TheShahanshah: { return "SHAHANSHAH"; }
	case Sniper_t_TheTribalmansShiv: { return "TRIBALMANS SHIV"; }

	case Spy_m_FestiveRevolver: { return "REVOLVER"; }
	case Spy_m_FestiveAmbassador:
	case Spy_m_TheAmbassador: { return "AMBASSADOR"; }
	case Spy_m_BigKill: { return "BIG KILL"; }
	case Spy_m_TheDiamondback: { return "DIAMONDBACK"; }
	case Spy_m_TheEnforcer: { return "ENFORCER"; }
	case Spy_m_LEtranger: { return "LETRANGER"; }
	case Spy_s_Sapper:
	case Spy_s_SapperR:
	case Spy_s_FestiveSapper: { return "SAPPER"; }
	case Spy_s_TheRedTapeRecorder:
	case Spy_s_TheRedTapeRecorderG: { return "RED TAPE RECORDER"; }
	case Spy_s_TheApSapG: { return "AP-SAP"; }
	case Spy_s_TheSnackAttack: { return "SNACK ATTACK"; }
	case Spy_t_FestiveKnife: { return "KNIFE"; }
	case Spy_t_ConniversKunai: { return "KUNAI"; }
	case Spy_t_YourEternalReward: { return "YOUR ETERNAL REWARD"; }
	case Spy_t_TheBigEarner: { return "BIG EARNER"; }
	case Spy_t_TheSpycicle: { return "SPYCICLE"; }
	case Spy_t_TheSharpDresser: { return "SHARP DRESSER"; }
	case Spy_t_TheWangaPrick: { return "WANGA PRICK"; }
	case Spy_t_TheBlackRose: { return "BLACK ROSE"; }

	case Heavy_m_Deflector_mvm: { return "DEFLECTOR"; }
	case Misc_t_FryingPan: { return "FRYING PAN"; }
	case Misc_t_GoldFryingPan: { return "GOLDEN FRYING PAN"; }
	case Misc_t_Saxxy: { return "SAXXY"; }

	default:
	{
		switch (this->m_hActiveWeapon().Get()->As<C_TFWeaponBase>()->GetWeaponID())
		{
			//scout
		case TF_WEAPON_SCATTERGUN: { return "SCATTERGUN"; }
		case TF_WEAPON_HANDGUN_SCOUT_PRIMARY: { return "SHORTSTOP"; }
		case TF_WEAPON_HANDGUN_SCOUT_SECONDARY: { return "PISTOL"; }
		case TF_WEAPON_SODA_POPPER: { return "SODA POPPER"; }
		case TF_WEAPON_PEP_BRAWLER_BLASTER: { return "BABY FACES BLASTER"; }
		case TF_WEAPON_PISTOL_SCOUT: { return "PISTOL"; }
		case TF_WEAPON_JAR_MILK: { return "MAD MILK"; }
		case TF_WEAPON_CLEAVER: { return "CLEAVER"; }
		case TF_WEAPON_BAT: { return "BAT"; }
		case TF_WEAPON_BAT_WOOD: { return "SANDMAN"; }
		case TF_WEAPON_BAT_FISH: { return "HOLY MACKEREL"; }
		case TF_WEAPON_BAT_GIFTWRAP: { return "WRAP ASSASSIN"; }

								   //soldier
		case TF_WEAPON_ROCKETLAUNCHER: { return "ROCKET LAUNCHER"; }
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT: { return "DIRECT HIT"; }
		case TF_WEAPON_PARTICLE_CANNON: { return "COW MANGLER 5000"; }
		case TF_WEAPON_SHOTGUN_SOLDIER: { return "SHOTGUN"; }
		case TF_WEAPON_BUFF_ITEM: { return "BUFF BANNER"; }
		case TF_WEAPON_RAYGUN: { return "RIGHTEOUS BISON"; }
		case TF_WEAPON_SHOVEL: { return "SHOVEL"; }

							 //pyro
		case TF_WEAPON_FLAMETHROWER: { return "FLAME THROWER"; }
		case TF_WEAPON_FLAME_BALL: { return "DRAGONS FURY"; }
		case TF_WEAPON_SHOTGUN_PYRO: { return "SHOTGUN"; }
		case TF_WEAPON_FLAREGUN: { return "FLAREGUN"; }
		case TF_WEAPON_FLAREGUN_REVENGE: { return "MANMELTER"; }
		case TF_WEAPON_JAR_GAS: { return "GAS PASSER"; }
		case TF_WEAPON_ROCKETPACK: { return "THERMAL THRUSTER"; }
		case TF_WEAPON_FIREAXE: { return "FIRE AXE"; }
		case TF_WEAPON_BREAKABLE_SIGN: { return "NEON ANNIHILATOR"; }
		case TF_WEAPON_SLAP: { return "HOT HAND"; }

						   //demoman
		case TF_WEAPON_GRENADELAUNCHER: { return "GRENADE LAUNCHER"; }
		case TF_WEAPON_PIPEBOMBLAUNCHER: { return "STICKYBOMB LAUNCHER"; }
		case TF_WEAPON_CANNON: { return "LOOSE CANNON"; }
		case TF_WEAPON_BOTTLE: { return "BOTTLE"; }
		case TF_WEAPON_SWORD: { return "SWORD"; }
		case TF_WEAPON_STICKBOMB: { return "ULLAPOOL CABER"; }

								//heavy
		case TF_WEAPON_MINIGUN: { return "MINIGUN"; }
		case TF_WEAPON_SHOTGUN_HWG: { return "SHOTGUN"; }
		case TF_WEAPON_LUNCHBOX: { return "LUNCHBOX"; }
		case TF_WEAPON_FISTS: { return "FISTS"; }

							//engineer
		case TF_WEAPON_SHOTGUN_PRIMARY: { return "SHOTGUN"; }
		case TF_WEAPON_SHOTGUN_BUILDING_RESCUE: { return "RESCUE RANGER"; }
		case TF_WEAPON_SENTRY_REVENGE: { return "FRONTIER JUSTICE"; }
		case TF_WEAPON_DRG_POMSON: { return "POMSON 6000"; }
		case TF_WEAPON_PISTOL: { return "PISTOL"; }
		case TF_WEAPON_LASER_POINTER: { return "WRANGLER"; }
		case TF_WEAPON_MECHANICAL_ARM: { return "MECHANICAL ARM"; }
		case TF_WEAPON_WRENCH: { return "WRENCH"; }
		case TF_WEAPON_PDA_ENGINEER_DESTROY: { return "DESTRUCTION PDA"; }
		case TF_WEAPON_PDA_ENGINEER_BUILD: { return "CONSTRUCTION PDA"; }
		case TF_WEAPON_BUILDER: { return "TOOLBOX"; }

							  //medic
		case TF_WEAPON_SYRINGEGUN_MEDIC: { return "SYRINGE GUN"; }
		case TF_WEAPON_CROSSBOW: { return "CROSSBOW"; }
		case TF_WEAPON_MEDIGUN: { return "MEDIGUN"; }
		case TF_WEAPON_BONESAW: { return "BONESAW"; }

							  //sniper
		case TF_WEAPON_SNIPERRIFLE: { return "SNIPER RIFLE"; }
		case TF_WEAPON_COMPOUND_BOW: { return "COMPOUND BOW"; }
		case TF_WEAPON_SNIPERRIFLE_DECAP: { return "BAZAAR BARGAIN"; }
		case TF_WEAPON_SNIPERRIFLE_CLASSIC: { return "CLASSIC"; }
		case TF_WEAPON_SMG: { return "SMG"; }
		case TF_WEAPON_CHARGED_SMG: { return "CLEANERS CARBINE"; }
		case TF_WEAPON_JAR: { return "JARATE"; }
		case TF_WEAPON_CLUB: { return "CLUB"; }

						   //spy
		case TF_WEAPON_REVOLVER: { return "REVOLVER"; }
		case TF_WEAPON_PDA_SPY_BUILD: { return "SAPPER"; }
		case TF_WEAPON_KNIFE: { return "KNIFE"; }
		case TF_WEAPON_PDA_SPY: { return "DISGUISE KIT"; }
		case TF_WEAPON_INVIS: { return "INVIS WATCH"; }

		case TF_WEAPON_GRAPPLINGHOOK: { return "GRAPPLING HOOK"; }

		default: break;
		}
	}
	}

	return "";
}