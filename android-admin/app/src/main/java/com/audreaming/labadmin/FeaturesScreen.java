package com.audreaming.labadmin;

import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Switch;

import org.json.JSONObject;

import java.util.LinkedHashMap;
import java.util.Map;

public class FeaturesScreen extends Screen {
    public FeaturesScreen(MainActivity a) { super(a); }

    @Override public String title() { return "기능 설정"; }

    @Override public boolean autoRefresh() { return false; }

    @Override
    public void load() {
        int seq = a.beginLoad();
        Ui.async(a, () -> api.get("/api/admin/settings").getJSONObject("settings"), s -> {
            if (!a.isLatest(seq)) return;
            a.adminSettings = s;
            a.render(build(s));
        }, a.loadError(seq));
    }

    private View build(JSONObject s) {
        LinearLayout root = page("기능 설정", "바꾸는 즉시 학생 화면과 출입 단말에 적용됩니다.");

        root.addView(Ui.groupTitle(a, "출입"));
        LinearLayout p1 = panel();
        p1.addView(sw(s, "lockdown", "긴급 잠금", "켜면 모든 학생 QR이 거부됩니다. 마스터 QR은 계속 쓸 수 있습니다.", true));
        p1.addView(Ui.divider(a));
        p1.addView(sw(s, "master_qr_enabled", "마스터 QR 사용", "끄면 발급된 마스터 QR로도 문이 열리지 않습니다.", false));
        p1.addView(Ui.divider(a));
        p1.addView(num(s, "door_open_ms", "문 열림 시간 (ms)", "출입 허용 시 릴레이를 켜 두는 시간 (1000~15000)"));
        root.addView(p1);

        root.addView(Ui.groupTitle(a, "실시간 보기"));
        LinearLayout p2 = panel();
        p2.addView(sw(s, "live_view", "단말 카메라 실시간 보기", "관리자가 출입 단말의 카메라 화면을 보고 단말 스피커로 말할 수 있습니다. 연결 중에는 단말 화면에 표시가 나오고, 영상은 저장하지 않습니다.", false));
        p2.addView(Ui.divider(a));
        p2.addView(sw(s, "live_mic", "단말 소리 듣기", "실시간 보기 중 단말 마이크 소리를 들을 수 있습니다. 녹음·저장하지 않습니다. 켜기 전에 출입문에 안내문을 붙여 주세요.", true));
        root.addView(p2);

        root.addView(Ui.groupTitle(a, "신청과 가입"));
        LinearLayout p3 = panel();
        p3.addView(sw(s, "reservations_open", "이용 신청 받기", "끄면 학생이 새 이용 신청을 할 수 없습니다. 이미 승인된 예약은 그대로입니다.", false));
        p3.addView(Ui.divider(a));
        p3.addView(num(s, "max_days_ahead", "신청 가능 기간 (일)", "오늘부터 며칠 뒤까지 신청을 받을지 (1~90)"));
        p3.addView(Ui.divider(a));
        p3.addView(sw(s, "weekend_open", "주말 신청 받기", "끄면 토요일과 일요일은 달력에서 고를 수 없습니다.", false));
        p3.addView(Ui.divider(a));
        p3.addView(num(s, "print_open_hour", "출력 시작 시각 (시)", "이 시각부터 이용을 신청할 수 있습니다"));
        p3.addView(Ui.divider(a));
        p3.addView(num(s, "print_close_hour", "출력 종료 시각 (시)", "이 시각까지 이용이 끝나야 합니다"));
        p3.addView(Ui.divider(a));
        p3.addView(sw(s, "registration_open", "새 가입 받기", "끄면 가입 화면에서 새 계정을 만들 수 없습니다.", false));
        p3.addView(Ui.divider(a));
        p3.addView(sw(s, "require_user_approval", "가입 승인 필요", "켜면 관리자가 승인해야 새 계정으로 로그인할 수 있습니다.", false));
        root.addView(p3);

        root.addView(Ui.groupTitle(a, "화면"));
        LinearLayout p4 = panel();
        Map<String, EditText> texts = new LinkedHashMap<>();
        textField(p4, texts, s, "site_name", "서비스 이름", "로그인 화면과 상단에 표시됩니다.", 1);
        textField(p4, texts, s, "notice", "공지", "로그인 화면과 학생 화면 위쪽에 보입니다. 비우면 숨깁니다.", 2);
        textField(p4, texts, s, "intro", "소개 문구", "첫 화면 제목 아래에 보입니다.", 3);
        textField(p4, texts, s, "location", "위치", "첫 화면에 함께 표시됩니다.", 1);
        textField(p4, texts, s, "contact", "문의", null, 1);
        textField(p4, texts, s, "manual_url", "설명서 링크", "https:// 로 시작하는 주소만 표시됩니다.", 1);
        textField(p4, texts, s, "feedback_url", "건의사항 링크", null, 1);
        textField(p4, texts, s, "closed_dates", "휴관일", "공휴일 외에 신청을 막을 날짜를 한 줄에 하나씩 적습니다. 예: 2026-11-20 장비 점검", 3);
        textField(p4, texts, s, "departments", "학부(과) 목록", "신청서 드롭다운에 나옵니다. 한 줄에 '학부: 전공, 전공' 형식으로 적습니다. 전공이 없으면 학부 이름만 적습니다.", 5);
        textField(p4, texts, s, "rules", "이용 규정", "한 줄에 하나씩 적습니다. 첫 화면에 번호를 붙여 보여 줍니다.", 4);
        textField(p4, texts, s, "cautions", "주의사항", "한 줄에 하나씩 적습니다.", 6);
        Ui.add(p4, Ui.small(a, "글자로 된 항목은 저장 버튼을 눌러야 반영됩니다."), 14);
        Ui.add(p4, Ui.wrapLeft(Ui.smallButton(a, "저장", Ui.PRIMARY, v -> {
            JSONObject patch = new JSONObject();
            try { for (Map.Entry<String, EditText> e : texts.entrySet()) patch.put(e.getKey(), e.getValue().getText().toString()); }
            catch (Exception ignored) { }
            save(patch, null);
        })), 8);
        root.addView(p4);
        return root;
    }

    private LinearLayout panel() {
        LinearLayout p = Ui.card(a);
        p.setPadding(Ui.dp(a, 14), Ui.dp(a, 4), Ui.dp(a, 14), Ui.dp(a, 4));
        return p;
    }

    private View sw(JSONObject s, String key, String title, String desc, boolean danger) {
        Switch t = Ui.toggle(a, s.optBoolean(key));
        t.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            boolean ignore;
            @Override public void onCheckedChanged(CompoundButton b, boolean on) {
                if (ignore) return;
                Runnable revert = () -> { ignore = true; b.setChecked(!on); ignore = false; };
                Runnable go = () -> save(jsonOf(key, on), revert);
                if ("live_mic".equals(key) && on) {
                    confirmOr(revert, "단말 소리 듣기를 켤까요?", "관리자가 실시간 보기 중 단말 주변 소리를 들을 수 있게 됩니다. 소리는 저장되지 않지만, 출입문에 \"관리자가 영상과 음성으로 응대할 수 있음\"을 알리는 안내문을 먼저 붙여 주세요.", "켜기", false, go);
                } else if ("lockdown".equals(key) && on) {
                    confirmOr(revert, "긴급 잠금을 켤까요?", "모든 학생 QR이 즉시 거부됩니다. 마스터 QR만 문을 열 수 있습니다.", "잠금 켜기", true, go);
                } else go.run();
            }
        });
        return Ui.settingRow(a, title, desc, t, danger);
    }

    private void confirmOr(Runnable revert, String title, String text, String ok, boolean danger, Runnable go) {
        android.app.AlertDialog d = new android.app.AlertDialog.Builder(a).setTitle(title).setMessage(text)
                .setNegativeButton("취소", (x, y) -> revert.run())
                .setPositiveButton(ok, (x, y) -> go.run())
                .setOnCancelListener(x -> revert.run())
                .create();
        d.show();
        if (danger) d.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setTextColor(Ui.BAD);
    }

    private View num(JSONObject s, String key, String title, String desc) {
        EditText e = Ui.number(a, s.optInt(key));
        e.setMinWidth(Ui.dp(a, 96));
        e.setImeOptions(EditorInfo.IME_ACTION_DONE);
        String[] last = {String.valueOf(s.optInt(key))};
        Runnable commit = () -> {
            String v = e.getText().toString().trim();
            if (v.equals(last[0]) || v.isEmpty()) return;
            try {
                int n = Integer.parseInt(v);
                save(jsonOf(key, n), () -> e.setText(last[0]));
                last[0] = v;
            } catch (NumberFormatException x) { e.setText(last[0]); }
        };
        e.setOnEditorActionListener((v, id, ev) -> { commit.run(); e.clearFocus(); return false; });
        e.setOnFocusChangeListener((v, has) -> { if (!has) commit.run(); });
        return Ui.settingRow(a, title, desc, e, false);
    }

    private void textField(LinearLayout p, Map<String, EditText> texts, JSONObject s, String key, String title, String desc, int lines) {
        EditText e = lines > 1 ? Ui.multiline(a, Fmt.str(s, key), lines) : Ui.input(a, Fmt.str(s, key), "");
        LinearLayout f = Ui.vbox(a);
        f.setPadding(0, Ui.dp(a, 10), 0, 0);
        f.addView(Ui.bold(a, title, 15));
        if (desc != null) f.addView(Ui.small(a, desc));
        Ui.add(f, e, 6);
        p.addView(f);
        texts.put(key, e);
    }

    private static JSONObject jsonOf(String k, Object v) {
        try { return new JSONObject().put(k, v); } catch (Exception e) { return new JSONObject(); }
    }

    private void save(JSONObject patch, Runnable onFail) {
        Ui.async(a, () -> api.patch("/api/admin/settings", patch), r -> {
            a.updateSettings(r.optJSONObject("settings"));
            a.dataChanged();
            Ui.toast(a, "저장했습니다.");
        }, e -> {
            Ui.toast(a, e.getMessage());
            if (onFail != null) onFail.run();
        });
    }
}
