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
    PoseDef p = ridePose();
    p.hips(0, -0.08f, 0.02f).set(RJ_Spine, -20).set(RJ_Chest, -8).set(RJ_Neck, 16);
    p.pair(RJ_ThighL, RJ_ThighR, 88, 0, 6).pair(RJ_ShinL, RJ_ShinR, -122).pair(RJ_FootL, RJ_FootR, 34);
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
        PoseDef a = B;
        a.hips(0, -0.12f, 0.03f).set(RJ_Spine, -16).set(RJ_Chest, -8).set(RJ_Neck, 14).pair(RJ_ThighL, RJ_ThighR, 48).pair(RJ_ShinL, RJ_ShinR, -80);
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
    // trick poses (sampled as held overlays while the trick runs)
    {
        hold("whip", tuckPose());
        PoseDef hw = tuckPose();
        hw.set(RJ_ThighR, 62, 0, 4).set(RJ_ShinR, -18).set(RJ_FootR, -5);
        hold("heelwhip", hw);
        PoseDef bs = B;
        bs.hips(0, -0.09f, 0.02f).set(RJ_Spine, -14).pair(RJ_UpperArmL, RJ_UpperArmR, 62, 0, 26).pair(RJ_LowerArmL, RJ_LowerArmR, 48);
        hold("barspin", bs);
        PoseDef fw = tuckPose();
        fw.set(RJ_Spine, -32).set(RJ_Chest, -10).set(RJ_Neck, 30).set(RJ_UpperArmR, 14, 0, 10).set(RJ_LowerArmR, 22);
        hold("fingerwhip", fw);
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
        PoseDef kl = tuckPose();
        kl.pair(RJ_UpperArmL, RJ_UpperArmR, 72, 0, 6).pair(RJ_LowerArmL, RJ_LowerArmR, 8).set(RJ_Spine, -10);
        hold("kickless", kl);
        PoseDef sf = tuckPose();
        sf.hips(0, -0.1f, 0.03f).set(RJ_Spine, -26).pair(RJ_UpperArmL, RJ_UpperArmR, 58, 0, 10).pair(RJ_LowerArmL, RJ_LowerArmR, 30);
        hold("scooter_flip", sf);
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
