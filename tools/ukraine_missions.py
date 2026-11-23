#!/usr/bin/env python3
"""Write the Ukrainian campaign's missions (data/missions/UKRAIC*.DTA, .TXT, .MIS) and the drone
hunters' training flight among the original's Saratov training missions (SARATT21).

  tools/ukraine_missions.py

The remake's own content, in the original's mission format (see src/gamedata/mission.hpp) with
one addition, ":Drone Name" (a unit by name: Shahed_136, Gerbera, Geran_3, Mi_17_Hip_H,
Mi_24_Hind_Wingman). The
war is fought on the Saratov zone of the original: its steppe and river stand in for Ukraine's.
The missions are described here in metres east and north of the zone's south-west corner and in
seconds; the script lays out the routes and timings.

A night's raid: Shahed-136 drones launched far to the south-east fly at some 180 km/h, low, on
dog-legged courses to their targets and dive onto them at the end of their route; in the later
nights also the jet ones, the Geran-3 (Shahed-238), at some 330 km/h, faster than the Mi-24 can
follow: met head on, with the gun or the R-60. The flight is
three helicopters, as the crews hunting them fly: a Mi-17 that finds and calls out the drones
(the designator), and two Mi-24s that shoot them down, the player's and a wingman.
"""
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "data", "missions")
FT = 0.3048

# The zone (Saratov): our airbase and what the drones go for, m east and north.
BASE = (173461.0, 156512.0)
TARGETS = {
    "airbase": (173900.0, 158300.0),  # the hangars beside the runway
    "town": (168900.0, 157650.0),
    "bridge": (159063.0, 150599.0),   # the railway viaduct
    "plant": (185150.0, 171050.0),    # the works
    "depot": (186560.0, 176450.0),    # barracks, bunker and water tower
    "field": (163100.0, 166630.0),    # the second airfield
}
SHAHED_SPEED = 50.0  # m/s
JET_SPEED = 92.0     # m/s, the Geran-3 (some 330 km/h; it opens up to some 550 in its dive)
HELI_SPEED = 55.0    # m/s, the flight's patrol speed


def add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def polar(origin, bearing_deg, dist):
    """A point `dist` m from `origin` on a bearing (degrees clockwise from north)."""
    b = math.radians(bearing_deg)
    return (origin[0] + math.sin(b) * dist, origin[1] + math.cos(b) * dist)


def length(pts):
    return sum(math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def wp(kind, p, alt=0.0, t=None, speed=0.0, wait=0.0):
    """A waypoint line: type, x, y, alt in 1/256 ft, time in 1/100 s (-1: does not move),
    speed in 1/512 ft/s on the leg to it, ..., wait in 1/100 s."""
    f = [kind, round(p[0] / FT * 256), round(p[1] / FT * 256), round(alt / FT * 256),
         -1 if t is None else round(t * 100), round(speed / FT * 512)] + [0] * 11 + [round(wait * 100)]
    return ",".join(str(v) for v in f)


def route(points, alt, speed, start_wait=0.0, airborne=False):
    """Lines of a route flown at `alt`, from the ground (or already in the air)."""
    lines, t = [], 0.0
    for i, p in enumerate(points):
        if i:
            t += math.dist(points[i - 1], p) / speed
        lines.append(wp(1 if i == 0 else 2, p, alt if i or airborne else 0.0, t, 0.0 if i == 0 else speed,
                        start_wait if i == 0 else 0.0))
    return lines


def shahed(target, bearing, arrive, dist=70000.0, dogleg=(25.0, -20.0), alt=150.0, speed=SHAHED_SPEED):
    """A drone reaching `target` `arrive` s after take-off, coming in on `bearing` (from the
    target towards where it was launched), with two dog-legs on the way."""
    launch = polar(target, bearing, dist)
    p1 = polar(target, bearing + dogleg[0], dist * 0.6)
    p2 = polar(target, bearing + dogleg[1], dist * 0.25)
    pts = [launch, p1, p2, target]
    flight = length(pts) / speed
    wait = arrive - flight
    if wait < 0:
        # Launched earlier: already on its way, `arrive` s of flight from the target.
        left = arrive * speed
        back = [target]
        for a, b in zip(reversed(pts[:-1]), reversed(pts[1:])):
            leg = math.dist(a, b)
            if leg >= left:
                k = left / leg
                back.append((b[0] + (a[0] - b[0]) * k, b[1] + (a[1] - b[1]) * k))
                break
            back.append(a)
            left -= leg
        pts = list(reversed(back))
        wait = 0.0
    return pts, wait, alt


def patrol(target, bearing, out=18000.0, half=5000.0):
    """The flight's patrol line across the drones' way in, `out` m from the target."""
    centre = polar(target, bearing, out)
    return [polar(centre, bearing - 90, half), polar(centre, bearing + 90, half)]


MISSIONS = [
    dict(title="Night Watch", hour="20:40", weather=0, spotter=True,
         brief="Air defence reports three Shahed-136 drones crossing the line from the south-east, "
               "heading for our airbase. Take the flight up and hunt them down before they reach "
               "the hangars. The Hip will find them for you and call them out: listen for her, then "
               "close in, match their speed and let the gun do the work. The pylons carry racks of "
               "interceptor drones: the door gunners send them at the jet drones by themselves, and at "
               "the one you have when you tell them (G).",
         patrol=("airbase", 140.0),
         raids=[("airbase", 140.0, 600.0), ("airbase", 135.0, 660.0), ("airbase", 150.0, 760.0)],
         interceptors=True, success=2, fail=2),
    dict(title="The Railway Bridge", hour="22:10", weather=0, spotter=True,
         brief="The railway viaduct over the river carries every train to the front. Intelligence "
               "expects a raid of five drones in two waves. Patrol south of the bridge and stop them.",
         patrol=("bridge", 150.0),
         raids=[("bridge", 150.0, 620.0), ("bridge", 145.0, 650.0), ("bridge", 160.0, 690.0),
                ("bridge", 140.0, 1000.0), ("bridge", 155.0, 1050.0)],
         interceptors=True, success=3, fail=2),
    dict(title="Power for the City", hour="23:30", weather=1, spotter=True,
         brief="The enemy is after the power works north-east of the town. Six drones are coming in "
               "low under the cloud from the east. Every one that gets through leaves thousands in the "
               "dark and the cold.",
         patrol=("plant", 110.0),
         raids=[("plant", 110.0, 640.0, 80.0), ("plant", 100.0, 660.0, 80.0), ("plant", 120.0, 700.0, 80.0),
                ("plant", 105.0, 880.0, 80.0), ("plant", 115.0, 900.0, 80.0), ("plant", 95.0, 960.0, 80.0)],
         interceptors=True, success=4, fail=2),
    dict(title="Without the Hip", hour="21:15", weather=0, spotter=False,
         brief="The Hip is down for repairs tonight: there is no one to call the drones out for you. "
               "Four are heading for the town from the south. Watch the sky low on the horizon and "
               "let the WSO search ahead.",
         patrol=("town", 175.0, 14000.0),
         raids=[("town", 175.0, 600.0), ("town", 170.0, 640.0), ("town", 185.0, 720.0), ("town", 165.0, 780.0)],
         interceptors=True, success=2, fail=2),
    dict(title="Two Directions", hour="1:20", weather=0, spotter=True,
         brief="Two raids at once: four drones from the east on the depot, four from the south on "
               "the second airfield. The flight cannot be everywhere. Split your time between the two "
               "lines and take what you can. Two of them may be Gerbera decoys: no warhead, there only "
               "to draw your fire. You cannot tell them apart; the score counts only the Shaheds.",
         patrol=("depot", 120.0, 16000.0),
         decoys=[("depot", 98.0, 640.0), ("field", 168.0, 800.0)],
         raids=[("depot", 95.0, 620.0), ("depot", 100.0, 660.0), ("depot", 90.0, 700.0), ("depot", 105.0, 760.0),
                ("field", 170.0, 780.0), ("field", 165.0, 820.0), ("field", 175.0, 880.0), ("field", 160.0, 940.0)],
         interceptors=True, success=5, fail=3),
    dict(title="Fog on the River", hour="5:30", weather=2, spotter=True,
         brief="Morning fog over the river, and six drones following it at tree-top height towards the "
               "railway bridge. You will see them late: trust the Hip's calls. The interceptor drones "
               "find them in the fog when your eyes cannot. "
               "Tell the door gunners (G) to let one go at the Hip's contact.",
         patrol=("bridge", 160.0, 15000.0), interceptors=True,
         raids=[("bridge", 160.0, 620.0, 60.0), ("bridge", 155.0, 650.0, 60.0), ("bridge", 165.0, 700.0, 60.0),
                ("bridge", 150.0, 760.0, 60.0), ("bridge", 170.0, 800.0, 60.0), ("bridge", 158.0, 860.0, 60.0)],
         success=4, fail=2),
    dict(title="The Swarm", hour="2:00", weather=0, spotter=True,
         brief="A mass raid: a dozen drones on the town, the airbase and the power works, all at once "
               "from the south-east, and among them Gerbera decoys to soak up the rounds. Stay on the "
               "line where their courses cross, and keep the gun busy. Two of them are the jet ones, "
               "Geran-3s, at 330 km/h: you will not catch them from behind. Meet them head on, the Hip "
               "calls them 'JET SHAHED', and you carry R-60s for them on the outer pylons. Tonight the inner "
               "pylons carry UPK-23 gun pods instead of the interceptors: more gun for the swarm.",
         patrol=("town", 135.0, 20000.0, 7000.0),
         decoys=[("town", 138.0, 650.0), ("airbase", 128.0, 690.0), ("plant", 142.0, 720.0), ("town", 132.0, 760.0)],
         jets=[("airbase", 132.0, 720.0), ("town", 138.0, 800.0)], r60=True, upk=True,
         raids=[("town", 135.0, 640.0), ("town", 140.0, 660.0), ("town", 130.0, 700.0), ("town", 145.0, 740.0),
                ("airbase", 135.0, 660.0), ("airbase", 125.0, 700.0), ("airbase", 140.0, 760.0), ("airbase", 130.0, 820.0),
                ("plant", 140.0, 700.0), ("plant", 135.0, 760.0), ("plant", 150.0, 820.0), ("plant", 130.0, 880.0)],
         success=9, fail=4),
    dict(title="The Long Night", hour="22:30", weather=1, spotter=True,
         brief="The biggest raid yet: sixteen drones in four waves through the night, on everything we "
               "hold, decoys with every wave and a jet Geran-3 in three of them. The interceptor drones are "
               "back on the pylons: the door gunners let them go at the jets, and at "
               "anything else when you tell them (G). Land to rearm when you must, and get back up. The "
               "town is counting on you.",
         patrol=("town", 140.0, 18000.0, 8000.0),
         decoys=[("town", 138.0, 640.0), ("airbase", 133.0, 1120.0), ("town", 142.0, 1620.0), ("depot", 105.0, 2120.0)],
         jets=[("airbase", 138.0, 1130.0), ("town", 136.0, 1640.0), ("plant", 118.0, 2140.0)], interceptors=True,
         raids=[(t, b, a) for (t, b, a) in [
             ("airbase", 140.0, 620.0), ("town", 135.0, 650.0), ("bridge", 150.0, 700.0), ("plant", 120.0, 740.0),
             ("airbase", 130.0, 1100.0), ("town", 145.0, 1150.0), ("depot", 110.0, 1200.0), ("field", 165.0, 1260.0),
             ("town", 140.0, 1600.0), ("plant", 115.0, 1650.0), ("bridge", 155.0, 1700.0), ("airbase", 135.0, 1760.0),
             ("depot", 100.0, 2100.0), ("town", 130.0, 2150.0), ("field", 170.0, 2200.0), ("airbase", 145.0, 2260.0)]],
         success=11, fail=5),
]


# Training (in the menu's training list, after the original's twenty on the Saratov zone): the
# drone hunt by daylight, to learn it before the nights.
TRAINING = [
    dict(name="SARATT21", title="Operational Training - Drone Interception", hour="11:00", weather=0, spotter=True,
         brief="Before you fly the nights, learn the hunt by day. Three Shahed-136 drones will come in from "
               "the south-east towards our airbase, as a real raid would. The Hip finds them and calls them "
               "out ('SHAHED 2 O'CLOCK 4.3 KM LOW'), a diamond on the HUD marks the one called. Turn towards it, "
               "close in behind or alongside at its speed (about 180 km/h) and let the gun do the work: the WSO "
               "lays it on the drone when it is in reach. The inner pylons carry UPK-23 gun pods for a heavier "
               "burst head on. Door gunners down them alongside by themselves; D in "
               "the air tells them to hold fire, F4 takes a door gun yourself.",
         patrol=("airbase", 140.0, 15000.0), upk=True,
         raids=[("airbase", 140.0, 560.0), ("airbase", 130.0, 680.0), ("airbase", 150.0, 800.0)],
         success=2, fail=2,
         outstanding="All three down. You are ready for the nights.",
         succeeded="Most of them down. Fly it again until none gets through.",
         failed="Too many got through. Fly it again: listen to the Hip, and get alongside them."),
    # The busy one, for flying together (the network game): four waves of six close to home by
    # day, the first at the patrol line some two and a half minutes after take-off.
    dict(name="SARATT22", title="Operational Training - Drone Swarm", hour="12:00", weather=0, spotter=True,
         brief="A daylight swarm close to home, to fly together: twenty-four Shahed-136 drones in four "
               "waves of six, with a few Gerbera decoys, all coming in from the south-east at our airbase "
               "and the town. The patrol line is only 8 km out, the first wave reaches it some two and a "
               "half minutes after take-off and the next follows every two and a half minutes. Split the "
               "waves between you, land at the base to rearm when the guns run dry, and get back up.",
         patrol=("airbase", 140.0, 8000.0, -4000.0), passes=8, upk=True,  # out to the southern end first (not over the pads)
         decoys=[("town", 138.0, 330.0), ("airbase", 132.0, 480.0), ("town", 146.0, 630.0), ("airbase", 128.0, 780.0)],
         raids=[(("airbase", "town")[k % 2], 122.0 + 7.0 * k, 300.0 + 150.0 * w + 12.0 * k)
                for w in range(4) for k in range(6)],
         success=16, fail=6,
         outstanding="Not one of the twenty-four got through. Well flown, all of you.",
         succeeded="Most of the swarm went down. Fly it again and get them all.",
         failed="Too many got through. Fly it again: share the waves, and keep the guns fed."),
    # The jet ones, by day.
    dict(name="SARATT23", title="Operational Training - Jet Drones", hour="11:30", weather=0, spotter=True,
         brief="The enemy has started sending the jet Shahed, the Geran-3: the same black delta with a "
               "turbojet in the tail, some 330 km/h, and in its dive on the target some 550. The Mi-24 "
               "cannot catch it from behind, and the door gunners will not get a shot. Four will come in "
               "from the south-east at our airbase. The Hip calls them 'JET SHAHED': get in front of one, "
               "turn to meet it head on, and either let the gun meet it (the WSO lays it on, a burst as "
               "it closes) or fire an R-60 from the outer pylons once the HUD shows it in reach. They are "
               "on the line within a minute of one another: be quick.",
         patrol=("airbase", 140.0, 15000.0),
         jets=[("airbase", 140.0, 420.0), ("airbase", 132.0, 480.0), ("airbase", 148.0, 560.0), ("airbase", 136.0, 640.0)],
         raids=[], r60=True, upk=True,
         success=2, fail=3,
         outstanding="All four down. Head on and quick: that is how it is done.",
         succeeded="Some of them down. Fly it again: be in front of them, not behind.",
         failed="Too many got through. Fly it again: meet them head on, and use the R-60s."),
    # The interceptor drones, by day.
    dict(name="SARATT24", title="Operational Training - Interceptor Drones", hour="10:30", weather=0, spotter=True,
         brief="The pylons carry two racks of interceptor drones, six on each, as the Mi-8 crews "
               "have them. They are the door gunners' to fly: when a jet Geran-3 comes within 5 km they let "
               "an interceptor go at it, and it steers itself onto it at some 400 km/h. The slower Shaheds "
               "they leave to their guns, unless you tell them (G) to send one at your target. A gunner "
               "guiding an interceptor has his hands full: his gun is silent till it is close to its "
               "drone. D in the air holds them, as it holds the door guns. Six Shaheds and two Geran-3s will come "
               "in at our airbase: take the flight to the patrol line and watch the interceptors work, "
               "the gun is yours for what they miss.",
         patrol=("airbase", 140.0, 15000.0),
         jets=[("airbase", 136.0, 600.0), ("airbase", 146.0, 700.0)],
         raids=[("airbase", 140.0, 560.0), ("airbase", 132.0, 600.0), ("airbase", 148.0, 640.0),
                ("airbase", 128.0, 720.0), ("airbase", 152.0, 760.0), ("airbase", 138.0, 800.0)],
         interceptors=True,
         success=5, fail=2,
         outstanding="Not one got through. The interceptors and the gun together: well done.",
         succeeded="Most of them down. Fly it again and let none through.",
         failed="Too many got through. Fly it again: be on the line early, in reach of them."),
    # Not about drones: the engines off and down, from high over the base.
    dict(name="SARATT25", title="Autorotation Training", hour="10:00", weather=0, height=900,
         brief=["You start at 900 m over the base at 200 km/h, the engines running. Press O: the engines drop to "
                "idle and nothing drives the rotor. At once lower the collective to 35 %, or the revolutions fall "
                "away in seconds. The HUD now shows the rotor's RPM in place of the torque: keep it at 90-110 %. "
                "More collective spends it, less lets it climb; 35-45 % holds it.",
                "Glide at about 150 km/h, sinking 15-20 m/s: 900 m leaves about a minute and 3 km of ground. "
                "Turn back towards the base early and lower the gear (U).",
                "At 50 m raise the nose to about +20 degrees: the speed and the sink fall and the RPM climbs. At 12 m "
                "bring the nose back level and raise the collective as much as it takes to touch down below 4 m/s. "
                "You get this one chance: there is no going round. On the wheels lower the collective and brake (B).",
                "Land anywhere in one piece with the engines disengaged since at least 100 m up. Land within 500 m of "
                "the start for an outstanding. O engages the engines again, some seconds to full power, for "
                "a try that has gone wrong, but a landing under power fails the drill. Stable or Realistic only: "
                "a Novice pilot flies this one in Stable."],
         outstanding="Down on the base without power. That one will save your life.",
         succeeded="Down in one piece without power, though not on the base. Next time turn back earlier.",
         failed="That was no autorotation: the engines have to stay off down to the ground. Fly it again."),
]


def listed(items):
    """"a", "a and b", "a, b and c"."""
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " and " + items[-1]


def drill_files(m):
    """The autorotation drill: nothing to fight, the flight starts high over the base."""
    dta = [":Task", ":User Task", ":Side", "1", ":Task Type", "5", ":Drone", ":Drone Type", "607", ":Waypoint"]
    dta += route([BASE, polar(BASE, 90.0, 3000.0), BASE], m["height"], HELI_SPEED)
    dta += [":Gun", "1470", ":Laser Timeout", "0", ":Wind Speed", "0", ":Wind Direction", "0",
            ":Weather Condition", str(m["weather"]), ":Time", m["hour"],
            ":Failure Count", "0", ":Success Count", "0", ":Outstanding Count", "0",
            ":Humans loaded count", "0", ":Humans deployed count", "0", ":Autorotation", str(m["height"]), ":Destroy", ""]
    txt = [":Title", m["title"], ":Description", "Helicopters: 1", ":Para", "Targets: None",
           ":Para", f"Take-off time: {m['hour']}", ":Para", "Flight time: 2 mins", ":Para", "Weather: Clear"]
    for para in m["brief"]:
        txt += [":Para", para]
    txt += [":Outstanding", m["outstanding"], ":Success", m["succeeded"], ":Failure", m["failed"], ":EOF"]
    return m["name"], dta, txt, [":Game Flow", "2", ":War Zone", "4", ":Campaign Level", "0", ":EOF"]


def mission_files(n, m):
    if "height" in m:
        return drill_files(m)
    name = m.get("name", f"UKRAIC{n:02d}")
    tgt, bearing, *rest = m["patrol"]
    line = patrol(TARGETS[tgt], bearing, *rest)
    # The player: out to the patrol line, along it twice, home.
    passes = m.get("passes", 2)
    player = [BASE] + [line[i % 2] for i in range(2 * passes)] + [BASE]
    dta = [":Task", ":User Task", ":Side", "1", ":Task Type", "5", ":Drone", ":Drone Type", "607", ":Waypoint"]
    dta += route(player, 150.0, HELI_SPEED)
    # The wingman a little behind, and the Hip ahead of them on the line, to and fro.
    # Both fly the player's route as he plans it (":Follow Player").
    dta += [":Drone", ":Drone Name", "Mi_24_Hind_Wingman", ":Follow Player", ":Waypoint"]
    dta += route([add(BASE, (40.0, -30.0))] + player[1:-1] + [add(BASE, (40.0, -30.0))], 170.0, HELI_SPEED, 20.0)
    if m["spotter"]:
        hip = [add(BASE, (-40.0, -30.0))] + [line[i % 2] for i in range(2 * passes + 2)] + [add(BASE, (-40.0, -30.0))]
        dta += [":Drone", ":Drone Name", "Mi_17_Hip_H", ":Follow Player", ":Waypoint"]
        dta += route(hip, 300.0, 45.0)
    # The drones.
    dta += [":Task", ":Side", "2", ":Task Type", "13"]
    for raid in m["raids"]:
        t, b, arrive = raid[:3]
        alt = raid[3] if len(raid) > 3 else 150.0
        pts, wait, alt = shahed(TARGETS[t], b, arrive, alt=alt)
        dta += [":Drone", ":Drone Name", "Shahed_136", ":Kill Success", ":Waypoint"]
        dta += route(pts, alt, SHAHED_SPEED, wait, airborne=True)
    # The jet ones, the Geran-3: faster, higher, from further out.
    for t, b, arrive in m.get("jets", []):
        pts, wait, alt = shahed(TARGETS[t], b, arrive, dist=110000.0, alt=200.0, speed=JET_SPEED)
        dta += [":Drone", ":Drone Name", "Geran_3", ":Kill Success", ":Waypoint"]
        dta += route(pts, alt, JET_SPEED, wait, airborne=True)
    # Gerbera decoys: flying as the Shaheds do, not to be counted (no ":Kill Success").
    for t, b, arrive in m.get("decoys", []):
        pts, wait, alt = shahed(TARGETS[t], b, arrive)
        dta += [":Drone", ":Drone Name", "Gerbera", ":Waypoint"]
        dta += route(pts, alt, SHAHED_SPEED, wait, airborne=True)
    jets = len(m.get("jets", []))
    total = len(m["raids"]) + jets
    decoys = len(m.get("decoys", []))
    # S-8 pods inside; outside R-60s or the interceptor drones (our code 30) for the jet ones.
    # The pylons ready for drones: the interceptors' rack (our code 30, on the inner pylons and
    # spanning the outer ones), or UPK-23 gun pods (10) inside with R-60s (9) outside for the jet
    # ones; S-8 pods (16) otherwise.
    inner = ["0,30", "1,30"] if m.get("interceptors") else ["0,10", "1,10"] if m.get("upk") else ["0,16", "1,16"]
    outer = ["2,9", "3,9"] if m.get("r60") and not m.get("interceptors") else []
    dta += [":Weapon"] + inner + outer + ["8,19", "9,7", "10,8", ":Gun", "1470",
            ":Laser Timeout", "0", ":Wind Speed", "3", ":Wind Direction", "120",
            ":Weather Condition", str(m["weather"]), ":Time", m["hour"],
            ":Failure Count", str(m["fail"]), ":Success Count", str(m["success"]),
            ":Outstanding Count", str(total), ":Humans loaded count", "0", ":Humans deployed count", "0", ":Destroy", ""]
    weather = ["Clear", "Overcast", "Light fog", "Thick fog", "Rain"][m["weather"]]
    minutes = round(length(player) / HELI_SPEED / 60)
    txt = [":Title", m["title"], ":Description", "Helicopters: 3" if m["spotter"] else "Helicopters: 2",
           ":Para", "Targets: " + listed(
               ([f"{len(m['raids'])} Shahed-136 drones"] if m["raids"] else []) + ([f"{jets} Geran-3 jet drones"] if jets else [])
               + ([f"{decoys} Gerbera decoys"] if decoys else [])),
           ":Para", "Weapons: Gun, door guns, " + listed(
               (["interceptor drones"] if m.get("interceptors") else ["UPK-23 gun pods"] if m.get("upk") else ["S8 rockets"])
               + (["R-60 missiles"] if m.get("r60") and not m.get("interceptors") else [])),
           ":Para", f"Take-off time: {m['hour']}", ":Para", f"Flight time: {minutes} mins",
           ":Para", f"Weather: {weather}", ":Para", m["brief"],
           ":Para", f"Shoot down at least {m['success']}. If {m['fail']} get through, the mission has failed.",
           ":Outstanding", m.get("outstanding", "Not one of them got through. The town slept tonight."),
           ":Success", m.get("succeeded", "Most of them went down before they could do harm. Well done."),
           ":Failure", m.get("failed", "Too many got through. There will be funerals in the morning."), ":EOF"]
    # The campaign's missions by their place in it, training as the original's: level 0.
    mis = [":Game Flow", "2", ":War Zone", "4", ":Campaign Level", str(n), ":EOF"]
    return name, dta, txt, mis


# The campaign's texts, as the original's AFGHNL00.TXT ... (gamedata/campaign.hpp): the
# campaign's description, its end won and lost; each level's situation, a stalemate after a
# failed mission, the failure that ends the campaign.
CAMPAIGN = {
    "Description": (
        "Autumn. The war is in its fourth year, and every night the sky over the towns behind the front fills with "
        "the drone of Shahed engines. They are cheap, slow and many: a lawnmower's buzz, fifty kilograms of explosive, "
        "a course set for a power station, a bridge, a block of flats. The missiles and the guns of the air defence "
        "cannot be everywhere, and every one spent on a drone is one less for a missile.\n"
        "Your squadron flies old Mi-24s, once Czech, now ours, with a Mi-17 to find the drones in the dark. Out here "
        "on the steppe you are the last line between the raids and the town. Fly low, listen to the Hip, match their "
        "speed and let the gun do the work."),
    "Successful": (
        "The raids have thinned out. The enemy has learnt that the steppe is a bad way in, and the town has had its "
        "lights on all week. The squadron has earned its rest, and you, your decorations."),
    "Failure": (
        "Too many drones got through. The squadron has been pulled back for new crews and new tactics, and the town "
        "will spend the winter in the dark."),
}
LEVELS = [
    ("The first raids are probing the line south-east of the airbase. Learn how they fly and how the Hip calls them out.",
     "A few got through, but nobody expected the first nights to be easy. Try again.",
     "The airbase has been hit hard. The squadron cannot fly from it any more."),
    ("The enemy has found the railway viaduct: every train to the front crosses it. It must stand.",
     "The bridge is damaged but still carries trains. It will not survive another raid like that.",
     "The viaduct is down. The front will go short of everything for weeks."),
    ("Now it is the power works north-east of the town. Winter is coming, and the enemy knows it.",
     "Part of the town is without power tonight. The engineers are working, but they need you to keep the sky clear.",
     "The power works are burning. The town faces the winter in the dark and the cold."),
    ("The Hip is down for repairs. You will have to find the drones yourselves, with the WSO's search and your own eyes.",
     "Without the Hip it is hard. Use the WSO, keep low and watch the horizon.",
     "Blind without the Hip, the flight missed too many. The command grounds the squadron until it is whole again."),
    ("The raids come from two directions at once, and the enemy mixes in Gerbera decoys to waste our rounds.",
     "The decoys did their job: too many rounds spent on empty drones. Choose your targets.",
     "Stretched between two lines, the flight could not hold either. The depot and the airfield are gone."),
    ("Fog on the river. The drones follow it at tree-top height towards the railway bridge.",
     "In the fog you saw them too late. Trust the Hip's calls and be where they will be.",
     "The fog covered the raid all the way to the bridge. It is down again, and this time for good."),
    ("A swarm: a dozen drones and decoys at once on the town, the airbase and the power works, and the first jet Geran-3s.",
     "The swarm was too big for one flight. Stay where their courses cross and keep the gun busy.",
     "The swarm got through. The town counts its dead, and the command has lost faith in the helicopters."),
    ("The biggest raid yet: four waves through the whole night, on everything we hold. Rearm when you must and get back up.",
     "The night was too long. Some waves got through, and the enemy will come again with more.",
     "Wave after wave got through. By the morning there was little left to defend."),
]


def campaign_texts():
    """UKRAIL00.TXT: the campaign; UKRAIL01..: its levels."""
    files = {"UKRAIL00": [":Description", CAMPAIGN["Description"], ":Successful", CAMPAIGN["Successful"],
                          ":Failure", CAMPAIGN["Failure"], ":EOF"]}
    for n, (situation, stalemate, failure) in enumerate(LEVELS, 1):
        files["UKRAIL%02d" % n] = [":Situation", situation, ":Stalemate", stalemate, ":Failure", failure, ":EOF"]
    return files


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, lines in campaign_texts().items():
        with open(os.path.join(OUT, name + ".TXT"), "w") as f:
            # Paragraphs as the original's: an empty line between them.
            f.write("\r\n".join(l.replace("\n", "\r\n\r\n") for l in lines) + "\r\n")
    for n, m in [(i, m) for i, m in enumerate(MISSIONS, 1)] + [(0, m) for m in TRAINING]:
        name, dta, txt, mis = mission_files(n, m)
        for ext, lines in ((".DTA", dta), (".TXT", txt), (".MIS", mis)):
            with open(os.path.join(OUT, name + ext), "w") as f:
                f.write("\r\n".join(lines) + "\r\n")
        print(name, m["title"], len(m.get("raids", [])) + len(m.get("jets", [])), "drones")


if __name__ == "__main__":
    main()
