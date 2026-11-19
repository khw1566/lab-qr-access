package com.audreaming.labadmin;

import android.content.Intent;
import android.view.View;
import android.widget.AdapterView;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

public class MasterScreen extends Screen {
    private static final int[][] DYN_HOURS = {{1}, {8}, {24}, {24 * 7}, {24 * 30}};
    private static final String[] DYN_LABELS = {"1시간", "8시간", "1일", "7일", "30일"};
    private static final int[][] STATIC_HOURS = {{24 * 7}, {24 * 30}, {24 * 90}, {24 * 180}, {24 * 365}};
    private static final String[] STATIC_LABELS = {"7일", "30일", "90일", "180일", "1년"};

    public MasterScreen(MainActivity a) { super(a); }

    @Override public String title() { return "마스터 QR"; }

    @Override public boolean autoRefresh() { return false; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        Ui.async(a, () -> new JSONObject[]{api.get("/api/admin/masters"), api.get("/api/rooms?all=1")},
                r -> { if (a.isLatest(seq)) a.render(build(r[0], r[1].optJSONArray("rooms"))); }, a.loadError(seq));
    }

    private View build(JSONObject o, JSONArray rooms) {
        LinearLayout root = page("마스터 QR", "예약 없이 출입문을 여는 관리자용 QR입니다. 출입 단말 펌웨어 변경 없이 바로 쓸 수 있습니다.");
        if (!a.adminSettings.optBoolean("master_qr_enabled", true))
            Ui.add(root, Ui.banner(a, "기능 설정에서 마스터 QR이 꺼져 있어 지금은 문이 열리지 않습니다.", true), 12);

        // 새로 발급
        LinearLayout form = Ui.card(a);
        form.addView(Ui.bold(a, "새 마스터 QR 발급", 16));
        EditText label = Ui.input(a, "", "예: 조교 김OO");
        List<String> roomNames = new ArrayList<>();
        List<Integer> roomIds = new ArrayList<>();
        roomNames.add("모든 실습실");
        roomIds.add(0);
        for (int i = 0; rooms != null && i < rooms.length(); i++) {
            roomNames.add(rooms.optJSONObject(i).optString("name"));
            roomIds.add(rooms.optJSONObject(i).optInt("id"));
        }
        Spinner room = Ui.spinner(a, roomNames, 0);
        Spinner mode = Ui.spinner(a, MainActivity.labels("화면용 5분 QR (권장)", "인쇄용 고정 QR"), 0);
        Spinner hours = Ui.spinner(a, MainActivity.labels(DYN_LABELS), 2);
        TextView hint = Ui.small(a, "");
        Runnable sync = () -> {
            boolean st = mode.getSelectedItemPosition() == 1;
            Spinner fresh = Ui.spinner(a, MainActivity.labels(st ? STATIC_LABELS : DYN_LABELS), st ? 1 : 2);
            hours.setAdapter(fresh.getAdapter());
            hours.setSelection(st ? 1 : 2);
            hint.setText(st ? "인쇄용 고정 QR은 사진을 찍으면 누구나 쓸 수 있습니다. 비상용으로 금고 등에 보관하고, 분실하면 바로 재발급하거나 폐기하세요."
                    : "관리자 폰 화면에 띄워 쓰는 QR입니다. 5분 동안 한 번만 쓸 수 있어 캡처본은 통과하지 않습니다.");
        };
        mode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> p, View v, int pos, long id) { sync.run(); }
            public void onNothingSelected(AdapterView<?> p) { }
        });
        sync.run();
        Ui.add(form, Ui.field(a, "이름 (사용자/용도)", label), 12);
        Ui.add(form, Ui.field(a, "실습실", room), 10);
        Ui.add(form, Ui.field(a, "종류", mode), 10);
        Ui.add(form, Ui.field(a, "유효 기간", hours), 10);
        Ui.add(form, hint, 8);
        Ui.add(form, Ui.wrapLeft(Ui.smallButton(a, "발급하기", Ui.PRIMARY, v -> {
            boolean st = mode.getSelectedItemPosition() == 1;
            int h = (st ? STATIC_HOURS : DYN_HOURS)[hours.getSelectedItemPosition()][0];
            int rid = roomIds.get(room.getSelectedItemPosition());
            Ui.async(a, () -> {
                JSONObject b = new JSONObject().put("label", label.getText().toString()).put("mode", st ? "static" : "dynamic").put("valid_hours", h);
                b.put("room_id", rid == 0 ? JSONObject.NULL : rid);
                return api.post("/api/admin/masters", b);
            }, r -> {
                Ui.toast(a, "마스터 QR을 발급했습니다.");
                a.dataChanged();
                reload();
                String sub = roomNames.get(room.getSelectedItemPosition());
                openQr(r.optInt("id"), label.getText().toString().trim(), st, sub);
            });
        })), 12);
        Ui.add(root, form, 14);

        JSONArray ms = o.optJSONArray("masters");
        long now = Fmt.ms(o, "server_time");
        root.addView(Ui.groupTitle(a, "발급한 마스터 QR"));
        if (ms == null || ms.length() == 0) root.addView(Ui.empty(a, "발급한 마스터 QR이 없습니다."));
        else for (int i = 0; i < ms.length(); i++) Ui.add(root, card(ms.optJSONObject(i), now), i == 0 ? 0 : 10);
        return root;
    }

    private View card(JSONObject m, long now) {
        LinearLayout c = Ui.card(a);
        boolean revoked = m.optInt("revoked") == 1, expired = Fmt.ms(m, "expires_at") < now, dead = revoked || expired;
        boolean st = "static".equals(m.optString("mode"));
        if (!dead) c.setBackground(Ui.box(0xFFFFFBEB, Ui.MASTER, 10, a));
        LinearLayout head = Ui.hbox(a);
        head.addView(Ui.bold(a, m.optString("label"), 16), new LinearLayout.LayoutParams(0, -2, 1));
        head.addView(revoked ? Ui.pill(a, "폐기", Ui.BAD, Ui.BAD_SOFT) : expired ? Ui.pill(a, "기간 끝", Ui.MUTED, Ui.SUNKEN)
                : Ui.pill(a, st ? "인쇄용 고정" : "화면용 5분", Ui.MASTER_INK, Ui.MASTER));
        c.addView(head);
        String room = Fmt.str(m, "room_name").isEmpty() ? "모든 실습실" : m.optString("room_name");
        String sub = room + " · " + Fmt.dt(Fmt.ms(m, "expires_at")) + "까지";
        c.addView(Ui.small(a, sub));
        long last = Fmt.ms(m, "last_used_at");
        c.addView(Ui.small(a, "사용 " + m.optInt("use_count") + "회" + (last > 0 ? " · 마지막 " + Fmt.ago(last) : "")
                + " · 발급 " + (Fmt.str(m, "created_by_name").isEmpty() ? "-" : m.optString("created_by_name"))));
        if (dead) { c.setAlpha(0.6f); return c; }
        int id = m.optInt("id");
        FlowLayout f = Ui.flow(a);
        f.addView(Ui.smallButton(a, "QR 보기", Ui.PRIMARY, v -> openQr(id, m.optString("label"), st, sub)));
        if (st) f.addView(Ui.smallButton(a, "재발급", Ui.SECONDARY, v -> Ui.confirm(a, "QR을 재발급할까요?",
                "예전에 인쇄한 QR은 즉시 무효가 되고 새 QR이 만들어집니다.", "재발급", false,
                () -> Ui.async(a, () -> api.post("/api/admin/masters/" + id + "/reissue", null), r -> {
                    Ui.toast(a, "재발급했습니다.");
                    a.dataChanged();
                    reload();
                    openQr(id, m.optString("label"), true, sub);
                }))));
        f.addView(Ui.smallButton(a, "폐기", Ui.SECONDARY, v -> Ui.confirm(a, "이 마스터 QR을 폐기할까요?",
                "폐기하면 이 QR로는 더 이상 문이 열리지 않습니다. 되돌릴 수 없습니다.", "폐기", true,
                () -> act(() -> api.post("/api/admin/masters/" + id + "/revoke", null), "폐기했습니다."))));
        Ui.add(c, f, 12);
        return c;
    }

    private void openQr(int id, String label, boolean isStatic, String sub) {
        Intent i = new Intent(a, QrActivity.class);
        i.putExtra("id", id);
        i.putExtra("label", label);
        i.putExtra("static", isStatic);
        i.putExtra("sub", sub);
        a.startActivity(i);
    }
}
