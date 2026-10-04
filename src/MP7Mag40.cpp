// MP7 40-Round Magazine - GML plugin (com.remarrow.mp7mag40).
//
// The game's MP7 (Firearm_SMG_MP7A2) takes one magazine, Magazine_MP7A2_20rnd. Every such magazine
// that appears in the world - in a gun, in a pouch or on its own - becomes a 40-rounder:
//
//   Capacity  its AmmoCapacity goes to 40, and one that is full when first seen is filled to 40 (on
//             the machine with authority over it; CurrentAmmo replicates from there). Reloads,
//             ammo pickups and the gun's feeding all run on those two numbers, as for any magazine.
//   Look      the 40-round magazine from the MP7 model (built into this DLL, encrypted) hangs on the
//             magazine's own mesh component, whose 20-round mesh is hidden, so it goes wherever the
//             magazine goes. The game's rounds, laid out for the 20-round shape, are hidden as well.
//
// Settings: GML\config\com.remarrow.mp7mag40.cfg. Build: build.bat [install].
// Assets: tools\prepare_mag.py writes Assets\ from the model's .blend (not in the repo); tools\pack
// encrypts them into the payload that src\MP7Mag40.rc builds into the DLL (src\payload.h).
#include <GML/GML.hpp>
#include <windows.h>
#include <algorithm>
#include <unordered_map>
#include "payload.h"
#include "payload_key.h"  // generated per build by tools\pack (build\gen), never committed
#include "version.h"

using namespace gml;

GML_PLUGIN("com.remarrow.mp7mag40", "MP7 40-Round Magazine", MP7MAG40_VERSION);

static const char* kMagClass = "Magazine_MP7A2_20rnd_C";
static const char* kMagMesh = "MP7A2_Mag_20";  // the static mesh the 40 replaces (its mesh space is ours)
static const char* kShader = "/Game/Art/Common/M_DefaultShader.M_DefaultShader";
static constexpr int32_t kCapacity = 40;

static ConfigEntry<bool> s_enabled;
static GUObject* s_mesh = nullptr;  // runtime UDynamicMesh: the 40-round magazine
static GUObject* s_mid = nullptr;   // M_DefaultShader instance with its textures
static bool s_assetsTried = false;

// A magazine full when it shows up (the game may set its round count a moment after spawning it) is
// filled to 40; after that its rounds are the game's business.
static constexpr ULONGLONG kFillWindowMs = 1500;
struct Mag {
    ULONGLONG seenAt = 0;
    int32_t stock = 20;      // the capacity the game gave it
    GUObject* look = nullptr;
    GUObject* grab = nullptr;  // the box that makes the 40's lower part grabbable
};

// The hand. The magazine's HandSocket (a VRExpansion hand socket: where a gripping hand snaps, its
// mesh placed relative to it) sits 1.36 cm under the 20's baseplate, so the palm cups the bottom of
// the magazine. The 40's baseplate is 7.23 cm lower and, as the magazine leans forward, centred
// 1.90 cm further forward (x 2.45 against 0.55), so the socket moves by that much: the hand cups the
// 40's baseplate the way it cupped the 20's, and the top stays free to go into the gun.
// Magazine mesh space, cm.
struct V3 { double x, y, z; };
struct R3 { double pitch, yaw, roll; };
static constexpr V3 kHandShift{1.90, 0.0, -7.23};
// The 40 reaches below the 20's collision (which the magazine keeps: it is its root), so its lower
// part gets a box of its own for hands to find: from the 20's bottom (z -8.97) to the 40's (-16.20),
// leaning forward with the magazine's baseplate. Query only: it never takes part in physics.
static constexpr V3 kGrabCentre{1.52, 0.0, -12.6}, kGrabExtent{2.6, 1.4, 3.9};
static constexpr R3 kGrabTilt{10.0, 0.0, 0.0};
static std::unordered_map<GUObject*, Mag> s_mags;

// ------------------------------------------------------------------ assets

// Decrypts one asset from the payload into `out`; callers wipe it once the engine has it.
static bool Unpack(const char* name, std::vector<uint8_t>& out) {
    static Blob blob = Resource("DATA");
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = kPayloadKeyA[i] ^ kPayloadKeyB[i];
    bool ok = blob && payload::Extract(blob.data, blob.size, key, name, out);
    SecureZeroMemory(key, sizeof key);
    if (!ok) Error("embedded asset '{}' is missing or damaged", name);
    return ok;
}

static bool BuildAssets() {
    if (s_assetsTried) return s_mesh != nullptr;
    s_assetsTried = true;
    if (API->size < offsetof(GML_API, ImportDynamicMeshFromMemory) + sizeof(void*)) {
        Error("this plugin needs GML 2.2 or newer (assets embedded in the DLL)");
        return false;
    }
    Object shader = LoadObject(kShader);
    if (!shader) { Error("weapon shader {} not found", kShader); return false; }
    s_mid = API->CreateMaterialInstance(shader.ptr);
    if (!s_mid) { Error("could not create the magazine's material"); return false; }
    std::vector<uint8_t> buf;
    for (const char* param : {"Diffuse", "Normal", "ORM"}) {
        std::string res = std::string("Mag_") + param;
        GUObject* tex = Unpack(res.c_str(), buf) ? API->ImportTextureFromMemory(buf.data(), buf.size(), res.c_str()) : nullptr;
        payload::Wipe(buf);
        if (!tex || !API->SetMaterialTexture(s_mid, param, tex)) Warn("texture {} missing - that channel keeps the shader default", param);
    }
    static const char* groups[] = {"MI_Mag"};
    GML_MeshImport o{};
    o.size = sizeof o;
    o.axis = GML_AXIS_BLENDER_OBJ;
    o.flipWinding = -1;
    o.materialCount = 1;
    o.materialNames = groups;
    o.materials = &s_mid;
    if (Unpack("mp7mag40", buf)) s_mesh = API->ImportDynamicMeshFromMemory(buf.data(), buf.size(), "mp7mag40", &o);
    payload::Wipe(buf);
    if (!s_mesh) { Error("could not build the 40-round magazine mesh"); return false; }
    Log("40-round magazine ready");
    return true;
}

// ------------------------------------------------------------------ magazines

static bool CallBool(Object o, const char* fn) {
    Params p(o, fn);
    p.Call();
    return p.Return<bool>();
}

static void HideComponent(Object c) {
    Params v(c, "SetVisibility");
    v.Set("bNewVisibility", false).Set("bPropagateToChildren", false).Call();  // attached children stay visible
}

// The game draws the magazine's rounds (instanced meshes) along the 20-round's stack; they would sit
// in the wrong place in the 40, and the 40 is solid - so they stay hidden.
static void HideRounds(Object mag) {
    Params gc(mag, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("InstancedStaticMeshComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        if (c.GetBool("bVisible")) HideComponent(c);
    }
}

// The components showing the 20-round mesh. The magazine has two: its root (collision and grip, an
// OptionalRepStaticMeshComponent) and a plain StaticMeshComponent on it. Both are hidden; the 40 hangs
// on the root, in the 20's mesh space.
static std::vector<Object> MeshComponents(Object mag) {
    std::vector<Object> out;
    Params gc(mag, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("StaticMeshComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object c(((GUObject**)arr->Data)[i]);
        Object m = c.GetObj("StaticMesh");
        if (m && m.Name() == kMagMesh && !c.IsA("InstancedStaticMeshComponent")) out.push_back(c);
    }
    Object root = mag.GetObj("RootComponent");
    std::stable_partition(out.begin(), out.end(), [&](Object& c) { return c.ptr == root.ptr; });  // the root first
    return out;
}

static void HideTwenty(Object mag) {
    for (Object c : MeshComponents(mag))
        if (c.GetBool("bVisible")) HideComponent(c);
}

// Moves the hand socket onto the 40 (once per magazine: the socket is the magazine's own component).
static void MoveHand(Object mag) {
    Object s = mag.GetObj("HandSocket");
    Object root = mag.GetObj("RootComponent");
    if (!s || !root) return;
    Params par(s, "GetAttachParent");
    par.Call();
    if (par.Return<GUObject*>() != root.ptr) {  // the shift is in the root's (the mesh's) space
        Warn("{}: its hand socket is not on the magazine's root - left where it is", mag.Name());
        return;
    }
    V3 was = s.Get<V3>("RelativeLocation");
    V3 to{was.x + kHandShift.x, was.y + kHandShift.y, was.z + kHandShift.z};
    Params mv(s, "K2_SetRelativeLocation");
    mv.Set("NewLocation", to).Set("bSweep", false).Set("bTeleport", true).Call();
    Log("{}: hand socket moved from ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f})", mag.Name(), was.x, was.y, was.z, to.x, to.y, to.z);
}

static Object AddGrabBox(Object mag) {
    Object root = mag.GetObj("RootComponent");
    GUClass* box = (GUClass*)FindClass("BoxComponent");
    if (!root || !box) return {};
    Params mk(Lib("KismetMathLibrary"), "MakeTransform");
    mk.Set("Location", kGrabCentre).Set("Rotation", kGrabTilt).Set("Scale", V3{1, 1, 1}).Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &xi);
    Params add(mag, "AddComponentByClass");
    add.Set("Class", box).Set("bManualAttachment", true).Set("bDeferredFinish", false);
    std::memcpy(add.Ptr("RelativeTransform"), mk.Ptr("ReturnValue"), xi.size);
    add.Call();
    Object b = add.Return<GUObject*>();
    if (!b) return {};
    Params at(b, "K2_AttachToComponent");
    at.SetObj("Parent", root).Set("SocketName", uint64_t(0)).Set("LocationRule", (uint8_t)0).Set("RotationRule", (uint8_t)0)
        .Set("ScaleRule", (uint8_t)0).Set("bWeldSimulatedBodies", false).Call();  // KeepRelative: the transform above
    Params ext(b, "SetBoxExtent");
    ext.Set("InBoxExtent", kGrabExtent).Set("bUpdateOverlaps", true).Call();
    Params prof(root, "GetCollisionProfileName");
    prof.Call();
    Params sp(b, "SetCollisionProfileName");
    sp.Set("InCollisionProfileName", prof.Return<uint64_t>()).Set("bUpdateOverlaps", true).Call();
    Params go(b, "SetGenerateOverlapEvents");
    go.Set("bInGenerateOverlapEvents", true).Call();
    return b;
}

// The grab box follows the magazine's own collision: off while the game has it off (inserted, in a
// pouch...), query only otherwise.
static void SyncGrabBox(Object mag, Object b) {
    Object root = mag.GetObj("RootComponent");
    Params ce(root, "GetCollisionEnabled");
    ce.Call();
    uint8_t want = ce.Return<uint8_t>() == 0 ? 0 : 1;  // ECollisionEnabled: NoCollision, QueryOnly
    Params cur(b, "GetCollisionEnabled");
    cur.Call();
    if (cur.Return<uint8_t>() != want) {
        Params set(b, "SetCollisionEnabled");
        set.Set("NewType", want).Call();
    }
}

static void Dress(Object mag, Mag& m) {
    // Capacity on every machine (the game reads it locally); the round count only where it is decided.
    ULONGLONG now = GetTickCount64();
    if (!m.seenAt) m.seenAt = now;
    int32_t cap = mag.Get<int32_t>("AmmoCapacity");
    if (cap != kCapacity) {
        m.stock = cap;
        mag.Set("AmmoCapacity", kCapacity);
    }
    if (now - m.seenAt < kFillWindowMs && mag.Get<int32_t>("CurrentAmmo") == m.stock && CallBool(mag, "HasAuthority")) {
        mag.Set("CurrentAmmo", kCapacity);
        Params rep(mag, "OnRep_CurrentAmmo");
        rep.Call();
    }
    if (m.look && API->IsValid(m.look)) {
        HideTwenty(mag);  // the game may show them again
        HideRounds(mag);
        if (m.grab && API->IsValid(m.grab)) SyncGrabBox(mag, Object(m.grab));
        return;
    }
    std::vector<Object> twenty = MeshComponents(mag);
    if (twenty.empty() || !BuildAssets()) return;
    Object comp = twenty.front();
    Object look = API->AddDynamicMeshComponent(mag.ptr, s_mesh, &s_mid, 1);
    if (!look) return;
    Params at(look, "K2_AttachToComponent");
    at.SetObj("Parent", comp).Set("SocketName", uint64_t(0))
        .Set("LocationRule", (uint8_t)2).Set("RotationRule", (uint8_t)2).Set("ScaleRule", (uint8_t)2)  // SnapToTarget
        .Set("bWeldSimulatedBodies", false).Call();
    HideTwenty(mag);
    HideRounds(mag);
    MoveHand(mag);
    if (Object b = AddGrabBox(mag)) {
        SyncGrabBox(mag, b);
        m.grab = b.ptr;
    }
    m.look = look.ptr;
    Log("{} is a 40-round magazine now ({} rounds)", mag.Name(), mag.Get<int32_t>("CurrentAmmo"));
}

// Four times a second: every MP7 magazine in the world.
static void Watch() {
    static ULONGLONG next = 0;
    if (!s_enabled.Value() || GetTickCount64() < next) return;
    next = GetTickCount64() + 250;
    std::unordered_map<GUObject*, Mag> seen;
    for (Object mag : FindAllOf(kMagClass)) {
        if (!API->IsValid(mag.ptr)) continue;
        auto it = s_mags.find(mag.ptr);
        Mag m = it != s_mags.end() ? it->second : Mag{};
        Dress(mag, m);
        seen[mag.ptr] = m;
    }
    s_mags.swap(seen);  // magazines that are gone are forgotten
}

GML_AWAKE() {
    s_enabled = Config.Bind("Magazine", "FortyRounds", true,
        "MP7 magazines hold 40 rounds and look like the 40-round magazine (takes effect at the next launch).");
    // A magazine that has just begun play is dressed at once, not up to a quarter second later.
    HookAfter("Magazine_MP7A2_20rnd_C:ReceiveBeginPlay", [](Object mag, GUFunction*, void*) {
        if (!s_enabled.Value()) return;
        Mag& m = s_mags[mag.ptr];
        Dress(mag, m);
    });
    On(GML_EVENT_TICK, [](void*) { Watch(); });
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void*) {
        BuildAssets();     // while the level loads, not when the first magazine shows up
        s_mags.clear();    // a new level's actors can reuse the old ones' addresses
    });
    Message("MP7 40-Round Magazine {} - {}", MP7MAG40_VERSION, s_enabled.Value() ? "MP7 magazines hold 40" : "off");
    Log("{}", MP7MAG40_MODEL_CREDIT);
    return 0;
}
