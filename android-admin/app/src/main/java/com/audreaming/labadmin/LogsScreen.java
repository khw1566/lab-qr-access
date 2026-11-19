package com.audreaming.labadmin;

import android.app.DatePickerDialog;
import android.net.Uri;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;
import java.util.Locale;

public class LogsScreen extends Screen {
    private String from = "", to = "", result = "", q = "", order = "desc";
    private int roomId = 0, deviceId = 0, page = 1;

    public LogsScreen(MainActivity a) { super(a); }

    @Override public String title() { return "출입 기록"; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        StringBuilder sb = new StringBuilder("/api/admin/logs?order=").append(order).append("&page=").append(page);
        if (!from.isEmpty()) sb.append("&from=").append(from);
        if (!to.isEmpty()) sb.append("&to=").append(to);
        if (!result.isEmpty()) sb.append("&result=").append(result);
        if (roomId > 0) sb.append("&room_id=").append(roomId);
        if (deviceId > 0) sb.append("&device_id=").append(deviceId);
        if (!q.isEmpty()) sb.append("&q=").append(Uri.encode(q));
        String path = sb.toString();
        Ui.async(a, () -> new JSONObject[]{api.get(path), api.get("/api/rooms?all=1"), api.get("/api/admin/devices")},
                r -> { if (a.isLatest(seq)) a.render(build(r[0], r[1].optJSONArray("rooms"), r[2].optJSONArray("devices"))); }, a.loadError(seq));
    }

    private View build(JSONObject o, JSONArray rooms, JSONArray devices) {
        page = o.optInt("page", 1);
        int pages = o.optInt("pages", 1), total = o.optInt("total"), size = o.optInt("page_size", 50);
        LinearLayout root = page("출입 기록", "한국 시간 기준으로 조회합니다. 최신순이 기본이며, 과거 기록도 페이지를 넘겨 볼 수 있습니다.");

        // 조회 조건
        LinearLayout form = Ui.card(a);
        String[] dates = {from, to};
        Button bf = dateButton(dates, 0), bt = dateButton(dates, 1);
        Ui.add(form, pair(Ui.field(a, "시작일", bf), Ui.field(a, "종료일", bt)), 0);
        String[] resVals = {"", "granted", "denied"};
        Spinner res = Ui.spinner(a, MainActivity.labels("전체 결과", "허용", "거부"), indexOf(resVals, result));
        Spinner ord = Ui.spinner(a, MainActivity.labels("최신순", "오래된순"), "asc".equals(order) ? 1 : 0);
        Ui.add(form, pair(Ui.field(a, "출입 결과", res), Ui.field(a, "시각 정렬", ord)), 10);
        List<String> rn = new ArrayList<>();
        List<Integer> rid = new ArrayList<>();
        rn.add("전체 실습실");
        rid.add(0);
        for (int i = 0; rooms != null && i < rooms.length(); i++) { rn.add(rooms.optJSONObject(i).optString("name")); rid.add(rooms.optJSONObject(i).optInt("id")); }
        List<String> dn = new ArrayList<>();
        List<Integer> did = new ArrayList<>();
        dn.add("전체 단말");
        did.add(0);
        for (int i = 0; devices != null && i < devices.length(); i++) {
            JSONObject d = devices.optJSONObject(i);
            dn.add(d.optString("name") + (d.optInt("active") == 1 ? "" : " (키 폐기)"));
            did.add(d.optInt("id"));
        }
        Spinner room = Ui.spinner(a, rn, Math.max(0, rid.indexOf(roomId)));
        Spinner dev = Ui.spinner(a, dn, Math.max(0, did.indexOf(deviceId)));
        Ui.add(form, pair(Ui.field(a, "실습실", room), Ui.field(a, "단말", dev)), 10);
        EditText qq = Ui.input(a, q, "검색어 입력");
        Ui.add(form, Ui.field(a, "이름·학번·마스터 QR 이름", qq), 10);
        LinearLayout btns = Ui.hbox(a);
        btns.addView(Ui.smallButton(a, "조회", Ui.PRIMARY, v -> {
            if (!dates[0].isEmpty() && !dates[1].isEmpty() && dates[0].compareTo(dates[1]) > 0) { Ui.toast(a, "시작일은 종료일보다 늦을 수 없습니다."); return; }
            from = dates[0];
            to = dates[1];
            result = resVals[res.getSelectedItemPosition()];
            order = ord.getSelectedItemPosition() == 1 ? "asc" : "desc";
            roomId = rid.get(room.getSelectedItemPosition());
            deviceId = did.get(dev.getSelectedItemPosition());
            q = qq.getText().toString().trim();
            page = 1;
            a.show("logs");
        }));
        View g = new View(a);
        btns.addView(g, new LinearLayout.LayoutParams(Ui.dp(a, 8), 1));
        btns.addView(Ui.smallButton(a, "초기화", Ui.SECONDARY, v -> {
            from = to = result = q = "";
            order = "desc";
            roomId = deviceId = 0;
            page = 1;
            a.show("logs");
        }));
        Ui.add(form, btns, 12);
        Ui.add(root, form, 12);

        String range = total > 0 ? " · " + ((page - 1) * size + 1) + "–" + Math.min(page * size, total) + "건" : "";
        Ui.add(root, pager("총 " + total + "건" + range, pages), 12);

        JSONArray logs = o.optJSONArray("logs");
        if (logs == null || logs.length() == 0) Ui.add(root, Ui.empty(a, "조건에 맞는 출입 기록이 없습니다."), 10);
        else {
            LinearLayout list = Ui.card(a);
            for (int i = 0; i < logs.length(); i++) {
                if (i > 0) list.addView(Ui.divider(a));
                list.addView(row(logs.optJSONObject(i)));
            }
            Ui.add(root, list, 10);
            Ui.add(root, pager("", pages), 12);
        }
        return root;
    }

    private View row(JSONObject l) {
        LinearLayout r = Ui.vbox(a);
        r.setPadding(0, Ui.dp(a, 9), 0, Ui.dp(a, 9));
        LinearLayout top = Ui.hbox(a);
        boolean granted = "granted".equals(l.optString("result"));
        top.addView(Ui.pill(a, granted ? "허용" : "거부", granted ? Ui.OK : Ui.BAD, granted ? Ui.OK_SOFT : Ui.BAD_SOFT));
        TextView when = Ui.mono(a, "  " + Fmt.full(Fmt.ms(l, "at")), 13, Ui.MUTED);
        top.addView(when);
        r.addView(top);
        String who;
        if (!l.isNull("master_id")) who = "[마스터] " + Fmt.str(l, "master_label");
        else if (!Fmt.str(l, "name").isEmpty()) who = l.optString("name") + "  " + Fmt.str(l, "student_no");
        else who = "-";
        if (!l.isNull("special_id")) who = "[특별 출입] " + who;
        Ui.add(r, Ui.bold(a, who, 15), 4);
        String reason = Fmt.str(l, "reason");
        String line = (Fmt.str(l, "room_name").isEmpty() ? "-" : l.optString("room_name")) + " · " + (Fmt.str(l, "device_name").isEmpty() ? "-" : l.optString("device_name"));
        if (!reason.isEmpty() && !"master".equals(reason)) line = (Fmt.REASON.containsKey(reason) ? Fmt.REASON.get(reason) : reason) + " · " + line;
        r.addView(Ui.small(a, line));
        return r;
    }

    private View pager(String label, int pages) {
        LinearLayout p = Ui.hbox(a);
        p.addView(Ui.small(a, label), new LinearLayout.LayoutParams(0, -2, 1));
        Button prev = Ui.smallButton(a, "이전", Ui.SECONDARY, v -> { page--; a.show("logs"); });
        prev.setEnabled(page > 1);
        p.addView(prev);
        p.addView(Ui.small(a, "  " + page + " / " + pages + "  "));
        Button next = Ui.smallButton(a, "다음", Ui.SECONDARY, v -> { page++; a.show("logs"); });
        next.setEnabled(page < pages);
        p.addView(next);
        return p;
    }

    private Button dateButton(String[] dates, int i) {
        Button b = Ui.button(a, dates[i].isEmpty() ? "선택 안 함" : dates[i], Ui.SECONDARY);
        b.setOnClickListener(v -> {
            Calendar c = Fmt.cal(System.currentTimeMillis());
            if (!dates[i].isEmpty()) {
                String[] p = dates[i].split("-");
                c.set(Integer.parseInt(p[0]), Integer.parseInt(p[1]) - 1, Integer.parseInt(p[2]));
            }
            DatePickerDialog d = new DatePickerDialog(a, (view, y, m, day) -> {
                dates[i] = String.format(Locale.US, "%04d-%02d-%02d", y, m + 1, day);
                b.setText(dates[i]);
            }, c.get(Calendar.YEAR), c.get(Calendar.MONTH), c.get(Calendar.DAY_OF_MONTH));
            d.setButton(DatePickerDialog.BUTTON_NEUTRAL, "지우기", (x, y) -> { dates[i] = ""; b.setText("선택 안 함"); });
            d.show();
        });
        return b;
    }

    private static int indexOf(String[] a, String v) {
        for (int i = 0; i < a.length; i++) if (a[i].equals(v)) return i;
        return 0;
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
