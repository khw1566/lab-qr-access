package com.audreaming.labadmin;

import android.app.AlertDialog;
import android.app.DatePickerDialog;
import android.app.TimePickerDialog;
import android.net.Uri;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Calendar;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

public class UsersScreen extends Screen {
    private String filter = "all", search = "", draft = "";
    private int page = 1;

    public UsersScreen(MainActivity a) { super(a); }

    @Override public String title() { return "사용자"; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        String path = "/api/admin/users?status=" + filter + "&q=" + Uri.encode(search) + "&page=" + page;
        Ui.async(a, () -> api.get(path), o -> { if (a.isLatest(seq)) a.render(build(o)); }, a.loadError(seq));
    }

    private View build(JSONObject o) {
        page = o.optInt("page", 1);
        int pages = o.optInt("pages", 1);
        LinearLayout root = page("사용자", "학번·이름으로 검색하고 계정 정보와 비밀번호를 관리합니다.");
        Ui.add(root, segment(new String[][]{{"all", "전체"}, {"pending", "가입 대기"}, {"active", "사용 중"}, {"blocked", "정지"}}, filter,
                k -> { filter = k; page = 1; a.show("users"); }), 12);

        LinearLayout sc = Ui.card(a);
        EditText q = Ui.input(a, draft, "학번 또는 이름의 일부 입력");
        q.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        q.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int st, int c, int af) { }
            public void onTextChanged(CharSequence s, int st, int b, int c) { }
            public void afterTextChanged(Editable e) { draft = e.toString(); }
        });
        Runnable doSearch = () -> { search = draft.trim(); page = 1; a.show("users"); };
        q.setOnEditorActionListener((v, id, e) -> { doSearch.run(); return true; });
        sc.addView(Ui.field(a, "학번·이름 검색", q));
        LinearLayout row = Ui.hbox(a);
        row.addView(Ui.smallButton(a, "검색", Ui.PRIMARY, v -> doSearch.run()));
        View sp = new View(a);
        row.addView(sp, new LinearLayout.LayoutParams(Ui.dp(a, 8), 1));
        row.addView(Ui.smallButton(a, "초기화", Ui.SECONDARY, v -> { search = ""; draft = ""; filter = "all"; page = 1; a.show("users"); }));
        TextView total = Ui.small(a, "   총 " + o.optInt("total") + "명");
        row.addView(total);
        Ui.add(sc, row, 10);
        Ui.add(root, sc, 12);
        if ("blocked".equals(filter))
            Ui.add(root, Ui.small(a, "이전에 반려한 가입 정보는 이용 이력이 없으면 삭제할 수 있습니다. 삭제하면 해당 학번으로 다시 가입할 수 있습니다."), 10);

        JSONArray users = o.optJSONArray("users");
        if (users == null || users.length() == 0) Ui.add(root, Ui.empty(a, "해당하는 사용자가 없습니다."), 12);
        else for (int i = 0; i < users.length(); i++) Ui.add(root, item(users.optJSONObject(i)), i == 0 ? 12 : 10);

        LinearLayout pager = Ui.hbox(a);
        pager.addView(Ui.small(a, page + " / " + pages + " 페이지 · " + o.optInt("page_size", 50) + "명씩"), new LinearLayout.LayoutParams(0, -2, 1));
        Button prev = Ui.smallButton(a, "이전", Ui.SECONDARY, v -> { page--; a.show("users"); });
        prev.setEnabled(page > 1);
        Button next = Ui.smallButton(a, "다음", Ui.SECONDARY, v -> { page++; a.show("users"); });
        next.setEnabled(page < pages);
        pager.addView(prev);
        View g = new View(a);
        pager.addView(g, new LinearLayout.LayoutParams(Ui.dp(a, 8), 1));
        pager.addView(next);
        Ui.add(root, pager, 14);
        return root;
    }

    private View item(JSONObject u) {
        LinearLayout c = Ui.card(a);
        int id = u.optInt("id");
        boolean self = id == a.myId();
        String status = u.optString("status"), role = u.optString("role");
        LinearLayout head = Ui.hbox(a);
        head.addView(nameLine(u.optString("name") + (self ? " (나)" : ""), u.optString("student_no")), new LinearLayout.LayoutParams(0, -2, 1));
        if ("admin".equals(role)) head.addView(Ui.pill(a, "관리자", Ui.MASTER_INK, Ui.MASTER));
        c.addView(head);
        String dept = Fmt.str(u, "department"), phone = Fmt.str(u, "phone");
        c.addView(Ui.small(a, (dept.isEmpty() ? "-" : dept) + " · " + (phone.isEmpty() ? "-" : phone)));
        String st = "pending".equals(status) ? "가입 대기" : "active".equals(status) ? "사용 중" : "blocked".equals(status) ? "정지" : status;
        long consent = Fmt.ms(u, "privacy_consent_at");
        c.addView(Ui.small(a, st + " · " + Fmt.date(Fmt.ms(u, "created_at")) + " 가입 · " + (consent > 0 ? "개인정보 동의 " + Fmt.date(consent) : "개인정보 동의 기록 없음")));

        FlowLayout f = Ui.flow(a);
        f.addView(Ui.smallButton(a, "정보 수정", Ui.SECONDARY, v -> edit(u)));
        f.addView(Ui.smallButton(a, "비밀번호 변경", Ui.SECONDARY, v -> changePassword(u)));
        if ("active".equals(status)) f.addView(Ui.smallButton(a, "출입권한 부여", Ui.GOOD, v -> grant(u)));
        if (!"active".equals(status))
            f.addView(Ui.smallButton(a, "pending".equals(status) ? "가입 승인" : "정지 해제", Ui.GOOD, v -> op(u, "activate")));
        if ("active".equals(status) && !self)
            f.addView("admin".equals(role) ? Ui.smallButton(a, "관리자 해제", Ui.SECONDARY, v -> op(u, "make-student"))
                    : Ui.smallButton(a, "관리자로 지정", Ui.SECONDARY, v -> op(u, "make-admin")));
        if (!"blocked".equals(status) && !self)
            f.addView(Ui.smallButton(a, "pending".equals(status) ? "가입 반려" : "정지", Ui.SECONDARY, v -> op(u, "pending".equals(status) ? "reject" : "block")));
        if ((u.optInt("can_release") == 1 || u.optBoolean("can_release")) && !self)
            f.addView(Ui.smallButton(a, "가입 정보 삭제", Ui.SECONDARY, v -> op(u, "release")));
        Ui.add(c, f, 12);
        return c;
    }

    private void op(JSONObject u, String op) {
        int id = u.optInt("id");
        String who = u.optString("name") + " (" + u.optString("student_no") + ")";
        String path = "/api/admin/users/" + id + "/" + op;
        String deleted = "가입 정보를 삭제했습니다. 해당 학번으로 다시 가입할 수 있습니다.";
        switch (op) {
            case "reject":
                Ui.prompt(a, "가입을 반려할까요?", who + "의 가입 정보를 삭제합니다. 해당 학번으로 다시 가입할 수 있습니다. 신청자가 기존 비밀번호로 로그인하면 관리자 멘트를 확인할 수 있습니다.",
                        "관리자 멘트 (선택, 200자 이내)", "", true, "반려", true, t -> act(() -> api.post(path, reasonBody(t)), deleted));
                break;
            case "release":
                Ui.confirm(a, "가입 정보를 삭제할까요?", who + "의 가입 정보를 삭제합니다. 해당 학번으로 다시 가입할 수 있습니다.", "삭제", true,
                        () -> act(() -> api.post(path, null), deleted));
                break;
            case "block":
                Ui.confirm(a, "이 사용자를 정지할까요?", "정지하면 로그인과 출입 QR이 모두 막힙니다.", "정지", true, () -> act(() -> api.post(path, null), "변경했습니다."));
                break;
            case "make-admin":
                Ui.confirm(a, "관리자로 지정할까요?", "관리자는 모든 설정을 바꾸고 마스터 QR을 발급할 수 있습니다.", "지정", false, () -> act(() -> api.post(path, null), "변경했습니다."));
                break;
            default:
                act(() -> api.post(path, null), "변경했습니다.");
        }
    }

    private void edit(JSONObject u) {
        LinearLayout l = dialogBox();
        EditText name = Ui.input(a, u.optString("name"), ""), sno = Ui.input(a, u.optString("student_no"), "");
        EditText dept = Ui.input(a, Fmt.str(u, "department"), ""), phone = Ui.input(a, Fmt.str(u, "phone"), "");
        phone.setInputType(InputType.TYPE_CLASS_PHONE);
        Ui.add(l, Ui.field(a, "이름", name), 0);
        Ui.add(l, Ui.field(a, "학번", sno), 10);
        Ui.add(l, Ui.field(a, "학부(과)", dept), 10);
        Ui.add(l, Ui.field(a, "연락처", phone), 10);
        AlertDialog d = new AlertDialog.Builder(a).setTitle("사용자 정보 수정").setView(Ui.scroll(l))
                .setNegativeButton("취소", null).setPositiveButton("저장", null).create();
        d.show();
        d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> Ui.async(a, () -> {
            JSONObject b = new JSONObject().put("name", name.getText().toString().trim()).put("student_no", sno.getText().toString().trim())
                    .put("department", dept.getText().toString().trim()).put("phone", phone.getText().toString().trim());
            return api.patch("/api/admin/users/" + u.optInt("id"), b);
        }, r -> { d.dismiss(); Ui.toast(a, "저장했습니다."); a.dataChanged(); reload(); }));
    }

    private void changePassword(JSONObject u) {
        LinearLayout l = dialogBox();
        Ui.add(l, Ui.text(a, u.optString("name") + " · " + u.optString("student_no"), 15, Ui.FG), 0);
        Ui.add(l, Ui.small(a, "기존 비밀번호는 복원할 수 없는 방식으로 저장되어 열람할 수 없습니다. 변경하면 이 계정의 기존 로그인 세션이 해제됩니다."), 6);
        EditText p1 = Ui.input(a, "", ""), p2 = Ui.input(a, "", "");
        int hidden = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD;
        p1.setInputType(hidden);
        p2.setInputType(hidden);
        CheckBox show = Ui.check(a, "입력한 새 비밀번호 표시", false);
        show.setOnCheckedChangeListener((b, on) -> {
            int t = on ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD : hidden;
            p1.setInputType(t);
            p2.setInputType(t);
        });
        TextView err = Ui.text(a, "", 13, Ui.BAD);
        err.setVisibility(View.GONE);
        Ui.add(l, Ui.field(a, "새 비밀번호 (8~128자)", p1), 12);
        Ui.add(l, Ui.field(a, "새 비밀번호 확인", p2), 10);
        Ui.add(l, show, 6);
        Ui.add(l, err, 4);
        AlertDialog d = new AlertDialog.Builder(a).setTitle("비밀번호 변경").setView(Ui.scroll(l))
                .setNegativeButton("취소", null).setPositiveButton("변경", null).create();
        d.show();
        d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String a1 = p1.getText().toString(), a2 = p2.getText().toString();
            if (a1.length() < 8 || a1.length() > 128) { err.setText("새 비밀번호는 8~128자로 입력해 주세요."); err.setVisibility(View.VISIBLE); return; }
            if (!a1.equals(a2)) { err.setText("새 비밀번호가 일치하지 않습니다."); err.setVisibility(View.VISIBLE); return; }
            int id = u.optInt("id");
            Ui.async(a, () -> api.post("/api/admin/users/" + id + "/password", new JSONObject().put("password", a1)), r -> {
                d.dismiss();
                if (id == a.myId()) {
                    Ui.toast(a, "비밀번호를 변경했습니다. 새 비밀번호로 다시 로그인해 주세요.");
                    a.sessionExpired();
                } else Ui.toast(a, "비밀번호를 변경했습니다. 사용자는 새 비밀번호로 로그인할 수 있습니다.");
            }, e -> { err.setText(e.getMessage()); err.setVisibility(View.VISIBLE); });
        });
    }

    /** 관리자 출입권한 부여: 학생 대신 기간·시간대를 정해 승인된 이용 건을 만든다. */
    private void grant(JSONObject u) {
        Ui.async(a, () -> api.get("/api/rooms?all=1").getJSONArray("rooms"), rooms -> {
            List<JSONObject> rs = new ArrayList<>();
            List<String> names = new ArrayList<>();
            for (int i = 0; i < rooms.length(); i++) {
                JSONObject r = rooms.optJSONObject(i);
                if (r.optInt("enabled") == 1) { rs.add(r); names.add(r.optString("name")); }
            }
            if (rs.isEmpty()) { Ui.toast(a, "운영 중인 실습실이 없습니다."); return; }
            showGrant(u, rs, names);
        });
    }

    private void showGrant(JSONObject u, List<JSONObject> rs, List<String> names) {
        LinearLayout l = dialogBox();
        Ui.add(l, Ui.text(a, u.optString("name") + " · " + u.optString("student_no"), 15, Ui.FG), 0);
        Ui.add(l, Ui.small(a, "이용 신청과 승인을 대신 처리합니다. 기간 안의 날마다 같은 시간대로 승인된 이용 건이 만들어지고, 사용자는 출입증 탭에서 QR을 열 수 있습니다."), 6);
        Spinner room = Ui.spinner(a, names, 0);
        Ui.add(l, Ui.field(a, "실습실", room), 12);
        String today = Fmt.isoDay(System.currentTimeMillis());
        String[] v = {today, today, "09:00", "22:00"};
        Button from = pickerButton(v[0]), to = pickerButton(v[1]), st = pickerButton(v[2]), et = pickerButton(v[3]);
        from.setOnClickListener(x -> pickDate(v[0], d -> { v[0] = d; from.setText(d); if (v[1].compareTo(d) < 0) { v[1] = d; to.setText(d); } }));
        to.setOnClickListener(x -> pickDate(v[1], d -> { if (d.compareTo(v[0]) < 0) { Ui.toast(a, "끝 날짜가 시작 날짜보다 빠릅니다."); return; } v[1] = d; to.setText(d); }));
        st.setOnClickListener(x -> pickTime(v[2], t -> { v[2] = t; st.setText(t); }));
        et.setOnClickListener(x -> pickTime(v[3], t -> { v[3] = t; et.setText(t); }));
        Ui.add(l, pair(Ui.field(a, "시작 날짜", from), Ui.field(a, "끝 날짜", to)), 10);
        Ui.add(l, pair(Ui.field(a, "출입 시작", st), Ui.field(a, "출입 끝", et)), 10);
        CheckBox skip = Ui.check(a, "주말·공휴일·휴관일은 빼기", true);
        Ui.add(l, skip, 6);
        EditText purpose = Ui.input(a, "", "예: 졸업 작품 제작 기간");
        Ui.add(l, Ui.field(a, "사유 (사용자에게 보임)", purpose), 6);
        TextView err = Ui.text(a, "", 13, Ui.BAD);
        err.setVisibility(View.GONE);
        Ui.add(l, err, 6);
        AlertDialog d = new AlertDialog.Builder(a).setTitle("출입권한 부여").setView(Ui.scroll(l))
                .setNegativeButton("취소", null).setPositiveButton("부여", null).create();
        d.show();
        d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(x -> {
            err.setVisibility(View.GONE);
            JSONObject roomObj = rs.get(room.getSelectedItemPosition());
            Ui.async(a, () -> api.post("/api/admin/users/" + u.optInt("id") + "/grant", new JSONObject()
                    .put("room_id", roomObj.optInt("id")).put("from", v[0]).put("to", v[1])
                    .put("start_time", v[2]).put("end_time", v[3]).put("skip_closed", skip.isChecked())
                    .put("purpose", purpose.getText().toString().trim())), r -> {
                d.dismiss();
                JSONArray skipped = r.optJSONArray("skipped");
                String msg = u.optString("name") + "님에게 " + r.optInt("created") + "일 출입권한을 부여했습니다.";
                if (skipped != null && skipped.length() > 0) {
                    Set<String> why = new LinkedHashSet<>();
                    for (int i = 0; i < skipped.length(); i++) why.add(skipped.optJSONObject(i).optString("why"));
                    msg += " (" + skipped.length() + "일 제외: " + String.join(", ", why) + ")";
                }
                Ui.toast(a, msg);
                a.dataChanged();
            }, e -> { err.setText(e.getMessage()); err.setVisibility(View.VISIBLE); });
        });
    }

    private LinearLayout dialogBox() {
        LinearLayout l = Ui.vbox(a);
        int p = Ui.dp(a, 20);
        l.setPadding(p, Ui.dp(a, 8), p, 0);
        return l;
    }

    private View pair(View x, View y) {
        LinearLayout r = Ui.hbox(a);
        r.addView(x, new LinearLayout.LayoutParams(0, -2, 1));
        View g = new View(a);
        r.addView(g, new LinearLayout.LayoutParams(Ui.dp(a, 10), 1));
        r.addView(y, new LinearLayout.LayoutParams(0, -2, 1));
        return r;
    }

    private Button pickerButton(String s) {
        Button b = Ui.button(a, s, Ui.SECONDARY);
        b.setTypeface(android.graphics.Typeface.MONOSPACE);
        return b;
    }

    private void pickDate(String cur, Ui.Entered ok) {
        String[] p = cur.split("-");
        DatePickerDialog d = new DatePickerDialog(a, (view, y, m, day) -> ok.ok(String.format(Locale.US, "%04d-%02d-%02d", y, m + 1, day)),
                Integer.parseInt(p[0]), Integer.parseInt(p[1]) - 1, Integer.parseInt(p[2]));
        Calendar c = Fmt.cal(System.currentTimeMillis());
        c.set(Calendar.HOUR_OF_DAY, 0);
        d.getDatePicker().setMinDate(c.getTimeInMillis() - 1000);
        d.show();
    }

    private void pickTime(String cur, Ui.Entered ok) {
        String[] p = cur.split(":");
        new TimePickerDialog(a, (view, h, m) -> ok.ok(String.format(Locale.US, "%02d:%02d", h, m)),
                Integer.parseInt(p[0]), Integer.parseInt(p[1]), true).show();
    }
}
