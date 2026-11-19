package com.audreaming.labadmin;

import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class EquipmentScreen extends Screen {
    public EquipmentScreen(MainActivity a) { super(a); }

    @Override public String title() { return "장비 상태"; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        Ui.async(a, () -> new JSONObject[]{api.get("/api/admin/settings").getJSONObject("settings"), api.get("/api/config")},
                r -> {
                    if (!a.isLatest(seq)) return;
                    a.adminSettings = r[0];
                    a.config = r[1];
                    a.render(build(r[0], r[1]));
                }, a.loadError(seq));
    }

    private View build(JSONObject s, JSONObject config) {
        LinearLayout root = page("장비 상태", "장비를 누르면 사용 가능 ↔ 점검 중이 바뀝니다. 첫 화면 배치도와 신청 화면에 바로 반영됩니다.");
        Set<String> down = new LinkedHashSet<>();
        JSONArray d = s.optJSONArray("printer_down");
        for (int i = 0; d != null && i < d.length(); i++) down.add(d.optString(i));

        // 행(A~F, Z, N)별로 묶는다
        Map<String, List<JSONObject>> rows = new LinkedHashMap<>();
        JSONArray ps = config.optJSONArray("printers");
        for (int i = 0; ps != null && i < ps.length(); i++) {
            JSONObject p = ps.optJSONObject(i);
            rows.computeIfAbsent(p.optString("row"), k -> new ArrayList<>()).add(p);
        }
        int okRep = 0, allRep = 0, okZ = 0, allZ = 0;
        for (List<JSONObject> l : rows.values()) for (JSONObject p : l) {
            boolean up = !down.contains(p.optString("id"));
            if ("replicator".equals(p.optString("type"))) { allRep++; if (up) okRep++; }
            if ("z18".equals(p.optString("type"))) { allZ++; if (up) okZ++; }
        }
        Ui.add(root, Ui.text(a, "Replicator+ " + okRep + "/" + allRep + "대 사용 가능 · Z18 " + okZ + "/" + allZ + "대", 14, Ui.FG), 10);

        for (Map.Entry<String, List<JSONObject>> e : rows.entrySet()) {
            String row = e.getKey();
            String name = "Z".equals(row) ? "복도 쪽 선반 (Z18)" : "N".equals(row) ? "하단 Creality Ender 3 (별도 관리)" : row + "열 · Replicator+";
            root.addView(Ui.groupTitle(a, name));
            FlowLayout f = Ui.flow(a);
            for (JSONObject p : e.getValue()) f.addView(tile(p, down));
            root.addView(f);
        }

        LinearLayout legend = Ui.hbox(a);
        legend.addView(Ui.pill(a, "사용 가능", Ui.OK, Ui.OK_SOFT));
        legend.addView(space());
        legend.addView(Ui.pill(a, "점검 중", Ui.BAD, Ui.BAD_SOFT));
        legend.addView(space());
        legend.addView(Ui.pill(a, "별도 관리 (신청 불가)", Ui.MUTED, Ui.SUNKEN));
        Ui.add(root, legend, 20);
        return root;
    }

    private View tile(JSONObject p, Set<String> down) {
        String id = p.optString("id");
        boolean bookable = p.optBoolean("bookable");
        boolean isDown = down.contains(id);
        LinearLayout t = Ui.vbox(a);
        t.setGravity(Gravity.CENTER);
        int fill = !bookable ? Ui.SUNKEN : isDown ? Ui.BAD_SOFT : Ui.OK_SOFT;
        int stroke = !bookable ? Ui.LINE : isDown ? 0xFFFCA5A5 : 0xFF86EFAC;
        int ink = !bookable ? Ui.MUTED : isDown ? Ui.BAD : Ui.OK;
        t.setBackground(Ui.box(fill, stroke, 10, a));
        t.setMinimumWidth(Ui.dp(a, 76));
        int pad = Ui.dp(a, 10);
        t.setPadding(pad, pad, pad, pad);
        TextView n = Ui.text(a, id, 18, ink);
        n.setTypeface(Typeface.DEFAULT_BOLD);
        n.setGravity(Gravity.CENTER);
        t.addView(n);
        TextView st = Ui.text(a, !bookable ? "별도 관리" : isDown ? "점검 중" : "사용 가능", 12, ink);
        st.setGravity(Gravity.CENTER);
        t.addView(st);
        if (bookable) t.setOnClickListener(v -> toggle(id, down));
        return t;
    }

    private void toggle(String id, Set<String> down) {
        Set<String> next = new LinkedHashSet<>(down);
        boolean nowDown = !next.remove(id);
        if (nowDown) next.add(id);
        Ui.async(a, () -> api.patch("/api/admin/settings", new JSONObject().put("printer_down", new JSONArray(next))), r -> {
            Ui.toast(a, id + " " + (nowDown ? "점검 중" : "사용 가능") + "으로 바꿨습니다.");
            a.updateSettings(r.optJSONObject("settings"));
            a.dataChanged();
            reload();
        });
    }

    private View space() {
        View v = new View(a);
        v.setLayoutParams(new LinearLayout.LayoutParams(Ui.dp(a, 8), 1));
        return v;
    }
}
