// MP7Mag40Dev - in-game test harness for the MP7 40-Round Magazine plugin. Not shipped.
// Loads TrainingGrounds, spawns an MP7 in front of the camera (right side towards it), gives it a
// magazine the way the game does, and checks the magazine: capacity and rounds, the components it
// carries (which mesh is shown), firing from it, and the magazine on its own once ejected.
// "SNAP <name>" = a game-rendered screenshot (the test runner turns them into files).
#include <GML/GML.hpp>
#include <windows.h>
#include <cmath>

using namespace gml;

GML_PLUGIN("com.remarrow.mp7mag40dev", "MP7Mag40Dev", "0.1.0", {{"com.remarrow.mp7mag40", "0.1.0", GML_DEPENDENCY_SOFT}});

struct Vec { double x, y, z; };
struct Rot { double pitch, yaw, roll; };

static const char* kMap = "/Game/Maps/Training/TrainingGrounds";
static const char* kMp7 = "/Game/Weapons/MP7/Blueprints/Firearm_SMG_MP7A2.Firearm_SMG_MP7A2_C";
static const char* kMag = "/Game/Weapons/MP7/Blueprints/Magazine_MP7A2_20rnd.Magazine_MP7A2_20rnd_C";
static Object s_gun, s_loose;
static int s_step = -1;
static double s_wait = 0;

static bool CallBool(Object o, const char* fn) { Params p(o, fn); p.Call(); return p.Return<bool>(); }
static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}
static void Snap(const char* n) { Log("SNAP {}", n); API->ExecConsoleCommand("shot"); }

// `actor` `dist` cm in front of the camera and `up` cm above it, its forward along the camera's right
// so its right side faces the camera.
static void Place(Object actor, double dist, double up, double along = 0) {
    if (!actor || !API->IsValid(actor.ptr)) return;
    Params p(Lib("GameplayStatics"), "GetPlayerCameraManager");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    Object pcm = p.Return<GUObject*>();
    if (!pcm) return;
    Params lp(pcm, "GetCameraLocation"); lp.Call();
    Params rp(pcm, "GetCameraRotation"); rp.Call();
    Vec cam = lp.Return<Vec>();
    double t = rp.Return<Rot>().yaw * 3.14159265358979 / 180;
    Vec at{cam.x + std::cos(t) * dist - std::sin(t) * along, cam.y + std::sin(t) * dist + std::cos(t) * along, cam.z + up};
    Params mv(actor, "K2_SetActorLocationAndRotation");
    mv.Set("NewLocation", at).Set("NewRotation", Rot{0, rp.Return<Rot>().yaw + 90, 0}).Set("bSweep", false).Set("bTeleport", true).Call();
}
static void NoPhysics(Object a) {
    if (Object root = a.GetObj("RootComponent")) { Params sp(root, "SetSimulatePhysics"); sp.Set("bSimulate", false).Call(); }
}

static Object Spawn(const char* path) {
    GUClass* c = API->LoadClass(path);
    Object pawn = Pawn();
    if (!c || !pawn) { Error("no class {} or no pawn", path); return {}; }
    Params gt(pawn, "GetTransform");
    gt.Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(pawn.Struct(), "GetTransform"), "ReturnValue"), &xi);
    Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", c);
    std::memcpy(b.Ptr("SpawnTransform"), gt.Ptr("ReturnValue"), xi.size);
    b.Set("CollisionHandlingOverride", (uint8_t)1).Call();
    Object a = b.Return<GUObject*>();
    if (!a) return {};
    Params f(Lib("GameplayStatics"), "FinishSpawningActor");
    f.SetObj("Actor", a);
    std::memcpy(f.Ptr("SpawnTransform"), gt.Ptr("ReturnValue"), xi.size);
    f.Call();
    return a;
}

static void MagState(const char* when, Object mag) {
    if (!mag) { Log("MAG [{}] none", when); return; }
    Log("MAG [{}] {} capacity {} rounds {} | gun ammo {}", when, mag.Name(), mag.Get<int32_t>("AmmoCapacity"), mag.Get<int32_t>("CurrentAmmo"),
        [] { Object w = s_gun.GetObj("MagWellComponent"); if (!w) return -1; Params a(w, "GetCurrentAmmo"); a.Call(); return (int)a.Return<int32_t>(); }());
    Params gc(mag, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("PrimitiveComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        Object mesh = c.IsA("StaticMeshComponent") ? c.GetObj("StaticMesh") : Object();
        Params par(c, "GetAttachParent"); par.Call();
        Object parent = par.Return<GUObject*>();
        Log("  {} [{}] mesh={} visible={} parent={}", c.Name(), Object((GUObject*)c.Class()).Name(), mesh ? mesh.Name() : "-",
            c.GetBool("bVisible"), parent ? parent.Name() : "-");
    }
}

static Object Handle() { return s_gun.GetObj("ChargingHandle"); }
static void Rack() {
    Object h = Handle();
    if (!h) { Error("no ChargingHandle"); return; }
    { Params sp(h, "SetProgress"); sp.Set("Progress", 1.0f).Call(); Params b(h, "HandleBackServer"); b.Call(); }
    { Params sp(h, "SetProgress"); sp.Set("Progress", 0.0f).Call(); Params f(h, "HandleForwardServer"); f.Call(); }
}

static Vec Loc(Object a) { Params p(a, "K2_GetActorLocation"); p.Call(); return p.Return<Vec>(); }
static Vec CompLoc(Object c) { Params p(c, "K2_GetComponentLocation"); p.Call(); return p.Return<Vec>(); }

// A transform's translation, from its raw bytes (FQuat4d rotation first, then FVector3d translation).
static Vec XfTranslation(const uint8_t* xf) { Vec v; std::memcpy(&v, xf + 32, sizeof v); return v; }

// The hand socket: where it sits on the magazine and how it snaps a hand.
static void SocketState(const char* when, Object mag) {
    Object s = mag ? mag.GetObj("HandSocket") : Object();
    if (!s) { Log("SOCKET [{}] none", when); return; }
    Params par(s, "GetAttachParent"); par.Call();
    Object parent = par.Return<GUObject*>();
    Vec rel = s.Get<Vec>("RelativeLocation");
    Rot rr = s.Get<Rot>("RelativeRotation");
    Vec hand = XfTranslation((const uint8_t*)s.PropPtr("HandRelativePlacement"));
    Log("SOCKET [{}] parent {} rel ({:.2f}, {:.2f}, {:.2f}) rot ({:.1f}, {:.1f}, {:.1f}) | hand at ({:.2f}, {:.2f}, {:.2f}) from it | "
        "alwaysInRange {} overrideDistance {:.1f} onlySnapMesh {} matchRotation {} disabled {}",
        when, parent ? parent.Name() : "-", rel.x, rel.y, rel.z, rr.pitch, rr.yaw, rr.roll, hand.x, hand.y, hand.z,
        s.GetBool("bAlwaysInRange"), s.Get<float>("OverrideDistance"), s.GetBool("bOnlySnapMesh"), s.GetBool("bMatchRotation"), s.GetBool("bDisabled"));
    Object root = mag.GetObj("RootComponent");
    Params cp(root, "GetCollisionProfileName"); cp.Call();
    Params ce(root, "GetCollisionEnabled"); ce.Call();
    char prof[64] = "";
    API->NameToString(cp.Return<uint64_t>(), prof, sizeof prof);
    Log("  root collision profile {} enabled {}", prof, (int)ce.Return<uint8_t>());
    Params gc(mag, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("BoxComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object b(((GUObject**)arr->Data)[i]);
        Params be(b, "GetCollisionEnabled"); be.Call();
        Vec at = b.Get<Vec>("RelativeLocation"), ext = b.Get<Vec>("BoxExtent");
        Log("  box {} at ({:.2f}, {:.2f}, {:.2f}) extent ({:.2f}, {:.2f}, {:.2f}) collision {}", b.Name(), at.x, at.y, at.z, ext.x, ext.y, ext.z,
            (int)be.Return<uint8_t>());
    }
}

// A 1.5 cm ball on the hand socket, for the screenshots.
static void MarkSocket(Object mag) {
    Object s = mag.GetObj("HandSocket");
    Object sphere = LoadObject("/Engine/BasicShapes/Sphere.Sphere");
    if (!s || !sphere) return;
    Params mk(Lib("KismetMathLibrary"), "MakeTransform");
    mk.Set("Location", Vec{0, 0, 0}).Set("Rotation", Rot{0, 0, 0}).Set("Scale", Vec{0.015, 0.015, 0.015}).Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &xi);
    Params add(mag, "AddComponentByClass");
    add.Set("Class", FindClass("StaticMeshComponent")).Set("bManualAttachment", true).Set("bDeferredFinish", false);
    std::memcpy(add.Ptr("RelativeTransform"), mk.Ptr("ReturnValue"), xi.size);
    add.Call();
    Object ball = add.Return<GUObject*>();
    if (!ball) return;
    Params sm(ball, "SetStaticMesh"); sm.SetObj("NewMesh", sphere).Call();
    Params cc(ball, "SetCollisionEnabled"); cc.Set("NewType", (uint8_t)0).Call();
    Params at(ball, "K2_AttachToComponent");
    at.SetObj("Parent", s).Set("SocketName", uint64_t(0)).Set("LocationRule", (uint8_t)2).Set("RotationRule", (uint8_t)2)
        .Set("ScaleRule", (uint8_t)1).Set("bWeldSimulatedBodies", false).Call();
}

// The player's view from a camera of our own: `from`, looking at `at`.
static Object s_cam;
static void ViewFrom(Vec from, Vec at) {
    if (!s_cam) {
        GUClass* c = (GUClass*)FindClass("CameraActor");
        Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
        Params mk(Lib("KismetMathLibrary"), "MakeTransform");
        mk.Set("Location", from).Set("Rotation", Rot{0, 0, 0}).Set("Scale", Vec{1, 1, 1}).Call();
        GML_PropInfo xi;
        API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &xi);
        b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", c);
        std::memcpy(b.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
        b.Set("CollisionHandlingOverride", (uint8_t)1).Call();
        s_cam = b.Return<GUObject*>();
        if (!s_cam) { Error("no camera"); return; }
        Params f(Lib("GameplayStatics"), "FinishSpawningActor");
        f.SetObj("Actor", s_cam);
        std::memcpy(f.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), xi.size);
        f.Call();
        if (Object cc = s_cam.GetObj("CameraComponent")) cc.Set("FieldOfView", 40.0f);
        Params pc(Lib("GameplayStatics"), "GetPlayerController");
        pc.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
        Params vt(Object(pc.Return<GUObject*>()), "SetViewTargetWithBlend");
        vt.SetObj("NewViewTarget", s_cam).Set("BlendTime", 0.0f).Call();
    }
    double dx = at.x - from.x, dy = at.y - from.y, dz = at.z - from.z;
    Rot look{std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180 / 3.14159265358979, std::atan2(dy, dx) * 180 / 3.14159265358979, 0};
    Params mv(s_cam, "K2_SetActorLocationAndRotation");
    mv.Set("NewLocation", from).Set("NewRotation", look).Set("bSweep", false).Set("bTeleport", true).Call();
}

static Vec s_magAt;  // where the magazine sat in the gun
static double s_yaw = 0;
static int s_shots = 0, s_dry = 0;  // rounds the gun fired (GunChamberComponent:OnFired), empty checks in a row

static void Step(double dt) {
    if ((s_wait -= dt) > 0) return;
    Object mag = s_gun ? s_gun.GetObj("CurrentMagazine") : Object();
    switch (s_step) {
    case 0: {
        s_gun = Spawn(kMp7);
        if (!s_gun) { Error("no MP7"); Log("DEV TEST COMPLETE"); s_step = 99; return; }
        NoPhysics(s_gun);
        Place(s_gun, 100, 100);  // in mid-air, clear of the floor (the test camera sits at floor level)
        Params r(s_gun, "K2_GetActorRotation"); r.Call();
        s_yaw = r.Return<Rot>().yaw;
        Params hb(s_gun, "SetHoldingCharacter"); hb.SetObj("_HoldingCharacter", Pawn()).Call();
        Params fm(s_gun, "UpdateFireModeReplicated"); fm.Set("NewFireMode", (uint8_t)1).Call();  // semi, off safe
        Log("gun {} [{}]", s_gun.Name(), Object((GUObject*)s_gun.Class()).Name());
        s_step = 1; s_wait = 1.0;
        return;
    }
    case 1: {
        Params p(s_gun.GetObj("MagWellComponent"), "SpawnAndInsertMagazine");
        p.Set("AcceptedMagazineIndex", (int32_t)0).Call();
        s_step = 2; s_wait = 1.5;
        return;
    }
    case 2:
        MagState("inserted, 1.5 s", mag);
        SocketState("inserted", mag);
        MarkSocket(mag);
        if (!CallBool(s_gun, "CanFireRound")) Rack();  // chamber the first round only if the insert didn't
        s_magAt = Loc(mag);
        s_step = 3; s_wait = 0.8;
        return;
    case 3: {
        // From the gun's right side, 55 cm out, level with the lower half of the 40.
        double t = (s_yaw + 90) * 3.14159265358979 / 180;  // the gun's right
        Vec mid{s_magAt.x, s_magAt.y, s_magAt.z - 10};
        ViewFrom({mid.x + std::cos(t) * 55, mid.y + std::sin(t) * 55, mid.z}, mid);
        MagState("ready to fire", mag);
        s_step = 4; s_wait = 0.4;
        return;
    }
    case 4:
        Snap("mag_in");
        s_shots = 0;
        s_dry = 0;
        s_step = 5; s_wait = 0.4;
        return;
    case 5:
        // Empty it: one trigger pull per cycle, semi-auto, until the gun has nothing left to fire.
        if (CallBool(s_gun, "CanFireRound")) {
            s_dry = 0;
            Params f(s_gun, "FireRoundLocal");
            f.Call();
            s_wait = 0.15;
            return;
        }
        if (++s_dry < 8) { s_wait = 0.15; return; }  // give the action time to cycle before calling it empty
        Log("FIRED {} rounds (the game's own OnFired count) - magazine {} of {}, round in the chamber: {}", s_shots,
            mag ? mag.Get<int32_t>("CurrentAmmo") : -1, mag ? mag.Get<int32_t>("AmmoCapacity") : -1,
            CallBool(s_gun.GetObj("ChamberComponent"), "HasRoundLoadedInChamber"));
        s_step = 6; s_wait = 0.3;
        return;
    case 6:
        MagState("emptied", mag);
        { Params r(s_gun, "MagReleasePressed"); r.Call(); Params rr(s_gun, "MagReleaseReleased"); rr.Call(); }
        s_loose = mag;
        s_step = 7; s_wait = 1.0;
        return;
    case 7: {
        NoPhysics(s_loose);
        // The ejected magazine where it sat in the gun, upright; the gun out of the picture.
        Params mv(s_loose, "K2_SetActorLocationAndRotation");
        mv.Set("NewLocation", s_magAt).Set("NewRotation", Rot{0, s_yaw, 0}).Set("bSweep", false).Set("bTeleport", true).Call();
        Vec up = Loc(s_gun); up.z += 80;
        Params g(s_gun, "K2_SetActorLocation"); g.Set("NewLocation", up).Set("bSweep", false).Set("bTeleport", true).Call();
        MagState("ejected", s_loose);
        SocketState("ejected", s_loose);
        Vec s = CompLoc(s_loose.GetObj("HandSocket"));
        Log("SOCKET world ({:.2f}, {:.2f}, {:.2f}), {:.2f} cm below the magazine's origin", s.x, s.y, s.z, s_magAt.z - s.z);
        s_step = 8; s_wait = 0.5;
        return;
    }
    case 8:
        Snap("mag_out");
        s_step = 9; s_wait = 0.5;
        return;
    case 9: {
        Object fresh = Spawn(kMag);  // a magazine on its own, as an ammo box or a pouch would make one
        NoPhysics(fresh);
        Vec away = s_magAt; away.z += 200;
        Params mv(fresh, "K2_SetActorLocation"); mv.Set("NewLocation", away).Set("bSweep", false).Set("bTeleport", true).Call();
        s_loose = fresh;
        s_step = 10; s_wait = 1.0;
        return;
    }
    case 10:
        MagState("spawned on its own, 1 s", s_loose);
        SocketState("spawned on its own", s_loose);
        Log("DEV TEST COMPLETE");
        s_step = 99;
        return;
    default: return;
    }
}

GML_AWAKE() {
    // Every round the test gun actually fires: the game broadcasts OnFired from its chamber per shot.
    HookAfter("GunChamberComponent:OnFired", [](Object chamber, GUFunction*, void*) {
        if (s_gun && chamber.GetObj("Receiver").ptr == s_gun.ptr) s_shots++;
    });
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) {
        std::string level = Object((GUObject*)gm).Path();
        if (level.find("/Game/Maps/MainMenu/") == 0) {
            Params p(Lib("GameplayStatics"), "OpenLevel");
            p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName(kMap)).Set("bAbsolute", true).Call();
        } else if (level.find(kMap) == 0) {
            s_step = 0;
            s_wait = 6;
        }
    });
    On(GML_EVENT_TICK, [](void*) {
        static LARGE_INTEGER freq{}, last{};
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); last = now; }
        double dt = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        last = now;
        if (s_step >= 0 && s_step < 99) Step(dt);
    });
    return 0;
}
