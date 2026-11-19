package com.audreaming.labadmin;

import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Switch;

import org.json.JSONArray;
import org.json.JSONObject;

public class RoomsScreen extends Screen {
    public RoomsScreen(MainActivity a) { super(a); }

    @Override public String title() { return "실습실·자동 승인"; }

    @Override public boolean autoRefresh() { return false; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        Ui.async(a, () -> api.get("/api/rooms?all=1"), o -> { if (a.isLatest(seq)) a.render(build(o.optJSONArray("rooms"))); }, a.loadError(seq));
    }

    private View build(JSONArray rooms) {
        LinearLayout root = page("실습실·자동 승인", "실습실별 운영 여부와 자동 승인 기준을 정합니다.");
        int n = rooms == null ? 0 : rooms.length();
        Ui.add(root, Ui.wrapLeft(Ui.smallButton(a, "실습실 추가", Ui.SECONDARY, v -> Ui.prompt(a, "실습실 추가", null, "실습실 이름",
                "실습실 " + (n + 1), false, "추가", false, name -> {
                    if (name.isEmpty()) return;
                    try { JSONObject b = new JSONObject().put("name", name); act(() -> api.post("/api/admin/rooms", b), "추가했습니다."); }
                    catch (Exception ignored) { }
                }))), 10);
        for (int i = 0; i < n; i++) Ui.add(root, card(rooms.optJSONObject(i)), 12);
        return root;
    }

    private View card(JSONObject r) {
        LinearLayout c = Ui.card(a);
        EditText name = Ui.input(a, r.optString("name"), "");
        Switch enabled = Ui.toggle(a, r.optInt("enabled") == 1);
        Ui.add(c, Ui.field(a, "실습실 이름", name), 0);
        c.addView(Ui.settingRow(a, "운영 중", null, enabled, false));
        c.addView(Ui.divider(a));
        Switch auto = Ui.toggle(a, r.optInt("auto_approve") == 1);
        c.addView(Ui.settingRow(a, "자동 승인", "아래 기준에 맞는 신청은 관리자 확인 없이 바로 승인합니다.", auto, false));
        EditText open = Ui.number(a, r.optInt("open_hour")), close = Ui.number(a, r.optInt("close_hour"));
        EditText max = Ui.number(a, r.optInt("max_minutes")), cap = Ui.number(a, r.optInt("capacity")), early = Ui.number(a, r.optInt("early_minutes"));
        Ui.add(c, pair(Ui.field(a, "허용 시작 (시)", open), Ui.field(a, "허용 종료 (시)", close)), 6);
        Ui.add(c, pair(Ui.field(a, "1회 최대 (분)", max), Ui.field(a, "동시 정원 (명)", cap)), 10);
        Ui.add(c, pair(Ui.field(a, "조기 입장 (분 전)", early), new View(a)), 10);
        int id = r.optInt("id");
        Ui.add(c, Ui.wrapLeft(Ui.smallButton(a, "저장", Ui.PRIMARY, v -> Ui.async(a, () -> {
            JSONObject b = new JSONObject()
                    .put("name", name.getText().toString())
                    .put("enabled", enabled.isChecked() ? 1 : 0)
                    .put("auto_approve", auto.isChecked() ? 1 : 0)
                    .put("open_hour", num(open)).put("close_hour", num(close))
                    .put("max_minutes", num(max)).put("capacity", num(cap)).put("early_minutes", num(early));
            return api.patch("/api/admin/rooms/" + id, b);
        }, x -> { Ui.toast(a, "저장했습니다."); a.dataChanged(); }))), 14);
        return c;
    }

    private static Object num(EditText e) {
        String s = e.getText().toString().trim();
        try { return Integer.parseInt(s); } catch (NumberFormatException x) { return s; }
    }

    private View pair(View x, View y) {
        LinearLayout r = Ui.hbox(a);
        r.addView(x, new LinearLayout.LayoutParams(0, -2, 1));
        View g = new View(a);
        r.addView(g, new LinearLayout.LayoutParams(Ui.dp(a, 10), 1));
        r.addView(y, new LinearLayout.LayoutParams(0, -2, 1));
        return r;
    }
}
