package com.audreaming.labadmin;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** 관리자 메인: 위쪽 메뉴 띠에서 화면을 고르고, 아래 영역에 그 화면을 그린다. */
public class MainActivity extends Activity {
    private static final long VERSION_POLL_MS = 15_000;

    private final Map<String, Screen> screens = new LinkedHashMap<>();
    private final Map<String, Button> tabs = new LinkedHashMap<>();
    private String current = "overview";
    private FrameLayout content;
    private ProgressBar loading;
    private TextView titleView;
    private HorizontalScrollView tabScroll;
    private String dataVersion;
    private boolean resumed;
    private JSONObject me;
    private int loadSeq;
    private boolean keepScroll;

    /** 웹의 settings·info 처럼 여러 화면이 함께 쓰는 값. */
    public JSONObject adminSettings = new JSONObject();
    public JSONObject config = new JSONObject();

    private final Runnable versionPoll = new Runnable() {
        @Override public void run() {
            if (!resumed) return;
            Api api = Api.get(MainActivity.this);
            Ui.async(MainActivity.this, () -> api.get("/api/version").optString("v"), v -> {
                if (dataVersion != null && !v.equals(dataVersion)) {
                    refreshCounts();
                    Screen s = screens.get(current);
                    if (s != null && s.autoRefresh()) { keepScroll = true; s.load(); }
                }
                dataVersion = v;
                Ui.later(versionPoll, VERSION_POLL_MS);
            }, e -> Ui.later(versionPoll, VERSION_POLL_MS));
        }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (b != null) current = b.getString("screen", current);
        screens.put("overview", new OverviewScreen(this));
        screens.put("requests", new RequestsScreen(this));
        screens.put("special", new SpecialScreen(this));
        screens.put("users", new UsersScreen(this));
        screens.put("master", new MasterScreen(this));
        screens.put("equipment", new EquipmentScreen(this));
        screens.put("rooms", new RoomsScreen(this));
        screens.put("devices", new DevicesScreen(this));
        screens.put("logs", new LogsScreen(this));
        screens.put("features", new FeaturesScreen(this));
        buildUi();
        loadShared(() -> show(current));
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putString("screen", current);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        Ui.cancel(versionPoll);
        Ui.later(versionPoll, 1000);
    }

    @Override
    protected void onPause() {
        super.onPause();
        resumed = false;
        Ui.cancel(versionPoll);
    }

    @Override
    public void onBackPressed() {
        if (!"overview".equals(current)) show("overview");
        else super.onBackPressed();
    }

    private void buildUi() {
        LinearLayout root = Ui.vbox(this);
        root.setBackgroundColor(Ui.BG);

        // 상단 막대
        LinearLayout bar = Ui.hbox(this);
        bar.setBackgroundColor(Ui.SURFACE);
        bar.setPadding(Ui.dp(this, 16), Ui.dp(this, 8), Ui.dp(this, 4), Ui.dp(this, 8));
        titleView = Ui.bold(this, "실습실 출입 관리자", 18);
        bar.addView(titleView, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        loading = new ProgressBar(this);
        loading.setIndeterminate(true);
        loading.setVisibility(View.INVISIBLE);
        bar.addView(loading, new LinearLayout.LayoutParams(Ui.dp(this, 22), Ui.dp(this, 22)));
        ImageButton refresh = iconButton(android.R.drawable.ic_popup_sync, "새로고침");
        refresh.setOnClickListener(v -> { refreshCounts(); reloadCurrent(); });
        bar.addView(refresh);
        ImageButton more = iconButton(android.R.drawable.ic_menu_more, "메뉴");
        more.setOnClickListener(this::showMenu);
        bar.addView(more);
        root.addView(bar);

        // 메뉴 띠
        LinearLayout strip = Ui.hbox(this);
        strip.setPadding(Ui.dp(this, 12), Ui.dp(this, 6), Ui.dp(this, 12), Ui.dp(this, 8));
        String[][] names = {
            {"overview", "현황"}, {"requests", "이용 신청"}, {"special", "특별 출입"}, {"users", "사용자"},
            {"master", "마스터 QR"}, {"equipment", "장비 상태"}, {"rooms", "실습실"}, {"devices", "출입 단말"},
            {"logs", "출입 기록"}, {"features", "기능 설정"},
        };
        for (String[] n : names) {
            Button t = Ui.button(this, n[1], Ui.SECONDARY);
            t.setTextSize(14);
            t.setOnClickListener(v -> show(n[0]));
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, Ui.dp(this, 38));
            p.rightMargin = Ui.dp(this, 6);
            strip.addView(t, p);
            tabs.put(n[0], t);
        }
        tabScroll = new HorizontalScrollView(this);
        tabScroll.setHorizontalScrollBarEnabled(false);
        tabScroll.setBackgroundColor(Ui.SURFACE);
        tabScroll.addView(strip);
        root.addView(tabScroll);
        root.addView(Ui.divider(this));

        content = new FrameLayout(this);
        root.addView(content, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        // Android 15부터 화면 끝까지 그려지므로 상태 표시줄·내비게이션 막대만큼 띄운다
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.ime());
            v.setPadding(i.left, i.top, i.right, i.bottom);
            return WindowInsets.CONSUMED;
        });
        getWindow().setStatusBarColor(Color.WHITE);
        setContentView(root);
    }

    private ImageButton iconButton(int res, String desc) {
        ImageButton b = new ImageButton(this);
        b.setImageResource(res);
        b.setColorFilter(Ui.FG);
        b.setBackground(null);
        b.setContentDescription(desc);
        int p = Ui.dp(this, 10);
        b.setPadding(p, p, p, p);
        return b;
    }

    private void showMenu(View anchor) {
        PopupMenu m = new PopupMenu(this, anchor);
        String who = me != null ? me.optString("name") + " (" + me.optString("student_no") + ")" : "";
        if (!who.isEmpty()) m.getMenu().add(0, 0, 0, who).setEnabled(false);
        m.getMenu().add(0, 1, 1, "웹 관리자 열기");
        m.getMenu().add(0, 2, 2, "로그아웃");
        m.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == 1) {
                startActivity(new Intent(Intent.ACTION_VIEW, android.net.Uri.parse(Api.get(this).base() + "/#admin")));
            } else if (item.getItemId() == 2) {
                Ui.confirm(this, "로그아웃할까요?", "이 기기에서 관리자 로그인이 해제됩니다.", "로그아웃", false, this::logout);
            }
            return true;
        });
        m.show();
    }

    private void logout() {
        Api api = Api.get(this);
        Ui.async(this, () -> { try { api.post("/api/auth/logout", null); } catch (Exception ignored) { } api.clearSession(); return true; },
                ok -> { startActivity(new Intent(this, LoginActivity.class)); finish(); });
    }

    public void sessionExpired() {
        if (isFinishing()) return;
        Api.get(this).clearSession();
        Ui.toast(this, "로그인이 만료되었습니다. 다시 로그인해 주세요.");
        startActivity(new Intent(this, LoginActivity.class));
        finish();
    }

    /** 처음 한 번: 내 정보, 공개 설정(장비 목록), 관리자 설정. */
    private void loadShared(Runnable then) {
        Api api = Api.get(this);
        setLoading(true);
        Ui.async(this, () -> {
            JSONObject m = api.get("/api/me");
            JSONObject c = api.get("/api/config");
            JSONObject s = api.get("/api/admin/settings").getJSONObject("settings");
            return new JSONObject[]{m, c, s};
        }, r -> {
            me = r[0].optJSONObject("user");
            config = r[1];
            adminSettings = r[2];
            titleView.setText(config.optString("site_name", "실습실 출입 관리자"));
            setLoading(false);
            then.run();
        }, e -> {
            setLoading(false);
            if (e instanceof Api.ApiException && ((Api.ApiException) e).status == 403) {
                Ui.toast(this, "관리자 권한이 없습니다.");
                Api.get(this).clearSession();
                startActivity(new Intent(this, LoginActivity.class));
                finish();
                return;
            }
            showError(e.getMessage(), () -> loadShared(then));
        });
    }

    public JSONObject me() { return me; }

    public int myId() { return me != null ? me.optInt("id") : -1; }

    public void show(String key) {
        if (!screens.containsKey(key)) key = "overview";
        current = key;
        for (Map.Entry<String, Button> e : tabs.entrySet()) {
            boolean on = e.getKey().equals(key);
            Button t = e.getValue();
            t.setBackground(Ui.box(on ? Ui.FG : Ui.SURFACE, on ? 0 : Ui.LINE_STRONG, 8, this));
            t.setTextColor(on ? Color.WHITE : Ui.FG);
            if (on) tabScroll.post(() -> tabScroll.smoothScrollTo(Math.max(0, t.getLeft() - Ui.dp(this, 40)), 0));
        }
        keepScroll = false;
        refreshCounts();
        screens.get(key).load();
    }

    public void reloadCurrent() {
        keepScroll = true;
        Screen s = screens.get(current);
        if (s != null) s.load();
    }

    /** 화면이 데이터를 받기 시작할 때 부른다. 돌아온 응답이 마지막 요청의 것인지 확인할 번호를 준다. */
    public int beginLoad() {
        setLoading(true);
        return ++loadSeq;
    }

    public boolean isLatest(int seq) { return seq == loadSeq; }

    /** 화면 내용을 바꾼다. 같은 화면을 다시 그릴 때는 스크롤 위치를 지킨다. */
    public void render(View page) {
        setLoading(false);
        int y = 0;
        if (keepScroll && content.getChildCount() > 0 && content.getChildAt(0) instanceof ScrollView)
            y = content.getChildAt(0).getScrollY();
        keepScroll = false;
        content.removeAllViews();
        ScrollView sv = Ui.scroll(page);
        content.addView(sv, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        final int sy = y;
        if (sy > 0) sv.post(() -> sv.scrollTo(0, sy));
    }

    public void showError(String msg, Runnable retry) {
        setLoading(false);
        LinearLayout l = Ui.vbox(this);
        l.setGravity(Gravity.CENTER);
        int p = Ui.dp(this, 24);
        l.setPadding(p, p, p, p);
        TextView t = Ui.text(this, msg, 15, Ui.MUTED);
        t.setGravity(Gravity.CENTER);
        l.addView(t);
        Button b = Ui.button(this, "다시 시도", Ui.SECONDARY);
        b.setOnClickListener(v -> retry.run());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = Ui.dp(this, 14);
        l.addView(b, lp);
        content.removeAllViews();
        content.addView(l, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** 화면별 오류 처리: 마지막 요청이면 오류 화면을 띄운다. */
    public Ui.Done<Exception> loadError(int seq) {
        return e -> { if (isLatest(seq)) showError(e.getMessage(), this::reloadCurrent); };
    }

    public void setLoading(boolean on) { loading.setVisibility(on ? View.VISIBLE : View.INVISIBLE); }

    /** 내가 데이터를 바꿨다: 다음 버전 확인 때 내 변경으로 다시 불러오지 않도록 버전을 새로 받는다. */
    public void dataChanged() {
        Api api = Api.get(this);
        Ui.async(this, () -> api.get("/api/version").optString("v"), v -> dataVersion = v, e -> { });
        refreshCounts();
    }

    /** 메뉴 띠의 대기 건수 표시. */
    public void refreshCounts() {
        Api api = Api.get(this);
        Ui.async(this, () -> api.get("/api/admin/overview"), o -> {
            setCount("requests", "이용 신청", o.optInt("pending_reservations"));
            setCount("special", "특별 출입", o.optInt("pending_special"));
            setCount("users", "사용자", o.optInt("pending_users"));
        }, e -> { });
    }

    private void setCount(String key, String label, int n) {
        Button t = tabs.get(key);
        if (t != null) t.setText(n > 0 ? label + "  " + n : label);
    }

    /** 관리자 설정을 고친 뒤 공통 값을 새로 받는다. */
    public void updateSettings(JSONObject s) {
        if (s != null) adminSettings = s;
        Api api = Api.get(this);
        Ui.async(this, () -> api.get("/api/config"), c -> {
            config = c;
            titleView.setText(config.optString("site_name", "실습실 출입 관리자"));
        }, e -> { });
    }

    public static List<String> labels(String... s) {
        List<String> l = new ArrayList<>();
        for (String x : s) l.add(x);
        return l;
    }

    static TextView boldInline(Activity a, String s) {
        TextView t = Ui.text(a, s, 15, Ui.FG);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }
}
