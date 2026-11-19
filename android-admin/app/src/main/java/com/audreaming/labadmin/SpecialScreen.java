package com.audreaming.labadmin;

import android.view.View;
import android.widget.LinearLayout;

import org.json.JSONArray;
import org.json.JSONObject;

public class SpecialScreen extends Screen {
    private String filter = "pending";

    public SpecialScreen(MainActivity a) { super(a); }

    @Override public String title() { return "특별 출입신청"; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        String f = filter;
        Ui.async(a, () -> api.get("/api/admin/special-requests?status=" + f), o -> { if (a.isLatest(seq)) a.render(build(o)); }, a.loadError(seq));
    }

    static long end(JSONObject r) { return !r.isNull("expires_at") ? r.optLong("expires_at") : Fmt.ms(r, "activation_deadline"); }

    static String state(JSONObject r, long now) {
        switch (r.optString("status")) {
            case "approved": return now >= end(r) ? "만료" : !r.isNull("first_used_at") ? "이용 중" : "첫 출입 대기";
            case "pending": return "승인 대기";
            case "rejected": return "반려";
            case "cancelled": return "신청 취소";
            case "revoked": return "승인 취소";
            default: return r.optString("status");
        }
    }

    static String time(JSONObject r) {
        if (!"approved".equals(r.optString("status"))) return Fmt.dt(Fmt.ms(r, "created_at")) + " 신청";
        if (!r.isNull("first_used_at")) return "첫 출입 " + Fmt.dt(r.optLong("first_used_at")) + " · 종료 " + Fmt.dt(Fmt.ms(r, "expires_at"));
        return Fmt.dt(Fmt.ms(r, "activation_deadline")) + "까지 첫 출입 · 첫 출입부터 30분";
    }

    private View build(JSONObject o) {
        LinearLayout root = page("특별 출입신청", "승인하면 학생에게 전용 출입 QR이 발급됩니다. 승인 후 24시간 안에 첫 출입해야 하며, 첫 출입부터 30분 동안 재출입할 수 있습니다.");
        Ui.add(root, segment(new String[][]{{"pending", "승인 대기"}, {"approved", "발급됨"}, {"rejected", "반려"}, {"cancelled", "취소"}, {"revoked", "승인 취소"}},
                filter, k -> { filter = k; a.show("special"); }), 12);
        long now = Fmt.ms(o, "server_time");
        JSONArray list = o.optJSONArray("requests");
        if (list == null || list.length() == 0) { Ui.add(root, Ui.empty(a, "해당하는 특별 출입신청이 없습니다."), 14); return root; }
        for (int i = 0; i < list.length(); i++) Ui.add(root, item(list.optJSONObject(i), now), i == 0 ? 14 : 10);
        return root;
    }

    private View item(JSONObject r, long now) {
        LinearLayout c = Ui.card(a);
        LinearLayout head = Ui.hbox(a);
        head.addView(nameLine(r.optString("name"), r.optString("student_no")), new LinearLayout.LayoutParams(0, -2, 1));
        String st = state(r, now);
        boolean live = "이용 중".equals(st) || "첫 출입 대기".equals(st);
        head.addView(Ui.pill(a, st, live ? Ui.OK : "승인 대기".equals(st) ? Ui.WARN : Ui.MUTED, live ? Ui.OK_SOFT : "승인 대기".equals(st) ? Ui.WARN_SOFT : Ui.SUNKEN));
        c.addView(head);
        Ui.add(c, Ui.small(a, r.optString("room_name") + " · " + time(r)), 4);
        Ui.add(c, Ui.text(a, "사유: " + r.optString("purpose"), 14, Ui.FG), 6);
        String reason = Fmt.str(r, "reject_reason");
        if (!reason.isEmpty()) Ui.add(c, Ui.small(a, "관리자 멘트: " + reason), 4);

        int id = r.optInt("id");
        FlowLayout f = Ui.flow(a);
        String status = r.optString("status");
        if ("pending".equals(status)) {
            f.addView(Ui.smallButton(a, "승인·키 발급", Ui.GOOD, v -> Ui.confirm(a, "특별 출입을 승인할까요?",
                    "첫 출입부터 30분간 재출입 가능한 출입증을 발급합니다.", "승인·발급", false,
                    () -> act(() -> api.post("/api/admin/special-requests/" + id + "/approve", reasonBody("")), "처리했습니다."))));
            f.addView(Ui.smallButton(a, "반려", Ui.SECONDARY, v -> Ui.prompt(a, "특별 출입을 반려할까요?", "입력한 멘트를 신청자에게 전달합니다.",
                    "관리자 멘트 (선택, 200자 이내)", "", true, "반려", true,
                    t -> act(() -> api.post("/api/admin/special-requests/" + id + "/reject", reasonBody(t)), "처리했습니다."))));
        } else if ("approved".equals(status) && end(r) > now) {
            f.addView(Ui.smallButton(a, "승인 취소", Ui.SECONDARY, v -> Ui.prompt(a, "특별 출입 승인을 취소할까요?", "취소된 출입증은 즉시 사용할 수 없게 됩니다.",
                    "관리자 멘트 (선택, 200자 이내)", "", true, "승인 취소", true,
                    t -> act(() -> api.post("/api/admin/special-requests/" + id + "/revoke", reasonBody(t)), "처리했습니다."))));
        }
        if (f.getChildCount() > 0) Ui.add(c, f, 12);
        return c;
    }
}
