package com.audreaming.labadmin;

import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

public class DevicesScreen extends Screen {
    private String newKey; // 방금 발급한 단말 키 (한 번만 보여 줌)

    public DevicesScreen(MainActivity a) { super(a); }

    @Override public String title() { return "출입 단말"; }

    @Override public boolean autoRefresh() { return newKey == null; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        Ui.async(a, () -> {
            JSONObject live;
            try { live = api.get("/api/admin/live-sessions"); } catch (Api.ApiException e) { live = new JSONObject(); }
            return new JSONObject[]{api.get("/api/admin/devices"), api.get("/api/rooms?all=1"), live};
        }, r -> { if (a.isLatest(seq)) a.render(build(r[0], r[1].optJSONArray("rooms"), r[2].optJSONArray("sessions"))); }, a.loadError(seq));
    }

    private View build(JSONObject o, JSONArray rooms, JSONArray lives) {
        LinearLayout root = page("출입 단말", "출입문 보드(JC1060P470)를 등록하고 단말 키를 발급합니다. 보드는 1분마다 신호를 보내고, 원격 열기 요청은 바로 받습니다.");
        long now = Fmt.ms(o, "server_time");
        JSONArray devs = o.optJSONArray("devices");
        if (devs == null || devs.length() == 0) Ui.add(root, Ui.empty(a, "등록된 단말이 없습니다."), 14);
        else for (int i = 0; i < devs.length(); i++) Ui.add(root, card(devs.optJSONObject(i), now, rooms), i == 0 ? 14 : 10);

        // 새 단말 등록
        LinearLayout form = Ui.card(a);
        form.addView(Ui.bold(a, "새 단말 등록", 16));
        EditText name = Ui.input(a, "실습실 출입문", "");
        List<String> names = new ArrayList<>();
        for (int i = 0; rooms != null && i < rooms.length(); i++) names.add(rooms.optJSONObject(i).optString("name"));
        Spinner room = Ui.spinner(a, names, 0);
        Ui.add(form, Ui.field(a, "단말 이름", name), 12);
        Ui.add(form, Ui.field(a, "실습실", room), 10);
        Ui.add(form, Ui.wrapLeft(Ui.smallButton(a, "등록하고 키 받기", Ui.PRIMARY, v -> {
            if (rooms == null || rooms.length() == 0) { Ui.toast(a, "먼저 실습실을 추가하세요."); return; }
            int rid = rooms.optJSONObject(room.getSelectedItemPosition()).optInt("id");
            Ui.async(a, () -> api.post("/api/admin/devices", new JSONObject().put("name", name.getText().toString()).put("room_id", rid)), r -> {
                newKey = r.optString("key");
                a.dataChanged();
                reload();
            });
        })), 12);
        if (newKey != null) {
            LinearLayout kb = Ui.vbox(a);
            kb.addView(Ui.text(a, "단말 키는 지금 한 번만 보입니다. menuconfig의 Lab Door Config > Device key에 넣으세요. 서버 주소는 " + api.base() + " 입니다.", 14, Ui.FG));
            TextView key = Ui.mono(a, newKey, 14, Ui.FG);
            key.setTextIsSelectable(true);
            key.setBackground(Ui.box(Ui.SUNKEN, Ui.LINE, 8, a));
            int p = Ui.dp(a, 10);
            key.setPadding(p, p, p, p);
            Ui.add(kb, key, 8);
            String k = newKey;
            LinearLayout btns = Ui.hbox(a);
            btns.addView(Ui.smallButton(a, "키 복사", Ui.SECONDARY, v -> {
                ClipboardManager cm = (ClipboardManager) a.getSystemService(Context.CLIPBOARD_SERVICE);
                cm.setPrimaryClip(ClipData.newPlainText("device key", k));
                Ui.toast(a, "복사했습니다.");
            }));
            View g = new View(a);
            btns.addView(g, new LinearLayout.LayoutParams(Ui.dp(a, 8), 1));
            btns.addView(Ui.smallButton(a, "다 넣었어요 (키 숨기기)", Ui.QUIET, v -> { newKey = null; reload(); }));
            Ui.add(kb, btns, 8);
            Ui.add(form, kb, 14);
        }
        Ui.add(root, form, 20);

        root.addView(Ui.groupTitle(a, "실시간 보기 기록"));
        root.addView(Ui.small(a, "영상과 음성은 저장하지 않고, 누가 언제 연결했는지만 남깁니다 (최근 20건)."));
        if (lives == null || lives.length() == 0) Ui.add(root, Ui.empty(a, "아직 기록이 없습니다."), 8);
        else {
            LinearLayout list = Ui.card(a);
            for (int i = 0; i < lives.length(); i++) {
                JSONObject l = lives.optJSONObject(i);
                if (i > 0) list.addView(Ui.divider(a));
                long st = Fmt.ms(l, "started_at"), en = Fmt.ms(l, "ended_at");
                String dur = en > 0 ? Math.max(1, Math.round((en - st) / 60000.0)) + "분" : "연결 중";
                List<String> used = new ArrayList<>();
                if (l.optInt("mic_used") == 1) used.add("소리 듣기");
                if (l.optInt("talk_used") == 1) used.add("말하기");
                LinearLayout row = Ui.vbox(a);
                row.setPadding(0, Ui.dp(a, 8), 0, Ui.dp(a, 8));
                row.addView(Ui.text(a, Fmt.dt(st) + " · " + l.optString("device_name"), 14, Ui.FG));
                row.addView(Ui.small(a, l.optString("admin_name") + " · " + dur + " · " + (used.isEmpty() ? "영상만" : String.join(", ", used))));
                list.addView(row);
            }
            Ui.add(root, list, 8);
        }
        return root;
    }

    private View card(JSONObject d, long now, JSONArray rooms) {
        LinearLayout c = Ui.card(a);
        boolean active = d.optInt("active") == 1;
        long seen = Fmt.ms(d, "last_seen_at");
        boolean online = active && seen > 0 && now - seen < 180_000;
        LinearLayout head = Ui.hbox(a);
        LinearLayout nm = Ui.vbox(a);
        nm.addView(Ui.bold(a, d.optString("name"), 16));
        nm.addView(Ui.small(a, d.optString("room_name")));
        head.addView(nm, new LinearLayout.LayoutParams(0, -2, 1));
        head.addView(!active ? Ui.pill(a, "키 폐기", Ui.BAD, Ui.BAD_SOFT)
                : Ui.pill(a, online ? "연결됨" : "연결 끊김", online ? Ui.OK : Ui.MUTED, online ? Ui.OK_SOFT : Ui.SUNKEN));
        c.addView(head);
        Ui.add(c, Ui.small(a, seen > 0 ? "마지막 신호 " + Fmt.ago(seen) : "연결된 적 없음"), 4);
        int id = d.optInt("id");
        String n = d.optString("name");
        FlowLayout f = Ui.flow(a);
        if (active) {
            Button open = Ui.smallButton(a, "원격 열기", Ui.PRIMARY, null);
            open.setOnClickListener(v -> DeviceActions.remoteOpen(a, id, n, open));
            f.addView(open);
            Button live = Ui.smallButton(a, "실시간 보기", Ui.SECONDARY, v -> DeviceActions.liveView(a, id, n));
            if (!a.adminSettings.optBoolean("live_view", true)) live.setEnabled(false);
            f.addView(live);
        }
        f.addView(Ui.smallButton(a, "정보 수정", Ui.SECONDARY, v -> edit(d, rooms)));
        if (active) f.addView(Ui.smallButton(a, "키 폐기", Ui.SECONDARY, v -> Ui.confirm(a, "단말 키를 폐기할까요?",
                "이 보드는 더 이상 서버에 접속할 수 없습니다. 다시 쓰려면 새로 등록해 키를 넣어야 합니다.", "키 폐기", true,
                () -> act(() -> api.post("/api/admin/devices/" + id + "/revoke", null), "폐기했습니다."))));
        Ui.add(c, f, 10);
        return c;
    }

    private void edit(JSONObject d, JSONArray rooms) {
        LinearLayout l = Ui.vbox(a);
        int p = Ui.dp(a, 20);
        l.setPadding(p, Ui.dp(a, 8), p, 0);
        EditText name = Ui.input(a, d.optString("name"), "");
        List<String> names = new ArrayList<>();
        int sel = 0;
        for (int i = 0; rooms != null && i < rooms.length(); i++) {
            names.add(rooms.optJSONObject(i).optString("name"));
            if (rooms.optJSONObject(i).optInt("id") == d.optInt("room_id")) sel = i;
        }
        Spinner room = Ui.spinner(a, names, sel);
        Ui.add(l, Ui.field(a, "단말 이름", name), 0);
        Ui.add(l, Ui.field(a, "소속 실습실", room), 10);
        AlertDialog dlg = new AlertDialog.Builder(a).setTitle("단말 정보 수정").setView(l)
                .setNegativeButton("취소", null).setPositiveButton("저장", null).create();
        dlg.show();
        dlg.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> Ui.async(a, () -> {
            JSONObject b = new JSONObject().put("name", name.getText().toString().trim());
            if (rooms != null && rooms.length() > 0) b.put("room_id", rooms.optJSONObject(room.getSelectedItemPosition()).optInt("id"));
            return api.patch("/api/admin/devices/" + d.optInt("id"), b);
        }, r -> { dlg.dismiss(); Ui.toast(a, "저장했습니다."); a.dataChanged(); reload(); }));
    }
}
