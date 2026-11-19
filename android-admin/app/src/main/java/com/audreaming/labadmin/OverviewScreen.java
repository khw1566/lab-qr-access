package com.audreaming.labadmin;

import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

public class OverviewScreen extends Screen {
    public OverviewScreen(MainActivity a) { super(a); }

    @Override public String title() { return "현황"; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        Ui.async(a, () -> api.get("/api/admin/overview"), o -> { if (a.isLatest(seq)) a.render(build(o)); }, a.loadError(seq));
    }

    private View build(JSONObject o) {
        LinearLayout root = page("현황", Fmt.dt(Fmt.ms(o, "server_time")) + " 기준");
        if (o.optBoolean("lockdown")) {
            View b = Ui.banner(a, "긴급 잠금이 켜져 있어 학생 QR이 모두 거부됩니다. 기능 설정에서 해제하세요.", true);
            b.setOnClickListener(v -> a.show("features"));
            Ui.add(root, b, 14);
        }

        LinearLayout r1 = Ui.hbox(a), r2 = Ui.hbox(a);
        r1.addView(stat(String.valueOf(o.optInt("pending_reservations")), null, "승인 대기 신청", o.optInt("pending_reservations") > 0, "requests"), weight());
        r1.addView(gap());
        r1.addView(stat(String.valueOf(o.optInt("pending_users")), null, "가입 대기", o.optInt("pending_users") > 0, "users"), weight());
        r2.addView(stat(String.valueOf(o.optInt("pending_special")), null, "특별 출입 대기", o.optInt("pending_special") > 0, "special"), weight());
        r2.addView(gap());
        r2.addView(stat(String.valueOf(o.optInt("active_now")), null, "지금 이용 중인 예약", false, null), weight());
        Ui.add(root, r1, 14);
        Ui.add(root, r2, 10);
        Ui.add(root, stat(String.valueOf(o.optInt("granted_today")), " / " + o.optInt("denied_today"), "오늘 출입 허용 / 거부", false, "logs"), 10);

        root.addView(Ui.groupTitle(a, "출입 단말"));
        JSONArray devs = o.optJSONArray("devices");
        if (devs == null || devs.length() == 0) {
            View e = Ui.empty(a, "등록된 출입 단말이 없습니다. 출입 단말 화면에서 등록하세요.");
            e.setOnClickListener(v -> a.show("devices"));
            root.addView(e);
        } else {
            for (int i = 0; i < devs.length(); i++) {
                JSONObject d = devs.optJSONObject(i);
                Ui.add(root, deviceCard(d), i == 0 ? 0 : 10);
            }
        }
        return root;
    }

    private View deviceCard(JSONObject d) {
        LinearLayout c = Ui.card(a);
        LinearLayout head = Ui.hbox(a);
        LinearLayout name = Ui.vbox(a);
        name.addView(Ui.bold(a, d.optString("name"), 16));
        name.addView(Ui.small(a, d.optString("room_name")));
        head.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        boolean online = d.optBoolean("online");
        head.addView(Ui.pill(a, online ? "연결됨" : "연결 끊김", online ? Ui.OK : Ui.MUTED, online ? Ui.OK_SOFT : Ui.SUNKEN));
        c.addView(head);
        long seen = Fmt.ms(d, "last_seen_at");
        Ui.add(c, Ui.small(a, seen > 0 ? "마지막 신호 " + Fmt.ago(seen) : "아직 연결된 적 없음"), 4);
        FlowLayout f = Ui.flow(a);
        int id = d.optInt("id");
        String n = d.optString("name");
        Button open = Ui.smallButton(a, "원격 열기", Ui.PRIMARY, null);
        open.setOnClickListener(v -> DeviceActions.remoteOpen(a, id, n, open));
        f.addView(open);
        Button live = Ui.smallButton(a, "실시간 보기", Ui.SECONDARY, v -> DeviceActions.liveView(a, id, n));
        if (!a.adminSettings.optBoolean("live_view", true)) live.setEnabled(false);
        f.addView(live);
        Ui.add(c, f, 10);
        return c;
    }

    private View stat(String n, String extra, String label, boolean attn, String target) {
        LinearLayout c = Ui.card(a);
        if (attn) c.setBackground(Ui.box(Ui.WARN_SOFT, 0xFFFED7AA, 10, a));
        LinearLayout nl = Ui.hbox(a);
        nl.setGravity(Gravity.BOTTOM);
        TextView big = Ui.text(a, n, 28, attn ? Ui.WARN : Ui.FG);
        big.setTypeface(Typeface.DEFAULT_BOLD);
        nl.addView(big);
        if (extra != null) nl.addView(Ui.text(a, extra, 16, Ui.MUTED));
        c.addView(nl);
        c.addView(Ui.small(a, label));
        if (target != null) c.setOnClickListener(v -> a.show(target));
        return c;
    }

    private LinearLayout.LayoutParams weight() { return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1); }

    private View gap() {
        View v = new View(a);
        v.setLayoutParams(new LinearLayout.LayoutParams(Ui.dp(a, 10), 1));
        return v;
    }
}
