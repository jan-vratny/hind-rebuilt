#pragma once
#include <string>
#include <vector>

// The sounds of the original game: SOUND/MAXIMAL.BLK of the player's own installation (nothing
// of it is stored with the remake). A u32 header, then blocks of (u32 number, u32 length,
// samples): 8-bit unsigned mono at 11025 Hz. Block k is the k-th "sound\*.raw" name in HIND.EXE
// after the five music files; what each one is was found by listening to them.
namespace gamedata {

constexpr int kSoundRate = 11025;

class SoundBank {
public:
  bool load(const std::string& gameDir, std::string& error);
  bool loaded() const { return !blocks_.empty(); }
  // Samples -1..1 of block `id`, empty when it is not there.
  const std::vector<float>& get(int id) const;
  int count() const { return int(blocks_.size()); }

private:
  std::vector<std::vector<float>> blocks_;
};

// Block numbers.
namespace snd {
// Effects.
constexpr int Rotor = 0;          // HEL: the Hind's engines and blades, heard all the time in the cockpit (loop)
constexpr int Gear = 1;           // UNDRCR: undercarriage up or down
constexpr int CargoOpen = 2;      // CARGOP: the cargo doors
constexpr int CargoClose = 3;     // CARGCL
constexpr int Airframe = 4;       // AIRFM: creaking metal, the airframe under strain
constexpr int Touchdown = 5;      // LAND1..LAND5: touching down, from a light bump to a crash (5..9)
constexpr int Splash = 10;        // WATSPL: something falls into water
constexpr int Turret = 11;        // TURRET: the chin turret slewing
constexpr int Gun = 12;           // GAT: the turret machine gun firing (loop)
constexpr int Cannon = 13;        // 30MCN: cannon pods firing (loop)
constexpr int Rocket = 14;        // ROCKET: a rocket leaving its pod
constexpr int Rocket2 = 15;       // ROK2
constexpr int AirToSurface = 16;  // AIR2S: guided missile launch (9M114)
constexpr int AirToAir = 17;      // AIR2AR: R-60 launch, a sharp puff at the start
constexpr int AirToAir2 = 18;     // A2A2K
constexpr int BombRelease = 19;   // BOMRAC: a bomb off its rack
constexpr int FuelAir = 20;       // FUELAR: an explosion far off
constexpr int MineLay = 21;       // MNELY1, MNELY2: dull thuds of the mine dispenser
constexpr int MineLay2 = 22;
constexpr int SmallArms = 23;     // SMARMS: battle noise, bursts of machine guns
constexpr int Flak = 24;          // AA: one round of an anti-aircraft gun
constexpr int SamLaunch = 25;     // SRF2AR: a missile off the ground
constexpr int FeetOut = 26;       // FEET: troops getting out
constexpr int FeetIn = 27;        // FEETIN: troops boarding over the cabin floor
constexpr int HeliCrash = 28;     // HLCRSH: a helicopter crashing, explosion and falling wreckage
constexpr int MediumHit = 29;     // MEDHIT: as the crash, much shorter
constexpr int SmallHit = 30;      // SMLHIT
constexpr int ExplodeBig = 31;    // XPLBIG, XPLLRG, XPLMED, XPLSML: explosions, largest first
constexpr int ExplodeLarge = 32;
constexpr int ExplodeMedium = 33;
constexpr int ExplodeSmall = 34;
constexpr int ExplodeWater = 35;  // XPLSPL: an explosion in water
constexpr int Attention = 36;     // ATTEN: radio tones and crackle
constexpr int Warning = 37;       // WARNNG: warning tone
constexpr int Buttons = 38;       // BUTTNS: three presses of buttons
constexpr int Flare = 39;         // CHUFF: a flare fired, with an echo
// Voices. BANDIT: bandits at 1..12 o'clock high (40..51), low (52..63).
constexpr int BanditsHigh = 40, BanditsLow = 52;
constexpr int BanditOnSix = 64, WingmanHit = 65, WingmanGoingDown = 66;
// DRONE019..036: the wingman.
constexpr int DroneFirst = 67;
// GNDC: ground control.
constexpr int ClearedTakeOff = 85;   // "All right, Hind leader is cleared to take off"
constexpr int ClearedLand = 86;      // "All right, Hind leader is cleared to land"
constexpr int GoodRecon = 87, BadRecon = 88;
constexpr int StopFiringForces = 89;  // "Comrade, stop firing at our brave ground forces"
constexpr int HittingForces = 90;     // "Comrade, you are hitting our glorious forces"
constexpr int CeaseInstallations = 91;  // "... cease firing at allied ground installations"
constexpr int StopFriendlyAircraft = 92;  // "... stop your attack on friendly aircraft"
constexpr int Vector = 93;            // "Hind leader, vector" + three digits
constexpr int DigitFirst = 94;        // 0..3
constexpr int DigitMiddle = 98;       // 0..9, spoken slower
constexpr int DigitLast = 108;        // 0..9
constexpr int TargetAt = 118;         // "Target at" + three digits + 1..20 kilometers (119..138)
constexpr int Kilometers = 118;       // + n
// WSO: the weapons operator.
constexpr int WsoActive = 139, WsoOff = 140;
constexpr int ArmedAt6 = 141, ArmedR60 = 142, Armed57 = 143, Armed80 = 144, ArmedFab250 = 145, ArmedFab500 = 146,
              ArmedZab500 = 147, ArmedFuelAir = 148, ArmedCannon = 149, ArmedMines = 150, ArmedGun = 151;
constexpr int NavigationMode = 152, WeaponSafe = 153, Reloaded = 154;
constexpr int AutoChaffOn = 155, AutoChaffOff = 156, AutoFlareOn = 157, AutoFlareOff = 158;
constexpr int TargetInRange = 159, TargetOutOfRange = 160, TargetObscured = 161, TargetLost = 162;
constexpr int GotLock = 163, LockBroken = 164, FiringNow = 165, BombsAway = 166;
constexpr int GoodR60 = 167, GoodAt6 = 168, GoodRockets = 169, GoodBombing = 170, GoodMineLaying = 171,
              MinesOffTarget = 172;
constexpr int GoodShot = 173, GoodKill = 174, TargetHit = 175, TargetMissed = 176;
constexpr int WsoTypeFirst = 177;  // 177..294: what the target is (Mi-24 Hind .. blue flag)
// FENG: the flight engineer.
constexpr int CargoDoorsOpen = 295, CargoDoorsClosed = 296, TroopDeployed = 297, TroopsAboard = 298;
constexpr int CasualtiesRemoved = 299, CasualtiesAboard = 300, WrongDeploymentArea = 301, WrongBoardingArea = 302;
// NATA: "Natasha", the voice warning system.
constexpr int PullUp = 303, NavFailure = 304, LeftEngineFailure = 305, RightEngineFailure = 306,
              HydraulicFailure = 307, Overtorque = 308, LowRotorRpm = 309, CommsFailure = 310,
              JammerFailure = 311, RwrFailure = 312, HudFailure = 313, TvFailure = 314, FlareFailure = 315,
              GunFailure = 316, HelmetSightFailure = 317, LaserFailure = 318, FuelLow = 319,
              UndercarriageFailure = 320, EngineFire = 321, EnemyGroundFire = 322;
constexpr int RadarWarningFirst = 323;  // early warning, aircraft, SAM, AAA radar, RF missile, IR missile
constexpr int IncomingRf = 327, IncomingIr = 328;
// TPLR: orders to the other helicopter (329..358); CHAT: radio chatter (359..438).
constexpr int TplrFirst = 329, ChatFirst = 359;
// The battlefield.
constexpr int MachineGun = 439;  // M60: a heavier machine gun, low rate of fire
constexpr int Rifle = 440;       // AK47: one shot
constexpr int Mortar = 441, Artillery = 442, Jet = 443;
constexpr int TankEngine = 444;  // named TRAIN, sounds like an engine's even drone
constexpr int Train = 445;       // named TANK, the wheels of a train
constexpr int Truck = 446, JetIdle = 447;
}  // namespace snd

// The WSO's words for what an object of the original game (3DOBJS.BIN name) is, or -1; `enemy`
// tells a Kazakh rebel from a Russian soldier, who share their model.
int wsoTypeLine(const std::string& object, bool enemy);
// The words of such a line as a name ("Government Building"), or "".
std::string wsoTypeName(int line);

}  // namespace gamedata
