// scoot would asset tools - rider animation clips
#include "rider_clips.h"

namespace sw::tools {

//   hip flexion +X, knee flexion -X, spine forward bend -X, elbow flexion +X,
//   right arm abduction +Z (left -Z), yaw +Y turns left

PoseDef ridePose() {
    // regular stance (reference: street riders): left foot forward by the headtube, right foot on the
    // tail turned out; knees well bent with the back knee dropped in towards the front leg, hips open
    // towards the back foot, shoulders turned back square to the bars, slight forward lean
    PoseDef p;
    p.hips(0.012f, -0.16f, 0.04f)
        .set(RJ_Pelvis, -6, -30, 2)
        .set(RJ_Spine, -12, 14, -2)
        .set(RJ_Chest, -8, 10, 0)
        .set(RJ_Neck, 10, 4, 0)
        .set(RJ_Head, 6, 2, 0)
        .pair(RJ_UpperArmL, RJ_UpperArmR, 36, 0, 14)
        .pair(RJ_LowerArmL, RJ_LowerArmR, 38)
        .set(RJ_ThighL, 40, -6, -4)
        .set(RJ_ShinL, -66)
        .set(RJ_FootL, 24)
        .set(RJ_ThighR, 46, 10, -12)
        .set(RJ_ShinR, -78)
        .set(RJ_FootR, 26, 18, 0);
    return p;
}
PoseDef tuckPose() {
    // air tuck (photos of airs and whips): torso upright, the scooter pulled up to the chest, knees up and apart
    // either side of the stem, eyes down on the deck
    PoseDef p = ridePose();
    p.hips(0, -0.1f, 0.02f).set(RJ_Pelvis, -4, -14, 0).set(RJ_Spine, -8, 6, 0).set(RJ_Chest, -3, 4, 0).set(RJ_Neck, -10).set(RJ_Head, -6);
    p.pair(RJ_ThighL, RJ_ThighR, 80, 0, 12).pair(RJ_ShinL, RJ_ShinR, -116).pair(RJ_FootL, RJ_FootR, 28);
    p.pair(RJ_UpperArmL, RJ_UpperArmR, 14, 0, 26).pair(RJ_LowerArmL, RJ_LowerArmR, 96);
    return p;
}

std::vector<ClipDef> clipDefs() {
    std::vector<ClipDef> c;
    PoseDef B = ridePose();
    auto hold = [&](const std::string& n, const PoseDef& p) { c.push_back({n, {{0.0f, p}, {1.0f, p}}}); };

    // idle: upright, breathing
    {
        PoseDef a = B;
        // standing on the scooter: the riding stance, a little taller and more relaxed
        a.hips(0.008f, -0.105f, 0.03f).set(RJ_Pelvis, -4, -26, 2).set(RJ_Spine, -8, 12, -2).set(RJ_Chest, -4, 8, 0).set(RJ_Neck, 6, 4, 0).set(RJ_Head, 3);
        PoseDef b = a;
        b.add(RJ_Chest, -2).add(RJ_Neck, 1).add(RJ_Head, 1, 4, 0);
        b.pelvis.y -= 0.006f;
        c.push_back({"idle", {{0.0f, a}, {1.4f, b}, {2.8f, a}}});
    }
    // ride: small weight shifts
    {
        PoseDef a = B, b = B;
        b.add(RJ_Spine, -1, 3, 0).add(RJ_Head, 0, -3, 0);
        b.pelvis.y -= 0.008f;
        c.push_back({"ride", {{0.0f, a}, {0.8f, b}, {1.6f, a}}});
    }
    // push: back (right) foot plants beside the deck and kicks back; front knee bends
    {
        PoseDef k0 = B, k1 = B, k2 = B, k3 = B;
        k1.hips(0, -0.1f, 0.03f).set(RJ_ThighL, 45).set(RJ_ShinL, -75).set(RJ_ThighR, 8, 0, 9).set(RJ_ShinR, -22).set(RJ_FootR, 5);
        k1.add(RJ_Spine, -4);
        k2.hips(0, -0.1f, 0.02f).set(RJ_ThighL, 42).set(RJ_ShinL, -72).set(RJ_ThighR, -28, 0, 7).set(RJ_ShinR, -12).set(RJ_FootR, -25);
        k2.add(RJ_Spine, -6).add(RJ_Chest, -2);
        k3.set(RJ_ThighR, 30).set(RJ_ShinR, -70).set(RJ_FootR, 10);
        c.push_back({"push", {{0.0f, k0}, {0.12f, k1}, {0.25f, k2}, {0.34f, k3}, {0.42f, k0}}});
    }
    // crouch (pop charge, compression)
    {
        PoseDef p = B;
        p.hips(0, -0.25f, 0.06f)
            .set(RJ_Pelvis, -10)
            .set(RJ_Spine, -24)
            .set(RJ_Chest, -12)
            .set(RJ_Neck, 24)
            .set(RJ_Head, 12)
            .pair(RJ_UpperArmL, RJ_UpperArmR, 22, 0, 12)
            .pair(RJ_LowerArmL, RJ_LowerArmR, 72)
            .pair(RJ_ThighL, RJ_ThighR, 72)
            .pair(RJ_ShinL, RJ_ShinR, -112)
            .pair(RJ_FootL, RJ_FootR, 38);
        hold("crouch", p);
    }
    // air: knees up, compact
    {
        // photos of plain airs: upright, bars pulled up to the chest (elbows bent and out), knees bent under it
        PoseDef a = B;
        a.hips(0, -0.12f, 0.02f).set(RJ_Pelvis, -4, -16, 0).set(RJ_Spine, -8, 6, 0).set(RJ_Chest, -3, 4, 0).set(RJ_Neck, -8).set(RJ_Head, -4);
        a.pair(RJ_ThighL, RJ_ThighR, 64, 0, 8).pair(RJ_ShinL, RJ_ShinR, -100).pair(RJ_FootL, RJ_FootR, 20);
        a.pair(RJ_UpperArmL, RJ_UpperArmR, 16, 0, 24).pair(RJ_LowerArmL, RJ_LowerArmR, 90);
        PoseDef b = a;
        b.add(RJ_Spine, -2).add(RJ_Head, 2);
        c.push_back({"air", {{0.0f, a}, {0.6f, b}, {1.2f, a}}});
    }
    // landings: absorb and recover
    {
        PoseDef d = B;
        d.hips(0, -0.2f, 0.05f).set(RJ_Spine, -24).set(RJ_Chest, -10).set(RJ_Neck, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 60);
        c.push_back({"land", {{0.0f, d}, {0.3f, B}}});
        PoseDef h = d;
        h.hips(0, -0.3f, 0.07f).set(RJ_Spine, -36).set(RJ_Chest, -14).set(RJ_Neck, 32).pair(RJ_LowerArmL, RJ_LowerArmR, 85);
        c.push_back({"hard_land", {{0.0f, h}, {0.2f, h}, {0.55f, B}}});
    }
    // manuals
    {
        PoseDef m = B;
        m.hips(0, -0.04f, 0.07f).set(RJ_Pelvis, 6).set(RJ_Spine, 6).set(RJ_Chest, 2).set(RJ_Neck, -4).set(RJ_Head, -2);
        m.pair(RJ_UpperArmL, RJ_UpperArmR, 52, 0, 8).pair(RJ_LowerArmL, RJ_LowerArmR, 6);
        PoseDef m2 = m;
        m2.add(RJ_Spine, 1, 0, 2).add(RJ_UpperArmL, 0, 0, -4);
        c.push_back({"manual", {{0.0f, m}, {0.7f, m2}, {1.4f, m}}});
        PoseDef n = B;
        n.hips(0, -0.12f, -0.04f).set(RJ_Pelvis, -12).set(RJ_Spine, -26).set(RJ_Chest, -10).set(RJ_Neck, 26).set(RJ_Head, 12);
        n.pair(RJ_UpperArmL, RJ_UpperArmR, 20, 0, 12).pair(RJ_LowerArmL, RJ_LowerArmR, 76);
        PoseDef n2 = n;
        n2.add(RJ_Spine, -1, 0, -2);
        c.push_back({"nose_manual", {{0.0f, n}, {0.7f, n2}, {1.4f, n}}});
    }
    // grinds: low and centred, arms balance
    {
        PoseDef g = B;
        g.hips(0, -0.13f, 0.02f).set(RJ_Spine, -18).set(RJ_Chest, -6).set(RJ_Neck, 14).set(RJ_Head, 6);
        g.pair(RJ_ThighL, RJ_ThighR, 45).pair(RJ_ShinL, RJ_ShinR, -76).pair(RJ_UpperArmL, RJ_UpperArmR, 35, 0, 14).pair(RJ_LowerArmL, RJ_LowerArmR, 42);
        PoseDef g2 = g;
        g2.add(RJ_Spine, 0, 0, 3).add(RJ_Chest, 0, 0, 2);
        PoseDef g3 = g;
        g3.add(RJ_Spine, 0, 0, -3).add(RJ_Chest, 0, 0, -2);
        c.push_back({"grind", {{0.0f, g}, {0.5f, g2}, {1.0f, g}, {1.5f, g3}, {2.0f, g}}});
        PoseDef b = g;
        b.set(RJ_Pelvis, -6, 40, 0).set(RJ_Spine, -14, 22, 0).set(RJ_Chest, -4, 8, 0).set(RJ_Neck, 10, -35, 0).set(RJ_Head, 4, -30, 0);
        PoseDef b2 = b;
        b2.add(RJ_Chest, 0, 0, 3);
        c.push_back({"boardslide", {{0.0f, b}, {0.6f, b2}, {1.2f, b}}});
    }
    // scooter trick clips: keyed over the trick (0 = just after the pop, 1 = landed), sampled along its progress.
    // While a hand or foot is on the scooter the IK holds it there; the keys shape everything that is free.
    // Regular stance: left foot front, right foot on the tail (goofy riders get the mirror).
    {
        using Keys = std::vector<std::pair<float, PoseDef>>;
        auto keyed = [&](const std::string& n, Keys k) { c.push_back({n, std::move(k)}); };
        PoseDef T = tuckPose();
        // pop: legs just extended from the pop, arms pulling the scooter up
        PoseDef pop = B;
        pop.hips(0, -0.05f, 0.02f).set(RJ_Pelvis, -4, -18, 0).set(RJ_Spine, -6, 6, 0).set(RJ_Chest, -3, 4, 0).set(RJ_Neck, -4);
        pop.pair(RJ_ThighL, RJ_ThighR, 34, 0, 4).pair(RJ_ShinL, RJ_ShinR, -52).pair(RJ_FootL, RJ_FootR, 16);
        pop.pair(RJ_UpperArmL, RJ_UpperArmR, 18, 0, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 96);
        // tuck (photos of whips): torso upright over the scooter pulled up to the chest, eyes down on the deck,
        // knees up and apart either side of the stem, feet just above the deck as it goes round
        auto tight = [&](float thigh, float shin) {
            PoseDef p = T;
            p.hips(0, -0.1f, 0.02f).set(RJ_Pelvis, -4, -12, 0).set(RJ_Spine, -8, 4, 0).set(RJ_Chest, -3, 2, 0).set(RJ_Neck, -14).set(RJ_Head, -8);
            p.pair(RJ_ThighL, RJ_ThighR, thigh, 0, 20).pair(RJ_ShinL, RJ_ShinR, shin).pair(RJ_FootL, RJ_FootR, 22);
            p.pair(RJ_UpperArmL, RJ_UpperArmR, 12, 0, 28).pair(RJ_LowerArmL, RJ_LowerArmR, 100);
            return p;
        };
        // front foot reaches down first and stops the deck, back foot still up
        auto catchFront = [&](PoseDef p) {
            p.set(RJ_ThighL, 52, 0, 6).set(RJ_ShinL, -72).set(RJ_FootL, 16).set(RJ_Neck, -10);
            return p;
        };
        PoseDef land = B;
        land.hips(0, -0.12f, 0.03f).set(RJ_Spine, -12).set(RJ_Chest, -4).set(RJ_Neck, -4).set(RJ_ThighL, 46, -6, -4).set(RJ_ShinL, -80).set(RJ_ThighR, 50, 10, -12).set(RJ_ShinR, -88);

        // tailwhip: the back foot kicks the tail out to the side, the hips answer the other way, then both knees
        // come up while the deck goes round underneath
        PoseDef kick = pop;
        kick.set(RJ_Pelvis, -4, 12, 4).set(RJ_Spine, -8, -6, 0).set(RJ_Chest, -3, -6, 0).set(RJ_Neck, -12).set(RJ_Head, -8);
        kick.set(RJ_ThighR, 4, 0, 30).set(RJ_ShinR, -12).set(RJ_FootR, -24).set(RJ_ThighL, 70, 0, 12).set(RJ_ShinL, -100);
        // mid whip (photo): front knee pulled up, the kicking leg still hanging down, then it comes up for the catch
        PoseDef hang = tight(84, -112);
        hang.set(RJ_ThighR, 24, 0, 16).set(RJ_ShinR, -30).set(RJ_FootR, -10);
        keyed("whip", {{0.0f, pop}, {0.1f, kick}, {0.3f, hang}, {0.55f, tight(86, -118)}, {0.8f, catchFront(tight(84, -116))}, {1.0f, land}});
        // heelwhip: the back heel sweeps the tail round the other way, towards the front foot
        PoseDef heel = pop;
        heel.set(RJ_Pelvis, -4, -12, -4).set(RJ_Spine, -8, 6, 0).set(RJ_Neck, -12).set(RJ_Head, -8);
        heel.set(RJ_ThighR, 44, 0, -24).set(RJ_ShinR, -64).set(RJ_FootR, 8).set(RJ_ThighL, 72, 0, 4).set(RJ_ShinL, -104);
        keyed("heelwhip", {{0.0f, pop}, {0.1f, heel}, {0.3f, tight(86, -118)}, {0.6f, tight(88, -120)}, {0.8f, catchFront(tight(84, -116))}, {1.0f, land}});
        // kickless: no kick, the knees come straight up and the arms swing the deck round (shoulders wind up and
        // swing through)
        PoseDef kl1 = tight(100, -132), kl2 = tight(100, -132), kl3 = tight(98, -130);
        kl1.set(RJ_Spine, -8, -10, 0).set(RJ_Chest, -3, -16, 0).pair(RJ_UpperArmL, RJ_UpperArmR, 14, 0, 24).pair(RJ_LowerArmL, RJ_LowerArmR, 104);
        kl2.set(RJ_Spine, -8, 10, 0).set(RJ_Chest, -3, 14, 0).pair(RJ_UpperArmL, RJ_UpperArmR, 24, 0, 30).pair(RJ_LowerArmL, RJ_LowerArmR, 80);
        kl3.set(RJ_Chest, -3, 6, 0);
        keyed("kickless", {{0.0f, pop}, {0.08f, kl1}, {0.4f, kl2}, {0.7f, kl3}, {0.84f, catchFront(kl3)}, {1.0f, land}});
        // barspin: the front hand pushes the bar away, the back hand pulls, both let go, the arms stay open in
        // front ready to catch; the feet stay on
        PoseDef thr = pop;
        thr.set(RJ_UpperArmL, 60, 0, -18).set(RJ_LowerArmL, 30).set(RJ_UpperArmR, 4, 0, 30).set(RJ_LowerArmR, 104).set(RJ_Chest, -3, 12, 0);
        // photos of barspins: upright, knees bent, eyes on the bar spinning at chest height, hands open just over it
        PoseDef open = pop;
        open.hips(0, -0.14f, 0.02f).set(RJ_Spine, -6, 4, 0).set(RJ_Neck, -16).set(RJ_Head, -8).pair(RJ_ThighL, RJ_ThighR, 60, 0, 6).pair(RJ_ShinL, RJ_ShinR, -98);
        open.pair(RJ_UpperArmL, RJ_UpperArmR, 20, 0, 26).pair(RJ_LowerArmL, RJ_LowerArmR, 80);
        PoseDef open2 = open;
        open2.add(RJ_Chest, 0, -6, 0);
        PoseDef grab = open;
        grab.pair(RJ_UpperArmL, RJ_UpperArmR, 18, 0, 20).pair(RJ_LowerArmL, RJ_LowerArmR, 84);
        keyed("barspin", {{0.0f, pop}, {0.12f, thr}, {0.3f, open}, {0.7f, open2}, {0.86f, grab}, {1.0f, B}});
        // bartwist: the front hand keeps hold and twists the bar round, the back hand lets go and waits
        PoseDef tw = pop;
        tw.set(RJ_UpperArmR, 52, 0, 38).set(RJ_LowerArmR, 30).set(RJ_Neck, -10);
        keyed("bartwist", {{0.0f, pop}, {0.12f, tw}, {0.5f, tw}, {0.86f, grab}, {1.0f, B}});
        // x-up: the arms cross as the bars go 180 (the IK keeps the hands on), body compact
        PoseDef xu = pop;
        xu.pair(RJ_ThighL, RJ_ThighR, 70, 0, 6).pair(RJ_ShinL, RJ_ShinR, -104).set(RJ_Chest, -8, 10, 0).set(RJ_Neck, -8);
        keyed("x_up", {{0.0f, pop}, {0.5f, xu}, {1.0f, B}});
        // full whip: whip kick and bar throw together, knees up, arms open, catch bars and deck together
        PoseDef fk = kick;
        fk.set(RJ_UpperArmL, 60, 0, -18).set(RJ_LowerArmL, 30).set(RJ_UpperArmR, 4, 0, 30).set(RJ_LowerArmR, 104);
        PoseDef fo = tight(96, -128);
        fo.pair(RJ_UpperArmL, RJ_UpperArmR, 22, 0, 30).pair(RJ_LowerArmL, RJ_LowerArmR, 80);
        PoseDef fc = catchFront(fo);
        fc.pair(RJ_UpperArmL, RJ_UpperArmR, 16, 0, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 90);
        keyed("full_whip", {{0.0f, pop}, {0.1f, fk}, {0.3f, fo}, {0.66f, fo}, {0.84f, fc}, {1.0f, land}});
        // fingerwhip: bent over, the back hand grabs the deck and throws it round, then goes back to the bar
        PoseDef fw1 = tight(88, -120), fw2 = tight(90, -122), fw3 = tight(96, -128);
        // the back hand reaches down to the side of the deck (the IK puts it there), so the chest goes down with it
        fw1.pair(RJ_ThighL, RJ_ThighR, 88, 0, 18).set(RJ_Pelvis, -10, -6, 0).set(RJ_Spine, -28, 10, 0).set(RJ_Chest, -10, 8, 0).set(RJ_Neck, -6);
        fw1.set(RJ_UpperArmR, 20, 0, 20).set(RJ_LowerArmR, 16);
        fw2.pair(RJ_ThighL, RJ_ThighR, 90, 0, 18).set(RJ_Spine, -16, 6, 0).set(RJ_UpperArmR, -10, 0, 44).set(RJ_LowerArmR, 20);
        fw3.set(RJ_UpperArmR, 30, 0, 34).set(RJ_LowerArmR, 70);
        keyed("fingerwhip", {{0.0f, pop}, {0.1f, fw1}, {0.25f, fw2}, {0.55f, fw3}, {0.82f, catchFront(fw3)}, {1.0f, land}});
        // bri flip: the scooter is kicked out in front and pulled up, the arms go up and out to the side as it
        // circles over the head; knees tucked out of its way, eyes on it, back down to catch
        // photo of an inward bri: the legs split in a stride (front knee up, back leg stretched out behind) to let
        // the scooter round, torso leaning into it, both hands on the bar in front of the chest
        PoseDef br1 = tight(96, -126), br2 = tight(96, -126), br3 = tight(92, -122);
        br1.set(RJ_Spine, -14, 0, -6).set(RJ_Chest, -4, 0, -4).set(RJ_Neck, 10).set(RJ_Head, 4);
        br1.set(RJ_ThighL, 76, 0, 8).set(RJ_ShinL, -86).set(RJ_ThighR, -18, 0, 10).set(RJ_ShinR, -64).set(RJ_FootR, -16);
        br1.pair(RJ_UpperArmL, RJ_UpperArmR, 60, 0, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 50);
        br2.set(RJ_Spine, -18, 0, -8).set(RJ_Chest, -6, 0, -4).set(RJ_Neck, 6).set(RJ_Head, 2);
        br2.set(RJ_ThighL, 82, 0, 8).set(RJ_ShinL, -90).set(RJ_ThighR, -30, 0, 10).set(RJ_ShinR, -56).set(RJ_FootR, -20);
        br2.pair(RJ_UpperArmL, RJ_UpperArmR, 70, 0, 24).pair(RJ_LowerArmL, RJ_LowerArmR, 44);
        br3.pair(RJ_UpperArmL, RJ_UpperArmR, 30, 0, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 80).set(RJ_Neck, -10);
        keyed("bri", {{0.0f, pop}, {0.14f, br1}, {0.5f, br2}, {0.78f, catchFront(br3)}, {1.0f, land}});
        // inward: the same, swung forwards: arms push out in front, chest back
        PoseDef in1 = br1, in2 = br2;
        in1.set(RJ_Spine, -8, 0, -6).pair(RJ_UpperArmL, RJ_UpperArmR, 70, 0, 18).pair(RJ_LowerArmL, RJ_LowerArmR, 36);
        in2.set(RJ_Spine, -10, 0, -8).pair(RJ_UpperArmL, RJ_UpperArmR, 84, 0, 20).pair(RJ_LowerArmL, RJ_LowerArmR, 30);
        keyed("inward", {{0.0f, pop}, {0.14f, in1}, {0.5f, in2}, {0.78f, catchFront(br3)}, {1.0f, land}});
        // front scooter flip: the back hand lets go, the front hand throws the scooter forwards round the bar,
        // knees up, then the arms pull it back under the feet
        // photo: knees pulled right up to the chest, compact and upright, the front arm up holding the bar while the
        // scooter turns round beside it, the free arm out for balance
        PoseDef fs1 = tight(104, -134), fs2 = tight(106, -136), fs3 = tight(96, -126);
        fs1.set(RJ_Spine, -2, 0, 0).set(RJ_Chest, 0).set(RJ_Neck, -2).set(RJ_UpperArmR, 40, 0, 50).set(RJ_LowerArmR, 30);
        fs2.set(RJ_Spine, 0, 0, 0).set(RJ_Chest, 2).set(RJ_Neck, 0).set(RJ_UpperArmR, 34, 0, 56).set(RJ_LowerArmR, 26);
        fs3.pair(RJ_UpperArmL, RJ_UpperArmR, 18, 0, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 88);
        keyed("front_scoot", {{0.0f, pop}, {0.12f, fs1}, {0.5f, fs2}, {0.8f, catchFront(fs3)}, {1.0f, land}});
        // nothing front scoot: everything lets go, spread out, back together to catch
        PoseDef no1 = pop, no2 = B, no3 = tight(60, -90);
        no1.pair(RJ_UpperArmL, RJ_UpperArmR, 30, 0, 70).pair(RJ_LowerArmL, RJ_LowerArmR, 20).pair(RJ_ThighL, RJ_ThighR, -4, 0, 20).pair(RJ_ShinL, RJ_ShinR, -30);
        no2.hips(0, 0.0f, 0.0f).set(RJ_Spine, 4).set(RJ_Chest, 6).set(RJ_Head, -6).pair(RJ_UpperArmL, RJ_UpperArmR, 26, 0, 104).pair(RJ_LowerArmL, RJ_LowerArmR, 18);
        no2.pair(RJ_ThighL, RJ_ThighR, -10, 0, 22).pair(RJ_ShinL, RJ_ShinR, -30).pair(RJ_FootL, RJ_FootR, -18);
        no3.pair(RJ_UpperArmL, RJ_UpperArmR, 50, 0, 20).pair(RJ_LowerArmL, RJ_LowerArmR, 40);
        keyed("nothing", {{0.0f, pop}, {0.12f, no1}, {0.45f, no2}, {0.76f, no3}, {1.0f, land}});
    }
    // grab poses (held while the grab trigger is)
    {
        PoseDef nh = B;
        nh.hips(0, -0.07f, 0.02f).set(RJ_Spine, 2).set(RJ_Chest, 4).set(RJ_Head, -6);
        nh.pair(RJ_UpperArmL, RJ_UpperArmR, 18, 0, 86).pair(RJ_LowerArmL, RJ_LowerArmR, 16);
        hold("no_hand", nh);
        PoseDef oh = B;
        oh.set(RJ_Spine, -8).set(RJ_UpperArmR, 26, 0, 82).set(RJ_LowerArmR, 18);
        hold("one_hand", oh);
        PoseDef cc = tuckPose();
        cc.set(RJ_ThighL, 62, 0, 38).set(RJ_ShinL, -16).set(RJ_FootL, -10).set(RJ_Pelvis, -4, -12, 0);
        hold("can_can", cc);
        PoseDef nf = B;
        nf.hips(0, -0.04f, 0.0f).set(RJ_Spine, -4).pair(RJ_ThighL, RJ_ThighR, -32, 0, 24).pair(RJ_ShinL, RJ_ShinR, -18).pair(RJ_FootL, RJ_FootR, -22);
        nf.pair(RJ_UpperArmL, RJ_UpperArmR, 58, 0, 6).pair(RJ_LowerArmL, RJ_LowerArmR, 10);
        hold("no_footer", nf);
        PoseDef gr = tuckPose();
        gr.set(RJ_Spine, -38).set(RJ_Chest, -14).set(RJ_Neck, 34).set(RJ_UpperArmR, 34, 0, 6).set(RJ_LowerArmR, 12);
        gr.pair(RJ_ThighL, RJ_ThighR, 98, 0, 8).pair(RJ_ShinL, RJ_ShinR, -128);
        hold("grab", gr);
        // tuck no hander: bars clamped between the knees, arms thrown out wide and back
        PoseDef tnh = tuckPose();
        tnh.hips(0, -0.1f, 0.0f).set(RJ_Pelvis, -4, -6, 0).set(RJ_Spine, 2).set(RJ_Chest, 6).set(RJ_Neck, -6).set(RJ_Head, -6);
        tnh.pair(RJ_ThighL, RJ_ThighR, 100, 0, 4).pair(RJ_ShinL, RJ_ShinR, -136);  // knees squeeze the stem
        tnh.pair(RJ_UpperArmL, RJ_UpperArmR, -18, 0, 78).pair(RJ_LowerArmL, RJ_LowerArmR, 12);
        hold("tuck_no_hand", tnh);
        // suicide no hander: hands let go and sweep behind the back, chest out
        PoseDef sui = B;
        sui.hips(0, -0.05f, -0.02f).set(RJ_Pelvis, 4).set(RJ_Spine, 10).set(RJ_Chest, 10).set(RJ_Neck, -6).set(RJ_Head, -6);
        sui.pair(RJ_UpperArmL, RJ_UpperArmR, -56, 0, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 14);
        hold("suicide", sui);
        // superman: both legs stretched out behind, body flat over the bars, head up
        PoseDef sup = B;
        sup.hips(0, 0.02f, 0.12f).set(RJ_Pelvis, -34).set(RJ_Spine, -22).set(RJ_Chest, -8).set(RJ_Neck, 44).set(RJ_Head, 14);
        sup.pair(RJ_ThighL, RJ_ThighR, -26, 0, 6).pair(RJ_ShinL, RJ_ShinR, -12).pair(RJ_FootL, RJ_FootR, -34);
        sup.pair(RJ_UpperArmL, RJ_UpperArmR, 70, 0, 10).pair(RJ_LowerArmL, RJ_LowerArmR, 10);
        hold("superman", sup);
        // candybar: the front leg swings up over the bars
        PoseDef cb = B;
        cb.hips(0, -0.02f, 0.06f).set(RJ_Pelvis, 8, -20, 0).set(RJ_Spine, 6).set(RJ_Chest, 4);
        cb.set(RJ_ThighL, 118, 0, 8).set(RJ_ShinL, -24).set(RJ_FootL, 10);
        hold("candybar", cb);
        // nac nac: the back leg kicks out to the side and back
        PoseDef nn = B;
        nn.hips(-0.02f, -0.04f, 0.02f).set(RJ_Pelvis, -4, -30, -8).set(RJ_Spine, -8, 10, 6);
        nn.set(RJ_ThighR, -18, 0, 52).set(RJ_ShinR, -14).set(RJ_FootR, -20);
        hold("nac_nac", nn);
        // turndown: hips turn with the deck kicked out to the side, shoulders stay square to the landing
        PoseDef td = tuckPose();
        td.set(RJ_Pelvis, -6, -62, 10).set(RJ_Spine, -16, 30, 0).set(RJ_Chest, -6, 18, 0).set(RJ_Neck, 12, 10, 0);
        td.pair(RJ_ThighL, RJ_ThighR, 70, 0, 2).pair(RJ_ShinL, RJ_ShinR, -96);
        hold("turndown", td);
        // toboggan: bars turned, the back hand reaches down and holds the deck
        PoseDef tb = tuckPose();
        tb.set(RJ_Pelvis, -8, -20, 6).set(RJ_Spine, -22, -14, 8).set(RJ_Chest, -8, -10, 4).set(RJ_Neck, 8).set(RJ_UpperArmR, -10, 0, 30).set(RJ_LowerArmR, 16);
        hold("toboggan", tb);
        // cannonball: knees to the chest, both hands down on the deck
        PoseDef cnb = tuckPose();
        cnb.hips(0, 0.02f, 0.02f).set(RJ_Spine, -42).set(RJ_Chest, -16).set(RJ_Neck, 38).set(RJ_Head, 10);
        cnb.pair(RJ_ThighL, RJ_ThighR, 112, 0, 12).pair(RJ_ShinL, RJ_ShinR, -132).pair(RJ_UpperArmL, RJ_UpperArmR, 22, 0, 8).pair(RJ_LowerArmL, RJ_LowerArmR, 14);
        hold("cannonball", cnb);
    }
    // bail: flailing arms
    {
        PoseDef a;
        a.set(RJ_Spine, 10).pair(RJ_UpperArmL, RJ_UpperArmR, 100, 0, 60).pair(RJ_LowerArmL, RJ_LowerArmR, 30).pair(RJ_ThighL, RJ_ThighR, 30).pair(RJ_ShinL, RJ_ShinR, -40);
        PoseDef b = a;
        b.pair(RJ_UpperArmL, RJ_UpperArmR, 50, 0, 110).pair(RJ_LowerArmL, RJ_LowerArmR, 60).set(RJ_ThighL, 50).set(RJ_ThighR, 10);
        c.push_back({"bail", {{0.0f, a}, {0.35f, b}, {0.7f, a}}});
    }
    return c;
}

Quat eulerDeg(const Vec3& e) { return Quat::euler(e.x * kDeg2Rad, e.y * kDeg2Rad, e.z * kDeg2Rad); }
}  // namespace sw::tools
