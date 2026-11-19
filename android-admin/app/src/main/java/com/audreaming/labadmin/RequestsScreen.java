package com.audreaming.labadmin;

import android.app.AlertDialog;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Base64;
import android.view.View;
import android.widget.ImageView;
import android.widget.LinearLayout;

import org.json.JSONArray;
import org.json.JSONObject;

public class RequestsScreen extends Screen {
    private String filter = "pending";

    public RequestsScreen(MainActivity a) { super(a); }

    @Override public String title() { return "이용 신청 승인"; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        String f = filter;
        Ui.async(a, () -> api.get("/api/admin/reservations?status=" + f), o -> { if (a.isLatest(seq)) a.render(build(o)); }, a.loadError(seq));
    }

    private View build(JSONObject o) {
        LinearLayout root = page("이용 신청 승인", "승인하면 학생 화면에 출입 QR이 열립니다.");
        Ui.add(root, segment(new String[][]{{"pending", "승인 대기"}, {"approved", "승인됨"}, {"rejected", "반려"}}, filter,
                k -> { filter = k; a.show("requests"); }), 12);
        JSONArray list = o.optJSONArray("reservations");
        if (list == null || list.length() == 0) { Ui.add(root, Ui.empty(a, "해당하는 신청이 없습니다."), 14); return root; }
        for (int i = 0; i < list.length(); i++) Ui.add(root, item(list.optJSONObject(i)), i == 0 ? 14 : 10);
        return root;
    }

    private View item(JSONObject r) {
        LinearLayout c = Ui.card(a);
        c.addView(nameLine(r.optString("name"), r.optString("student_no")));
        String dept = Fmt.str(r, "department"), phone = Fmt.str(r, "phone");
        if (!dept.isEmpty() || !phone.isEmpty()) c.addView(Ui.small(a, dept + (phone.isEmpty() ? "" : " · " + phone)));
        String pt = Fmt.PT.containsKey(r.optString("printer_type")) ? " · " + Fmt.PT.get(r.optString("printer_type")) : "";
        Ui.add(c, Ui.mono(a, r.optString("room_name") + " · " + Fmt.range(Fmt.ms(r, "start_at"), Fmt.ms(r, "end_at")) + pt, 14, Ui.FG), 8);
        Ui.add(c, Ui.text(a, "목적: " + r.optString("purpose"), 14, Ui.FG), 6);
        Ui.add(c, Ui.small(a, "지도교수: " + (Fmt.str(r, "advisor_name").isEmpty() ? "-" : r.optString("advisor_name"))), 4);
        String reason = Fmt.str(r, "reject_reason");
        if (!reason.isEmpty()) Ui.add(c, Ui.text(a, "관리자 멘트: " + reason, 13, Ui.BAD), 4);
        String status = r.optString("status");
        if ("approved".equals(status) && r.isNull("decided_by")) Ui.add(c, Ui.small(a, "자동 승인"), 4);
        else if (!r.isNull("granted_by")) Ui.add(c, Ui.small(a, "관리자 출입권한 부여"), 4);

        FlowLayout f = Ui.flow(a);
        int id = r.optInt("id");
        if (r.optInt("has_sig") == 1 || r.optBoolean("has_sig"))
            f.addView(Ui.smallButton(a, "서명 보기", Ui.SECONDARY, v -> showSignature(id)));
        if ("pending".equals(status))
            f.addView(Ui.smallButton(a, "승인", Ui.GOOD, v -> act(() -> api.post("/api/admin/reservations/" + id + "/approve", null), "승인했습니다.")));
        if (!"rejected".equals(status)) {
            String label = "approved".equals(status) ? "승인 취소" : "반려";
            f.addView(Ui.smallButton(a, label, Ui.SECONDARY, v -> Ui.prompt(a, "신청을 반려할까요?", "학생에게 반려 사유가 표시됩니다.",
                    "관리자 멘트 (선택, 200자 이내)", "", true, "반려", true,
                    text -> act(() -> api.post("/api/admin/reservations/" + id + "/reject", reasonBody(text)), "반려했습니다."))));
        }
        if (f.getChildCount() > 0) Ui.add(c, f, 12);
        return c;
    }

    private void showSignature(int id) {
        Ui.async(a, () -> api.get("/api/admin/reservations/" + id + "/signature"), r -> {
            String data = r.optString("advisor_sig");
            int comma = data.indexOf(',');
            byte[] bytes = Base64.decode(comma >= 0 ? data.substring(comma + 1) : data, Base64.DEFAULT);
            Bitmap bm = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
            ImageView iv = new ImageView(a);
            iv.setImageBitmap(bm);
            iv.setAdjustViewBounds(true);
            iv.setBackgroundColor(0xFFFFFFFF);
            int p = Ui.dp(a, 16);
            iv.setPadding(p, p, p, p);
            new AlertDialog.Builder(a).setTitle("지도교수 서명 · " + r.optString("advisor_name")).setView(iv).setPositiveButton("닫기", null).show();
        });
    }
}
